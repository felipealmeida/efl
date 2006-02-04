#include <unistd.h>
#include <fcntl.h>

#include "Emotion.h"
#include "emotion_private.h"
#include "emotion_gstreamer.h"



/* Callbacks to handle errors and EOS */
static void cb_end_of_stream (GstElement *thread,
			      gpointer    data);

static gboolean cb_idle_eos (gpointer data);

static void cb_thread_error (GstElement *thread,
			     GstElement *source,
			     GError     *error,
			     gchar      *debug,
			     gpointer    data);

/* Callbacks to display the frame content */

static void cb_handoff (GstElement *fakesrc,
			GstBuffer  *buffer,
			GstPad     *pad,
			gpointer    user_data);
static void new_decoded_pad_cb (GstElement *decodebin,
                                GstPad     *new_pad,
                                gboolean    last,
                                gpointer    user_data);

static int   _em_fd_ev_active(void *data, Ecore_Fd_Handler *fdh);

/* Interface */

static unsigned char em_init(Evas_Object *obj,
			     void       **emotion_video);
static int em_shutdown(void *video);
static unsigned char em_file_open(const char  *file,
				  Evas_Object *obj,
				  void        *video);
static void em_file_close(void *video);
static void em_play(void *video,
		    double pos);
static void em_stop(void *video);
static void em_size_get(void *video,
			int  *width,
			int  *height);
static void em_pos_set(void  *video,
		       double pos);
static double em_len_get(void *video);
static int em_fps_num_get(void *video);
static int em_fps_den_get(void *video);
static double em_fps_get(void *video);
static double em_pos_get(void *video);
static double em_ratio_get(void *video);

static int em_video_handled(void *video);
static int em_audio_handled(void *video);

static int em_seekable(void *video);
static void em_frame_done(void *video);
static Emotion_Format em_format_get(void *video);
static void em_video_data_size_get(void *video, int *w, int *h);
static int em_yuv_rows_get(void *video, int w, int h, unsigned char **yrows, unsigned char **urows, unsigned char **vrows);
static int em_bgra_data_get(void *video, unsigned char **bgra_data);
static void em_event_feed(void *video, int event);
static void em_event_mouse_button_feed(void *video, int button, int x, int y);
static void em_event_mouse_move_feed(void *video, int x, int y);

static int em_video_channel_count(void *video);
static void em_video_channel_set(void *video,
				 int   channel);
static int em_video_channel_get(void *video);
static const char *em_video_channel_name_get(void *video,
					     int   channel);
static void em_video_channel_mute_set(void *video,
				      int   mute);
static int em_video_channel_mute_get(void *video);

static int em_audio_channel_count(void *video);
static void em_audio_channel_set(void *video,
				 int   channel);
static int em_audio_channel_get(void *video);
static const char *em_audio_channel_name_get(void *video,
					     int   channel);
static void em_audio_channel_mute_set(void *video,
				      int   mute);
static int em_audio_channel_mute_get(void *video);
static void em_audio_channel_volume_set(void  *video,
					double vol);
static double em_audio_channel_volume_get(void *video);

static int em_spu_channel_count(void *video);
static void em_spu_channel_set(void *video, int channel);
static int em_spu_channel_get(void *video);
static const char *em_spu_channel_name_get(void *video, int channel);
static void em_spu_channel_mute_set(void *video, int mute);
static int em_spu_channel_mute_get(void *video);

static int em_chapter_count(void *video);
static void em_chapter_set(void *video, int chapter);
static int em_chapter_get(void *video);
static const char *em_chapter_name_get(void *video, int chapter);
static void em_speed_set(void *video, double speed);
static double em_speed_get(void *video);
static int em_eject(void *video);
static const char *em_meta_get(void *video, int meta);

/* Module interface */

