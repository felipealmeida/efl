/* EINA - EFL data type library
 * Copyright (C) 2013 Cedric Bail
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#ifdef HAVE_DIRENT_H
# include <dirent.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_EVIL
# include <Evil.h>
#endif

#define COPY_BLOCKSIZE (4 * 1024 * 1024)

#include "eina_config.h"
#include "eina_private.h"

#include "eina_hash.h"
#include "eina_safety_checks.h"
#include "eina_file_common.h"
#include "eina_xattr.h"

#ifdef HAVE_ESCAPE
# include <Escape.h>
#endif

Eina_Hash *_eina_file_cache = NULL;
Eina_Lock _eina_file_lock_cache;

static char *
_eina_file_escape(char *path, size_t len)
{
   char *result;
   char *p;
   char *q;

   result = path;
   p = result;
   q = result;

   if (!result)
     return NULL;

   while ((p = strchr(p, '/')))
     {
	// remove double `/'
	if (p[1] == '/')
	  {
	     memmove(p, p + 1, --len - (p - result));
	     result[len] = '\0';
	  }
	else
	  if (p[1] == '.'
	      && p[2] == '.')
	    {
	       // remove `/../'
	       if (p[3] == '/')
		 {
		    char tmp;

		    len -= p + 3 - q;
		    memmove(q, p + 3, len - (q - result));
		    result[len] = '\0';
		    p = q;

		    /* Update q correctly. */
		    tmp = *p;
		    *p = '\0';
		    q = strrchr(result, '/');
		    if (!q) q = result;
		    *p = tmp;
		 }
	       else
		 // remove '/..$'
		 if (p[3] == '\0')
		   {
		      len -= p + 2 - q;
		      result[len] = '\0';
                      break;
		   }
		 else
		   {
		      q = p;
		      ++p;
		   }
	    }
        else
          if (p[1] == '.'
              && p[2] == '/')
            {
               // remove '/./'
               len -= 2;
               memmove(p, p + 2, len - (p - result));
               result[len] = '\0';
               q = p;
               ++p;
            }
	  else
	    {
	       q = p;
	       ++p;
	    }
     }

   return result;
}

// Global API

EAPI char *
eina_file_path_sanitize(const char *path)
{
   Eina_Tmpstr *result = NULL;
   size_t len;

   if (!path) return NULL;

   len = strlen(path);

   if (eina_file_path_relative(path))
     {
       result = eina_file_current_directory_get(path, len);
       len = eina_tmpstr_strlen(result);
     }
   else
     result = path;

   return _eina_file_escape(eina_file_cleanup(result), len);
}

EAPI Eina_File *
eina_file_dup(Eina_File *file)
{
   if (file) file->refcount++;
   return file;
}

EAPI void
eina_file_close(Eina_File *file)
{
   EINA_SAFETY_ON_NULL_RETURN(file);

   eina_lock_take(&file->lock);
   file->refcount--;
   eina_lock_release(&file->lock);
   if (file->refcount != 0) return;

   eina_lock_take(&_eina_file_lock_cache);

   eina_hash_del(_eina_file_cache, file->filename, file);
   eina_file_real_close(file);

   eina_lock_release(&_eina_file_lock_cache);
}

EAPI size_t
eina_file_size_get(Eina_File *file)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(file, 0);
   return file->length;
}

EAPI time_t
eina_file_mtime_get(Eina_File *file)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(file, 0);
   return file->mtime;
}

EAPI const char *
eina_file_filename_get(Eina_File *file)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(file, NULL);
   return file->filename;
}

/* search '\r' and '\n' by preserving cache locality and page locality
   in doing a search inside 4K boundary.
 */
static inline const char *
_eina_fine_eol(const char *start, int boundary, const char *end)
{
   const char *cr;
   const char *lf;
   unsigned long long chunk;

   while (start < end)
     {
        chunk = start + boundary < end ? boundary : end - start;
        cr = memchr(start, '\r', chunk);
        lf = memchr(start, '\n', chunk);
        if (cr)
          {
             if (lf && lf < cr)
               return lf + 1;
             return cr + 1;
          }
        else if (lf)
           return lf + 1;

        start += chunk;
        boundary = 4096;
     }

   return end;
}

static Eina_Bool
_eina_file_map_lines_iterator_next(Eina_Lines_Iterator *it, void **data)
{
   const char *eol;
   unsigned char match;

   if (it->current.end >= it->end)
     return EINA_FALSE;

   match = *it->current.end;
   while ((*it->current.end == '\n' || *it->current.end == '\r')
          && it->current.end < it->end)
     {
        if (match == *it->current.end)
          it->current.index++;
        it->current.end++;
     }
   it->current.index++;

   if (it->current.end == it->end)
     return EINA_FALSE;

   eol = _eina_fine_eol(it->current.end,
                        it->boundary,
                        it->end);
   it->boundary = (uintptr_t) eol & 0x3FF;
   if (it->boundary == 0) it->boundary = 4096;

   it->current.start = it->current.end;

   it->current.end = eol;
   it->current.length = eol - it->current.start - 1;

   *data = &it->current;
   return EINA_TRUE;
}