static Emotion_Video_Module em_module =
{
   em_init, /* init */
   em_shutdown, /* shutdown */
   em_file_open, /* file_open */
   em_file_close, /* file_close */
   em_play, /* play */
   em_stop, /* stop */
   em_size_get, /* size_get */
   em_pos_set, /* pos_set */
   em_len_get, /* len_get */
   em_fps_num_get, /* fps_num_get */
   em_fps_den_get, /* fps_den_get */
   em_fps_get, /* fps_get */
   em_pos_get, /* pos_get */
   em_ratio_get, /* ratio_get */
   em_video_handled, /* video_handled */
   em_audio_handled, /* audio_handled */
   em_seekable, /* seekable */
   em_frame_done, /* frame_done */
   em_format_get, /* format_get */
   em_video_data_size_get, /* video_data_size_get */
   em_yuv_rows_get, /* yuv_rows_get */
   em_bgra_data_get, /* bgra_data_get */
   em_event_feed, /* event_feed */
   em_event_mouse_button_feed, /* event_mouse_button_feed */
   em_event_mouse_move_feed, /* event_mouse_move_feed */
   em_video_channel_count, /* video_channel_count */
   em_video_channel_set, /* video_channel_set */
   em_video_channel_get, /* video_channel_get */
   em_video_channel_name_get, /* video_channel_name_get */
   em_video_channel_mute_set, /* video_channel_mute_set */
   em_video_channel_mute_get, /* video_channel_mute_get */
   em_audio_channel_count, /* audio_channel_count */
   em_audio_channel_set, /* audio_channel_set */
   em_audio_channel_get, /* audio_channel_get */
   em_audio_channel_name_get, /* audio_channel_name_get */
   em_audio_channel_mute_set, /* audio_channel_mute_set */
   em_audio_channel_mute_get, /* audio_channel_mute_get */
   em_audio_channel_volume_set, /* audio_channel_volume_set */
   em_audio_channel_volume_get, /* audio_channel_volume_get */
   em_spu_channel_count, /* spu_channel_count */
   em_spu_channel_set, /* spu_channel_set */
   em_spu_channel_get, /* spu_channel_get */
   em_spu_channel_name_get, /* spu_channel_name_get */
   em_spu_channel_mute_set, /* spu_channel_mute_set */
   em_spu_channel_mute_get, /* spu_channel_mute_get */
   em_chapter_count, /* chapter_count */
   em_chapter_set, /* chapter_set */
   em_chapter_get, /* chapter_get */
   em_chapter_name_get, /* chapter_name_get */
   em_speed_set, /* speed_set */
   em_speed_get, /* speed_get */
   em_eject, /* eject */
   em_meta_get /* meta_get */
};

static unsigned char
em_init(Evas_Object  *obj,
	void        **emotion_video)
{
   Emotion_Gstreamer_Video *ev;
   GstElement              *filesrc;
   GstElement              *decodebin;
   int                      fds[2];

   if (!emotion_video)
      return 0;

   printf ("Init gstreamer...\n");

   ev = calloc(1, sizeof(Emotion_Gstreamer_Video));
   if (!ev) return 0;
   ev->obj = obj;
   ev->obj_data = NULL;

   /* Initialization of gstreamer */
   gst_init (NULL, NULL);

   /* Gstreamer pipeline */
   ev->pipeline = gst_pipeline_new ("pipeline");
/*    g_signal_connect (ev->pipeline, "eos", G_CALLBACK (cb_end_of_stream), ev); */
/*    g_signal_connect (ev->pipeline, "error", G_CALLBACK (cb_thread_error), ev); */

   filesrc = gst_element_factory_make ("filesrc", "filesrc");
   if (!filesrc)
      gst_object_unref (GST_OBJECT (ev->pipeline));

   decodebin = gst_element_factory_make ("decodebin", "decodebin");
   if (!decodebin)
      gst_object_unref (GST_OBJECT (ev->pipeline));
  g_signal_connect (decodebin, "new-decoded-pad",
                    G_CALLBACK (new_decoded_pad_cb), ev);

   gst_bin_add_many (GST_BIN (ev->pipeline), filesrc, decodebin, NULL);
   gst_element_link (filesrc, decodebin);

   /* We allocate the sinks lists */
   ev->video_sinks = ecore_list_new ();
   ecore_list_set_free_cb(ev->video_sinks, ECORE_FREE_CB(free));
   ev->audio_sinks = ecore_list_new ();
   ecore_list_set_free_cb(ev->audio_sinks, ECORE_FREE_CB(free));

   *emotion_video = ev;

   /* Default values */
   ev->ratio = 1.0;

   /* Create the file descriptors */
   if (pipe(fds) == 0)
     {
	ev->fd_ev_read = fds[0];
	ev->fd_ev_write = fds[1];
	fcntl(ev->fd_ev_read, F_SETFL, O_NONBLOCK);
	ev->fd_ev_handler = ecore_main_fd_handler_add(ev->fd_ev_read,
						      ECORE_FD_READ,
                                                      _em_fd_ev_active,
                                                      ev,
                                                      NULL, NULL);
	ecore_main_fd_handler_active_set(ev->fd_ev_handler, ECORE_FD_READ);
     }

   return 1;
}

static int
em_shutdown(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   gst_element_set_state (ev->pipeline, GST_STATE_NULL);
   gst_object_unref (GST_OBJECT (ev->pipeline));

   ecore_list_destroy (ev->video_sinks);
   ecore_list_destroy (ev->audio_sinks);

   /* FIXME: and the evas object ? */

   ecore_main_fd_handler_del(ev->fd_ev_handler);
   close(ev->fd_ev_write);
   close(ev->fd_ev_read);

   free(ev);

   return 1;
}

static unsigned char
em_file_open(const char   *file,
	     Evas_Object  *obj,
	     void         *video)
{
   Emotion_Gstreamer_Video *ev;
   GstElement              *filesrc;
   GstStateChangeReturn     res;

   ev = (Emotion_Gstreamer_Video *)video;
   printf ("Open file gstreamer...\n");

   /* Evas Object */
   ev->obj = obj;

   /* Gstreamer elements */
   filesrc = gst_bin_get_by_name (GST_BIN (ev->pipeline), "filesrc");
   g_object_set (G_OBJECT (filesrc), "location", file, NULL);

   res = gst_element_set_state (ev->pipeline, GST_STATE_PAUSED);
   if (res == GST_STATE_CHANGE_FAILURE) {
      g_print ("Emotion-Gstreamer ERROR: could not pause\n");
      gst_object_unref (GST_OBJECT (ev->pipeline));
      return 0;
   }
   res = gst_element_get_state (ev->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
   if (res != GST_STATE_CHANGE_SUCCESS) {
      g_print ("Emotion-Gstreamer ERROR: could not complete pause\n");
      gst_object_unref (GST_OBJECT (ev->pipeline));
      return 0;
   }

   /* We get the informations of streams */
   ecore_list_goto_first (ev->video_sinks);
   ecore_list_goto_first (ev->audio_sinks);
   {
     GstElement  *decodebin;
     GstIterator *it;
     gpointer     data;

     decodebin = gst_bin_get_by_name (GST_BIN (ev->pipeline), "decodebin");
     it = gst_element_iterate_src_pads (decodebin);
     while (gst_iterator_next (it, &data) == GST_ITERATOR_OK) {
        GstPad  *pad;
        GstCaps *caps;
        gchar   *str;

        pad = GST_PAD (data);

        caps = gst_pad_get_caps (pad);
        str = gst_caps_to_string (caps);
        g_print ("%s\n", str);
        /* video stream */
        if (g_str_has_prefix (str, "video/")) {
           Emotion_Video_Sink *vsink;
           GstStructure       *structure;
           const GValue       *val;
           GstQuery           *query;

           vsink = (Emotion_Video_Sink *)ecore_list_next (ev->video_sinks);
           structure = gst_caps_get_structure (GST_CAPS (caps), 0);

           gst_structure_get_int (structure, "width", &vsink->width);
           gst_structure_get_int (structure, "height", &vsink->height);

           val = gst_structure_get_value (structure, "framerate");
           if (val) {
              vsink->fps_num = gst_value_get_fraction_numerator (val);
              vsink->fps_den = gst_value_get_fraction_denominator (val);
           }
           if (g_str_has_prefix(str, "video/x-raw-yuv")) {
             val = gst_structure_get_value (structure, "format");
             vsink->fourcc = gst_value_get_fourcc (val);
           }
           else if (g_str_has_prefix(str, "video/x-raw-rgb")) {
             vsink->fourcc = GST_MAKE_FOURCC ('A','R','G','B');
           }
           else
             vsink->fourcc = 0;

           printf ("  size      : %dx%d\n", vsink->width, vsink->height);
           printf ("  framerate : %d/%d\n", vsink->fps_num, vsink->fps_den);
           printf ("  fourcc    : %" GST_FOURCC_FORMAT "\n", GST_FOURCC_ARGS (vsink->fourcc));

           query = gst_query_new_duration (GST_FORMAT_TIME);
           if (gst_pad_query (pad, query)) {
              gint64 time;

              gst_query_parse_duration (query, NULL, &time);
              g_print ("  duration  : %" GST_TIME_FORMAT "\n\n", GST_TIME_ARGS (time));
              vsink->length_time = (double)time / (double)GST_SECOND;
           }
           gst_query_unref (query);
        }
        /* audio stream */
        else if (g_str_has_prefix (str, "audio/")) {
           Emotion_Audio_Sink *asink;
           GstStructure       *structure;
           GstQuery           *query;

           asink = (Emotion_Audio_Sink *)ecore_list_next (ev->audio_sinks);

           structure = gst_caps_get_structure (GST_CAPS (caps), 0);

           gst_structure_get_int (structure, "channels", &asink->channels);
           gst_structure_get_int (structure, "rate", &asink->samplerate);

           query = gst_query_new_duration (GST_FORMAT_TIME);
           if (gst_pad_query (pad, query)) {
              gint64 time;

              gst_query_parse_duration (query, NULL, &time);
              g_print ("  duration  : %" GST_TIME_FORMAT "\n\n", GST_TIME_ARGS (time));
              asink->length_time = (double)time / (double)GST_SECOND;
           }
           gst_query_unref (query);
        }

        g_free (str);
        gst_object_unref (pad);
     }
     gst_iterator_free (it);
   }

   ev->position = 0.0;

   return 1;
}

static void
em_file_close(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
   if (!ev)
     return;

   printf("EX pause end...\n");
   if (!emotion_object_play_get(ev->obj))
     {
	printf("  ... unpause\n");
        gst_element_set_state (ev->pipeline, GST_STATE_PAUSED);
     }
   printf("EX stop\n");
   if (ev->pipeline)
     gst_element_set_state (ev->pipeline, GST_STATE_READY);

   /* FIXME: delete the pipeline completely ? */
}

static void
em_play(void   *video,
	double  pos)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
   gst_element_set_state (ev->pipeline, GST_STATE_PLAYING);
   ev->play = 1;
}