static Eina_File *
_eina_file_map_lines_iterator_container(Eina_Lines_Iterator *it)
{
   return it->fp;
}

static void
_eina_file_map_lines_iterator_free(Eina_Lines_Iterator *it)
{
   eina_file_map_free(it->fp, (void*) it->map);
   eina_file_close(it->fp);

   EINA_MAGIC_SET(&it->iterator, 0);
   free(it);
}

EAPI Eina_Iterator *
eina_file_map_lines(Eina_File *file)
{
   Eina_Lines_Iterator *it;

   EINA_SAFETY_ON_NULL_RETURN_VAL(file, NULL);

   if (file->length == 0) return NULL;

   it = calloc(1, sizeof (Eina_Lines_Iterator));
   if (!it) return NULL;

   EINA_MAGIC_SET(&it->iterator, EINA_MAGIC_ITERATOR);

   it->map = eina_file_map_all(file, EINA_FILE_SEQUENTIAL);
   if (!it->map)
     {
        free(it);
        return NULL;
     }

   eina_lock_take(&file->lock);
   file->refcount++;
   eina_lock_release(&file->lock);

   it->fp = file;
   it->boundary = 4096;
   it->current.start = it->map;
   it->current.end = it->current.start;
   it->current.index = 0;
   it->current.length = 0;
   it->end = it->map + it->fp->length;

   it->iterator.version = EINA_ITERATOR_VERSION;
   it->iterator.next = FUNC_ITERATOR_NEXT(_eina_file_map_lines_iterator_next);
   it->iterator.get_container = FUNC_ITERATOR_GET_CONTAINER(_eina_file_map_lines_iterator_container);
   it->iterator.free = FUNC_ITERATOR_FREE(_eina_file_map_lines_iterator_free);

   return &it->iterator;
}

static Eina_Bool
_eina_file_copy_write_internal(int fd, char *buf, size_t size)
{
   size_t done = 0;
   while (done < size)
     {
        ssize_t w = write(fd, buf + done, size - done);
        if (w >= 0)
          done += w;
        else if ((errno != EAGAIN) && (errno != EINTR))
          {
             ERR("Error writing destination file during copy: %s",
                 strerror(errno));
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static Eina_Bool
_eina_file_copy_read_internal(int fd, char *buf, off_t bufsize, ssize_t *readsize)
{
   while (1)
     {
        ssize_t r = read(fd, buf, bufsize);
        if (r == 0)
          {
             ERR("Premature end of source file during copy.");
             return EINA_FALSE;
          }
        else if (r < 0)
          {
             if ((errno != EAGAIN) && (errno != EINTR))
               {
                  ERR("Error reading source file during copy: %s",
                      strerror(errno));
                  return EINA_FALSE;
               }
          }
        else
          {
             *readsize = r;
             return EINA_TRUE;
          }
     }
}

#ifdef HAVE_SPLICE
static Eina_Bool
_eina_file_copy_write_splice_internal(int fd, int pipefd, size_t size)
{
   size_t done = 0;
   while (done < size)
     {
        ssize_t w = splice(pipefd, NULL, fd, NULL, size - done, SPLICE_F_MORE);
        if (w >= 0)
          done += w;
        else if (errno == EINVAL)
          {
             INF("Splicing is not supported for destination file");
             return EINA_FALSE;
          }
        else if ((errno != EAGAIN) && (errno != EINTR))
          {
             ERR("Error splicing to destination file during copy: %s",
                 strerror(errno));
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static Eina_Bool
_eina_file_copy_read_splice_internal(int fd, int pipefd, off_t bufsize, ssize_t *readsize)
{
   while (1)
     {
        ssize_t r = splice(fd, NULL, pipefd, NULL, bufsize, SPLICE_F_MORE);
        if (r == 0)
          {
             ERR("Premature end of source file during splice.");
             return EINA_FALSE;
          }
        else if (r < 0)
          {
             if (errno == EINVAL)
               {
                  INF("Splicing is not supported for source file");
                  return EINA_FALSE;
               }
             else if ((errno != EAGAIN) && (errno != EINTR))
               {
                  ERR("Error splicing from source file during copy: %s",
                      strerror(errno));
                  return EINA_FALSE;
               }
          }
        else
          {
             *readsize = r;
             return EINA_TRUE;
          }
     }
}
#endif

static Eina_Bool
_eina_file_copy_splice_internal(int s, int d, off_t total, Eina_File_Copy_Progress cb, const void *cb_data, Eina_Bool *splice_unsupported)
{
#ifdef HAVE_SPLICE
   off_t bufsize = COPY_BLOCKSIZE;
   off_t done;
   Eina_Bool ret;
   int pipefd[2];

   *splice_unsupported = EINA_TRUE;

   if (pipe(pipefd) < 0) return EINA_FALSE;

   done = 0;
   ret = EINA_TRUE;
   while (done < total)
     {
        size_t todo;
        ssize_t r;

        if (done + bufsize < total)
          todo = bufsize;
        else
          todo = total - done;

        ret = _eina_file_copy_read_splice_internal(s, pipefd[1], todo, &r);
        if (!ret) break;

        ret = _eina_file_copy_write_splice_internal(d, pipefd[0], r);
        if (!ret) break;

        *splice_unsupported = EINA_FALSE;
        done += r;

        if (cb)
          {
             ret = cb((void *)cb_data, done, total);
             if (!ret) break;
          }
     }

   close(pipefd[0]);
   close(pipefd[1]);

   return ret;
#endif
   *splice_unsupported = EINA_TRUE;
   return EINA_FALSE;
   (void)s;
   (void)d;
   (void)total;
   (void)cb;
   (void)cb_data;
}

static Eina_Bool
_eina_file_copy_internal(int s, int d, off_t total, Eina_File_Copy_Progress cb, const void *cb_data)
{
   void *buf = NULL;
   off_t bufsize = COPY_BLOCKSIZE;
   off_t done;
   Eina_Bool ret, splice_unsupported;

   ret = _eina_file_copy_splice_internal(s, d, total, cb, cb_data,
                                         &splice_unsupported);
   if (ret)
     return EINA_TRUE;
   else if (!splice_unsupported) /* splice works, but copy failed anyway */
     return EINA_FALSE;

   /* make sure splice didn't change the position */
   lseek(s, 0, SEEK_SET);
   lseek(d, 0, SEEK_SET);

   while ((bufsize > 0) && ((buf = malloc(bufsize)) == NULL))
     bufsize /= 128;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buf, EINA_FALSE);

   done = 0;
   ret = EINA_TRUE;
   while (done < total)
     {
        size_t todo;
        ssize_t r;

        if (done + bufsize < total)
          todo = bufsize;
        else
          todo = total - done;

        ret = _eina_file_copy_read_internal(s, buf, todo, &r);
        if (!ret) break;

        ret = _eina_file_copy_write_internal(d, buf, r);
        if (!ret) break;

        done += r;

        if (cb)
          {
             ret = cb((void *)cb_data, done, total);
             if (!ret) break;
          }
     }

   free(buf);
   return ret;
}

EAPI Eina_Bool
eina_file_copy(const char *src, const char *dst, Eina_File_Copy_Flags flags, Eina_File_Copy_Progress cb, const void *cb_data)
{
   struct stat st;
   int s, d;
   Eina_Bool success;

   EINA_SAFETY_ON_NULL_RETURN_VAL(src, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dst, EINA_FALSE);

   s = open(src, O_RDONLY);
   EINA_SAFETY_ON_TRUE_RETURN_VAL (s < 0, EINA_FALSE);

   success = (fstat(s, &st) == 0);
   EINA_SAFETY_ON_FALSE_GOTO(success, end);

   d = open(dst, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
   EINA_SAFETY_ON_TRUE_GOTO(d < 0, end);

   success = _eina_file_copy_internal(s, d, st.st_size, cb, cb_data);
   if (success)
     {
#ifdef HAVE_FCHMOD
        if (flags & EINA_FILE_COPY_PERMISSION)
          fchmod(d, st.st_mode);
#endif
        if (flags & EINA_FILE_COPY_XATTR)
          eina_xattr_fd_copy(s, d);
     }

 end:
   close(s);

   if (!success)
     unlink(dst);

   return success;
}

EAPI int
eina_file_mkstemp(const char *templatename, Eina_Tmpstr **path)
{
   char buffer[PATH_MAX];
   const char *tmpdir;
   int fd;

#ifndef HAVE_EVIL
   tmpdir = getenv("TMPDIR");
   if (!tmpdir) tmpdir = "/tmp";
#else
   tmpdir = (char *)evil_tmpdir_get();
#endif /* ! HAVE_EVIL */

   snprintf(buffer, PATH_MAX, "%s/%s", tmpdir, templatename);

   fd = mkstemp(buffer);
   if (path) *path = eina_tmpstr_add(buffer);
   if (fd < 0)
     return -1;

   return fd;
}

EAPI Eina_Bool
eina_file_mkdtemp(const char *templatename, Eina_Tmpstr **path)
{
   char buffer[PATH_MAX];
   const char *tmpdir;
   char *tmpdirname;

#ifndef HAVE_EVIL
   tmpdir = getenv("TMPDIR");
   if (!tmpdir) tmpdir = "/tmp";
#else
   tmpdir = (char *)evil_tmpdir_get();
#endif /* ! HAVE_EVIL */

   snprintf(buffer, PATH_MAX, "%s/%s", tmpdir, templatename);

   tmpdirname = mkdtemp(buffer);
   if (path) *path = eina_tmpstr_add(buffer);
   if (tmpdirname == NULL)
     return EINA_FALSE;

   return EINA_TRUE;
}