static void
em_stop(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   gst_element_set_state (ev->pipeline, GST_STATE_PAUSED);
   ev->play = 0;
}

static void
em_size_get(void  *video,
	    int   *width,
	    int   *height)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink) {
      if (width) *width = vsink->width;
      if (height) *height = vsink->height;
   }
   else {
      if (width) *width = 0;
      if (height) *height = 0;
   }
}

static void
em_pos_set(void   *video,
	   double  pos)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;
   Emotion_Audio_Sink      *asink;

   ev = (Emotion_Gstreamer_Video *)video;

   if (ev->seek_to_pos == pos) return;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   asink = (Emotion_Audio_Sink *)ecore_list_goto_index (ev->video_sinks, ev->audio_sink_nbr);

   if (vsink) {
      gst_element_seek(vsink->sink, 1.0,
                       GST_FORMAT_TIME,
                       GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH,
                       GST_SEEK_TYPE_CUR,
                       (gint64)(pos * (double)GST_SECOND),
                       GST_SEEK_TYPE_NONE,
                       -1);
   }
   if (asink) {
      gst_element_seek(asink->sink, 1.0,
                       GST_FORMAT_TIME,
                       GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH,
                       GST_SEEK_TYPE_CUR,
                       (gint64)(pos * (double)GST_SECOND),
                       GST_SEEK_TYPE_NONE,
                       -1);
   }
   ev->seek_to_pos = pos;
}

static double
em_len_get(void *video)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink)
      return (double)vsink->length_time;

   return 0.0;
}

static int
em_fps_num_get(void *video)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink)
      return vsink->fps_num;

   return 0;
}

static int
em_fps_den_get(void *video)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink)
      return vsink->fps_den;

   return 1;
}

static double
em_fps_get(void *video)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink)
      return (double)vsink->fps_num / (double)vsink->fps_den;

   return 0.0;
}

static double
em_pos_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ev->position;
}

static double
em_ratio_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ev->ratio;
}

static int
em_video_handled(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   if (ecore_list_is_empty (ev->video_sinks))
     return 0;

   return 1;
}

static int
em_audio_handled(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   if (ecore_list_is_empty (ev->audio_sinks))
     return 0;

   return 1;
}

static int
em_seekable(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 1;
}

static void
em_frame_done(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static Emotion_Format
em_format_get (void *video)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink) {
      switch (vsink->fourcc) {
      case GST_MAKE_FOURCC ('I','4','2','0'):
         return EMOTION_FORMAT_I420;
      case GST_MAKE_FOURCC ('Y','V','1','2'):
         return EMOTION_FORMAT_YV12;
      case GST_MAKE_FOURCC ('Y','U','Y','2'):
         return EMOTION_FORMAT_YUY2;
      case GST_MAKE_FOURCC ('A','R','G','B'):
         return EMOTION_FORMAT_BGRA;
      default:
         return EMOTION_FORMAT_NONE;
      }
   }
   return EMOTION_FORMAT_NONE;
}

static void
em_video_data_size_get(void *video, int *w, int *h)
{
   Emotion_Gstreamer_Video *ev;
   Emotion_Video_Sink      *vsink;

   ev = (Emotion_Gstreamer_Video *)video;

   vsink = (Emotion_Video_Sink *)ecore_list_goto_index (ev->video_sinks, ev->video_sink_nbr);
   if (vsink) {
      *w = vsink->width;
      *h = vsink->height;
   }
   else {
      *w = 0;
      *h = 0;
   }
}

static int
em_yuv_rows_get(void           *video,
		int             w,
		int             h,
		unsigned char **yrows,
		unsigned char **urows,
		unsigned char **vrows)
{
   Emotion_Gstreamer_Video *ev;
   int                      i;

   ev = (Emotion_Gstreamer_Video *)video;

   if (ev->obj_data)
     {
       if (em_format_get(video) == EMOTION_FORMAT_I420) {
         for (i = 0; i < h; i++)
           yrows[i] = &ev->obj_data[i * w];

         for (i = 0; i < (h / 2); i++)
           urows[i] = &ev->obj_data[h * w + i * (w / 2) ];

         for (i = 0; i < (h / 2); i++)
           vrows[i] = &ev->obj_data[h * w + h * (w /4) + i * (w / 2)];
       }
       else if (em_format_get(video) == EMOTION_FORMAT_YV12) {
         for (i = 0; i < h; i++)
           yrows[i] = &ev->obj_data[i * w];

         for (i = 0; i < (h / 2); i++)
           vrows[i] = &ev->obj_data[h * w + i * (w / 2) ];

         for (i = 0; i < (h / 2); i++)
           urows[i] = &ev->obj_data[h * w + h * (w /4) + i * (w / 2)];
       }
       else
         return 0;

       return 1;
     }

   return 0;
}

static int
em_bgra_data_get(void *video, unsigned char **bgra_data)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   if (ev->obj_data && em_format_get(video) == EMOTION_FORMAT_BGRA) {
      *bgra_data = ev->obj_data;
      return 1;
   }
   return 0;
}

static void
em_event_feed(void *video, int event)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static void
em_event_mouse_button_feed(void *video, int button, int x, int y)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static void
em_event_mouse_move_feed(void *video, int x, int y)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

/* Video channels */
static int
em_video_channel_count(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ecore_list_nodes(ev->video_sinks);
}

static void
em_video_channel_set(void *video,
		     int   channel)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   if (channel < 0) channel = 0;
   /* FIXME: a faire... */
}

static int
em_video_channel_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ev->video_sink_nbr;
}

static const char *
em_video_channel_name_get(void *video,
			  int   channel)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return NULL;
}

static void
em_video_channel_mute_set(void *video,
			  int   mute)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
   ev->video_mute = mute;
}

static int
em_video_channel_mute_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ev->video_mute;
}

/* Audio channels */

static int
em_audio_channel_count(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ecore_list_nodes(ev->audio_sinks);
}

static void
em_audio_channel_set(void *video,
		     int   channel)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   if (channel < -1) channel = -1;
   /* FIXME: a faire... */
}

static int
em_audio_channel_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ev->audio_sink_nbr;
}

static const char *
em_audio_channel_name_get(void *video,
			  int   channel)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return NULL;
}

static void
em_audio_channel_mute_set(void *video,
			  int   mute)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   ev->audio_mute = mute;
   /* FIXME: a faire ... */
}

static int
em_audio_channel_mute_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return ev->audio_mute;
}

static void
em_audio_channel_volume_set(void  *video,
			    double vol)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   if (vol < 0.0)
     vol = 0.0;
   if (vol > 100.0)
     vol = 100.0;
   g_object_set (G_OBJECT (ev->pipeline), "volume",
		 vol / 100.0, NULL);
}

static double
em_audio_channel_volume_get(void *video)
{
   Emotion_Gstreamer_Video *ev;
   double                   vol;

   ev = (Emotion_Gstreamer_Video *)video;

   g_object_get (G_OBJECT (ev->pipeline), "volume", &vol, NULL);

   return vol*100.0;
}

/* spu stuff */

static int
em_spu_channel_count(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 0;
}

static void
em_spu_channel_set(void *video, int channel)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static int
em_spu_channel_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 1;
}

static const char *
em_spu_channel_name_get(void *video, int channel)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
   return NULL;
}

static void
em_spu_channel_mute_set(void *video, int mute)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static int
em_spu_channel_mute_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 0;
}

static int
em_chapter_count(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
   return 0;
}

static void
em_chapter_set(void *video, int chapter)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static int
em_chapter_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 0;
}

static const char *
em_chapter_name_get(void *video, int chapter)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return NULL;
}

static void
em_speed_set(void *video, double speed)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
}

static double
em_speed_get(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 1.0;
}

static int
em_eject(void *video)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;

   return 1;
}

static const char *
em_meta_get(void *video, int meta)
{
   Emotion_Gstreamer_Video *ev;

   ev = (Emotion_Gstreamer_Video *)video;
   return NULL;
}

unsigned char
module_open(Evas_Object           *obj,
	    Emotion_Video_Module **module,
	    void                 **video)
{
   if (!module)
      return 0;

   if (!em_module.init(obj, video))
      return 0;

   *module = &em_module;
   return 1;
}

void
module_close(Emotion_Video_Module *module,
	     void                 *video)
{
   em_module.shutdown(video);
}


/*
 * Callbacks
 */

static void
cb_end_of_stream (GstElement *thread,
		  gpointer    data)
{
  Emotion_Gstreamer_Video* ev;

  ev = (Emotion_Gstreamer_Video *)data;
  printf ("Have eos in thread %p\n", g_thread_self ());
  g_idle_add ((GSourceFunc) cb_idle_eos, data);
}

static gboolean
cb_idle_eos (gpointer data)
{
  Emotion_Gstreamer_Video* ev;

  ev = (Emotion_Gstreamer_Video *)data;

  printf ("Have idle-func in thread %p\n", g_thread_self ());

  _emotion_playback_finished(ev->obj);

  return 0;
}

static void
cb_thread_error (GstElement *thread,
		 GstElement *source,
		 GError     *error,
		 gchar      *debug,
		 gpointer    data)
{
  printf ("Error in thread %p: %s\n", g_thread_self (), error->message);
  g_idle_add ((GSourceFunc) cb_idle_eos, NULL);
}

/* Send the video frame to the evas object */
static void
cb_handoff (GstElement *fakesrc,
	    GstBuffer  *buffer,
	    GstPad     *pad,
	    gpointer    user_data)
{
   GstQuery *query;
   void *buf[2];

   Emotion_Gstreamer_Video *ev = ( Emotion_Gstreamer_Video *) user_data;
   if (!ev)
     return;

  if (!ev->obj_data)
    ev->obj_data = (void*) malloc (GST_BUFFER_SIZE(buffer) * sizeof(void));

  memcpy ( ev->obj_data, GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
  buf[0] = GST_BUFFER_DATA(buffer);
  buf[1] = buffer;
  write(ev->fd_ev_write, buf, sizeof(buf));

  query = gst_query_new_position (GST_FORMAT_TIME);
  if (gst_pad_query (gst_pad_get_peer (pad), query)) {
     gint64 position;

     gst_query_parse_position (query, NULL, &position);
     ev->position = (double)position / (double)GST_SECOND;
  }
  gst_query_unref (query);
}

static void
new_decoded_pad_cb (GstElement *decodebin,
                    GstPad     *new_pad,
                    gboolean    last,
                    gpointer    user_data)
{
   Emotion_Gstreamer_Video *ev;
   GstCaps *caps;
   gchar   *str;

   ev = (Emotion_Gstreamer_Video *)user_data;
   caps = gst_pad_get_caps (new_pad);
   str = gst_caps_to_string (caps);

   /* video stream */
   if (g_str_has_prefix (str, "video/")) {
      Emotion_Video_Sink *vsink;
      GstPad             *videopad;

      vsink = (Emotion_Video_Sink *)malloc (sizeof (Emotion_Video_Sink));
      if (!vsink) return;
      if (!ecore_list_append (ev->video_sinks, vsink)) {
         free(vsink);
         return;
      }

      vsink->sink = gst_element_factory_make ("fakesink", NULL);
      gst_bin_add (GST_BIN (ev->pipeline), vsink->sink);
      videopad = gst_element_get_pad (vsink->sink, "sink");
      gst_pad_link(new_pad, videopad);
      if (ecore_list_nodes(ev->video_sinks) == 1) {
         ev->ratio = (double)vsink->width / (double)vsink->height;
         g_object_set (G_OBJECT (vsink->sink), "sync", TRUE, NULL);
         g_object_set (G_OBJECT (vsink->sink), "signal-handoffs", TRUE, NULL);
         g_signal_connect (G_OBJECT (vsink->sink),
                           "handoff",
                           G_CALLBACK (cb_handoff), ev);
      }
      gst_element_set_state (vsink->sink, GST_STATE_PAUSED);
   }
   /* audio stream */
   else if (g_str_has_prefix (str, "audio/")) {
     Emotion_Audio_Sink *asink;
     GstElement         *audioqueue;
     GstElement         *conv;
     GstElement         *resample;
     GstPad             *audiopad;

     asink = (Emotion_Audio_Sink *)malloc (sizeof (Emotion_Audio_Sink));
     if (!asink) return;
     if (!ecore_list_append (ev->audio_sinks, asink)) {
       free(asink);
       return;
     }

     g_print ("node # %d\n", ecore_list_nodes(ev->audio_sinks));
     audioqueue = gst_element_factory_make ("queue", NULL);
     conv = gst_element_factory_make ("audioconvert", NULL);
     resample = gst_element_factory_make ("audioresample", NULL);
     if (ecore_list_nodes(ev->audio_sinks) == 1)
       asink->sink = gst_element_factory_make ("alsasink", NULL);
     else
       asink->sink = gst_element_factory_make ("fakesink", NULL);
     gst_bin_add_many (GST_BIN (ev->pipeline),
                       audioqueue, conv, resample, asink->sink, NULL);
     gst_element_link_many (audioqueue, conv, resample, asink->sink, NULL);
     audiopad = gst_element_get_pad (audioqueue, "sink");
     gst_pad_link(new_pad, audiopad);
     gst_element_set_state (audioqueue, GST_STATE_PAUSED);
     gst_element_set_state (conv, GST_STATE_PAUSED);
     gst_element_set_state (resample, GST_STATE_PAUSED);
     gst_element_set_state (asink->sink, GST_STATE_PAUSED);
   }
}

static int
_em_fd_ev_active(void *data, Ecore_Fd_Handler *fdh)
{
  int fd;
  int len;
  void *buf[1];
  unsigned char *frame_data;
  Emotion_Gstreamer_Video *ev;
  GstBuffer  *buffer;

  ev = data;
  fd = ecore_main_fd_handler_fd_get(fdh);

  while ((len = read(fd, buf, sizeof(buf))) > 0)
    {
      if (len == sizeof(buf))
        {
          frame_data = buf[0];
          buffer = buf[1];
          _emotion_frame_new(ev->obj);
          len = ((Emotion_Video_Sink *)ecore_list_goto_first(ev->video_sinks))->length_time;
          _emotion_video_pos_update(ev->obj, ev->position, len);

        }
    }
   return 1;
}
