#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <Eina.h>

#include "Eo.h"
#include "eo_ptr_indirection.h"
#include "eo_private.h"

/* The last id that should be reserved for statically allocated classes. */
#define EO_CLASS_IDS_FIRST 1
#define EO_OP_IDS_FIRST 1

/* Used inside the class_get functions of classes, see #EO_DEFINE_CLASS */
EAPI Eina_Lock _eo_class_creation_lock;
int _eo_log_dom = -1;

static _Eo_Class **_eo_classes;
static Eo_Class_Id _eo_classes_last_id;
static Eina_Bool _eo_init_count = 0;
static Eo_Op _eo_ops_last_id = 0;

static size_t _eo_sz = 0;
static size_t _eo_class_sz = 0;

static void _eo_condtor_reset(_Eo *obj);
static inline void *_eo_data_scope_get(const _Eo *obj, const _Eo_Class *klass);
static inline void *_eo_data_xref_internal(const char *file, int line, _Eo *obj, const _Eo_Class *klass, const _Eo *ref_obj);
static inline void _eo_data_xunref_internal(_Eo *obj, void *data, const _Eo *ref_obj);
static const _Eo_Class *_eo_op_class_get(Eo_Op op);
static const Eo_Op_Description *_eo_op_id_desc_get(Eo_Op op);

/* Start of Dich */

/* How we search and store the implementations in classes. */
#define DICH_CHAIN_LAST_BITS 5
#define DICH_CHAIN_LAST_SIZE (1 << DICH_CHAIN_LAST_BITS)
#define DICH_CHAIN1(x) ((x) / DICH_CHAIN_LAST_SIZE)
#define DICH_CHAIN_LAST(x) ((x) % DICH_CHAIN_LAST_SIZE)

#define OP_CLASS_OFFSET_GET(x) (((x) >> EO_OP_CLASS_OFFSET) & 0xffff)

#define ID_CLASS_GET(id) ({ \
      (_Eo_Class *) (((id <= _eo_classes_last_id) && (id > 0)) ? \
      (_eo_classes[id - 1]) : NULL); \
      })

#define EO_ALIGN_SIZE(size) eina_mempool_alignof(size)

static inline void
_dich_chain_alloc(Dich_Chain1 *chain1)
{
   if (!chain1->funcs)
     {
        chain1->funcs = calloc(DICH_CHAIN_LAST_SIZE, sizeof(*(chain1->funcs)));
     }
}

static inline void
_dich_copy_all(_Eo_Class *dst, const _Eo_Class *src)
{
   Eo_Op i;
   const Dich_Chain1 *sc1 = src->chain;
   Dich_Chain1 *dc1 = dst->chain;
   for (i = 0 ; i < src->chain_size ; i++, sc1++, dc1++)
     {
        if (sc1->funcs)
          {
             size_t j;

             _dich_chain_alloc(dc1);

             const op_type_funcs *sf = sc1->funcs;
             op_type_funcs *df = dc1->funcs;
             for (j = 0 ; j < DICH_CHAIN_LAST_SIZE ; j++, df++, sf++)
               {
                  if (sf->func)
                    {
                       memcpy(df, sf, sizeof(*df));
                    }
               }
          }
     }
}

static inline const op_type_funcs *
_dich_func_get(const _Eo_Class *klass, Eo_Op op)
{
   size_t idx1 = DICH_CHAIN1(op);
   if (EINA_UNLIKELY(idx1 >= klass->chain_size))
      return NULL;
   Dich_Chain1 *chain1 = &klass->chain[idx1];
   if (EINA_UNLIKELY(!chain1->funcs))
      return NULL;
   return &chain1->funcs[DICH_CHAIN_LAST(op)];
}

static inline void
_dich_func_set(_Eo_Class *klass, Eo_Op op, eo_op_func_type func)
{
   size_t idx1 = DICH_CHAIN1(op);
   Dich_Chain1 *chain1 = &klass->chain[idx1];
   _dich_chain_alloc(chain1);
   if (chain1->funcs[DICH_CHAIN_LAST(op)].src == klass)
     {
        const _Eo_Class *op_kls = _eo_op_class_get(op);
        const Eo_Op_Description *op_desc = _eo_op_id_desc_get(op);
        ERR("Already set function for op 0x%x (%s:%s). Overriding with func %p",
              op, op_kls->desc->name, op_desc->name, func);
     }

   chain1->funcs[DICH_CHAIN_LAST(op)].func = func;
   chain1->funcs[DICH_CHAIN_LAST(op)].src = klass;
}

static inline void
_dich_func_clean_all(_Eo_Class *klass)
{
   size_t i;
   Dich_Chain1 *chain1 = klass->chain;

   for (i = 0 ; i < klass->chain_size ; i++, chain1++)
     {
        if (chain1->funcs)
           free(chain1->funcs);
     }
   free(klass->chain);
   klass->chain = NULL;
}

/* END OF DICH */

static const Eo_Op_Description noop_desc =
        EO_OP_DESCRIPTION(EO_NOOP, "No operation.");

static inline _Eo_Class *
_eo_class_pointer_get(const Eo_Class *klass_id)
{
#ifdef HAVE_EO_ID
   return ID_CLASS_GET((Eo_Class_Id)klass_id);
#else
   return (_Eo_Class *)klass_id;
#endif
}

static inline
Eo_Class * _eo_class_id_get(const _Eo_Class *klass)
{
#ifdef HAVE_EO_ID
   return (Eo_Class *)klass->class_id;
#else
   return (Eo_Class *)klass;
#endif
}

static const _Eo_Class *
_eo_op_class_get(Eo_Op op)
{
   /* FIXME: Make it fast. */
   _Eo_Class **itr = _eo_classes;
   int mid, max, min;

   min = 0;
   max = _eo_classes_last_id - 1;
   while (min <= max)
     {
        mid = (min + max) / 2;

        if (itr[mid]->base_id + itr[mid]->desc->ops.count < op)
           min = mid + 1;
        else if (itr[mid]->base_id  > op)
           max = mid - 1;
        else
           return itr[mid];
     }

   return NULL;
}

static const Eo_Op_Description *
_eo_op_id_desc_get(Eo_Op op)
{
   const _Eo_Class *klass;

   if (op == EO_NOOP)
      return &noop_desc;

   klass = _eo_op_class_get(op);

   if (klass)
     {
        Eo_Op sub_id = op - klass->base_id;
       if (sub_id < klass->desc->ops.count)
          return klass->desc->ops.descs + sub_id;
     }

   return NULL;
}

static const char *
_eo_op_id_name_get(Eo_Op op)
{
   const Eo_Op_Description *desc = _eo_op_id_desc_get(op);
   return (desc) ? desc->name : NULL;
}

static inline const _Eo_Class *
_eo_kls_itr_next(const _Eo_Class *orig_kls, const _Eo_Class *cur_klass, Eo_Op op)
{
   const _Eo_Class **kls_itr = NULL;

   /* Find the kls itr. */
   kls_itr = orig_kls->mro;
   while (*kls_itr && (*kls_itr != cur_klass))
      kls_itr++;

   if (*kls_itr)
     {
        kls_itr++;
        while (*kls_itr)
          {
             const op_type_funcs *fsrc = _dich_func_get(*kls_itr, op);
             if (!fsrc || !fsrc->func)
               {
                  kls_itr++;
                  continue;
               }
             return fsrc->src;
          }
     }

   return NULL;
}

static inline const op_type_funcs *
_eo_kls_itr_func_get(const _Eo_Class *cur_klass, Eo_Op op)
{
   const _Eo_Class *klass = cur_klass;
   if (klass)
     {
        const op_type_funcs *func = _dich_func_get(klass, op);

        if (func && func->func)
          {
             return func;
          }
     }

   return NULL;
}

/************************************ EO2 ************************************/

static inline const _Eo_Class *
_eo2_kls_itr_next(const _Eo_Class *orig_kls, const _Eo_Class *cur_klass)
{
   const _Eo_Class **kls_itr = NULL;

   /* Find the kls itr. */
   kls_itr = orig_kls->mro;
   while (*kls_itr && (*kls_itr != cur_klass))
      kls_itr++;

   if (*kls_itr)
        return *(++kls_itr);

   return NULL;
}

// FIXME: per thread stack, grow/shrink
#define EO2_INVALID_DATA (void *) -1
#define EO2_CALL_STACK_SIZE 5
typedef struct _Eo2_Stack_Frame
{
    Eo               *obj_id;
   _Eo               *obj;
   const _Eo_Class   *cur_klass;
   void              *obj_data;

} Eo2_Stack_Frame;

typedef struct _Eo2_Call_Stack {
     Eo2_Stack_Frame stack[EO2_CALL_STACK_SIZE];
     Eo2_Stack_Frame *frame_ptr;
} Eo2_Call_Stack;

static Eo2_Call_Stack eo2_call_stack = {
       {
            { NULL, NULL, NULL, EO2_INVALID_DATA },
            { NULL, NULL, NULL, EO2_INVALID_DATA },
            { NULL, NULL, NULL, EO2_INVALID_DATA },
            { NULL, NULL, NULL, EO2_INVALID_DATA },
            { NULL, NULL, NULL, EO2_INVALID_DATA },
       },
       NULL };

EAPI Eina_Bool
eo2_do_start(Eo *obj_id, Eina_Bool do_super)
{
   _Eo * obj;
   const _Eo_Class *cur_klass;
   Eo2_Stack_Frame *fptr;

   fptr = eo2_call_stack.frame_ptr;
   if ((fptr != NULL) && (fptr->obj_id == obj_id))
     {
        obj = fptr->obj;
        if (do_super)
          cur_klass = _eo2_kls_itr_next(obj->klass, fptr->cur_klass);
        else
          cur_klass = fptr->cur_klass;
        eo2_call_stack.frame_ptr++;
     }
   else
     {
        obj = _eo_obj_pointer_get((Eo_Id)obj_id);
        if (!obj) return EINA_FALSE;
        if (do_super)
          cur_klass = _eo2_kls_itr_next(obj->klass, obj->klass);
        else
          cur_klass = obj->klass;
        if (fptr == NULL)
          eo2_call_stack.frame_ptr = &eo2_call_stack.stack[0];
        else
          eo2_call_stack.frame_ptr++;
     }

   fptr = eo2_call_stack.frame_ptr;

   if ((fptr - eo2_call_stack.stack) >= EO2_CALL_STACK_SIZE)
     ERR("eo2 call stack overflow !!!");

   _eo_ref(obj);

   fptr->obj = obj;
   fptr->obj_id = obj_id;
   fptr->cur_klass = cur_klass;
   fptr->obj_data = EO2_INVALID_DATA;

   return EINA_TRUE;
}

EAPI void
eo2_do_end()
{
   Eo2_Stack_Frame *fptr;

   fptr = eo2_call_stack.frame_ptr;

   _eo_unref(fptr->obj);

   fptr->obj = NULL;
   fptr->obj_id = NULL;
   fptr->cur_klass = NULL;
   fptr->obj_data = EO2_INVALID_DATA;

   if (fptr == &eo2_call_stack.stack[0])
     eo2_call_stack.frame_ptr = NULL;
   else
     eo2_call_stack.frame_ptr--;
}

EAPI Eina_Bool
eo2_call_resolve_internal(const Eo_Class *klass_id, Eo_Op op, Eo2_Op_Call_Data *call)
{
   Eo2_Stack_Frame *fptr;
   const _Eo * obj;
   const _Eo_Class *klass;
   const op_type_funcs *func;

   fptr = eo2_call_stack.frame_ptr;
   obj = fptr->obj;

   if (klass_id)
      klass = _eo_class_pointer_get(klass_id);
   else
      klass = fptr->cur_klass;

   func = _eo_kls_itr_func_get(klass, op);
   if (EINA_LIKELY(func != NULL))
     {
        call->obj_id = fptr->obj_id;
        call->func = func->func;

        if (func->src == obj->klass)
          {
             if (fptr->obj_data == EO2_INVALID_DATA)
               fptr->obj_data = _eo_data_scope_get(obj, func->src);

             call->data = fptr->obj_data;
          }
        else
          call->data = _eo_data_scope_get(obj, func->src);

        return EINA_TRUE;
     }

   /* Try composite objects */
   /* FIXME!!! */
   return EINA_FALSE;
}


static inline const Eo2_Op_Description *
_eo2_api_desc_get(void *api_func, const _Eo_Class *klass)
{
   int imin, imax, imid;
   Eo2_Op_Description *op_desc;
   Eo2_Op_Description *op_descs;

   while (klass)
     {
        imin = 0;
        imax = klass->desc->ops.count - 1;
        op_descs = klass->desc->ops.descs2;

        while (imax >= imin)
          {
             imid = (imax + imin) / 2;
             op_desc = op_descs + imid;

             if (op_desc->api_func > api_func)
               imin = imid + 1;
             else if (op_desc->api_func < api_func)
               imax = imid - 1;
             else
               return op_desc;
          }

        klass = klass->parent;
     }

   return NULL;
}

EAPI Eo_Op
eo2_api_op_id_get(void *api_func, const Eo_Class *klass_id)
{
    const Eo2_Op_Description *desc;
    const _Eo_Class *klass;

   if (klass_id)
      klass = _eo_class_pointer_get(klass_id);
   else
      klass = eo2_call_stack.frame_ptr->obj->klass;

   desc = _eo2_api_desc_get(api_func, klass);

   if (desc == NULL)
     return EO_NOOP;

    return desc->op;
}

static int
eo2_api_funcs_cmp(const void *p1, const void *p2)
{
   const Eo2_Op_Description *op1, *op2;
   op1 = (Eo2_Op_Description *) p1;
   op2 = (Eo2_Op_Description *) p2;
   if (op1->api_func > op2->api_func) return -1;
   else if (op1->api_func < op2->api_func) return 1;
   else return 0;
}

EAPI void
eo2_class_funcs_set(Eo_Class *klass_id)
{
   int op_id;
   _Eo_Class *klass;
    const Eo2_Op_Description *api_desc;
   Eo2_Op_Description *op_desc;
   Eo2_Op_Description *op_descs;

   klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN(klass, EO_CLASS_EINA_MAGIC);
   op_descs = klass->desc->ops.descs2;

   qsort((void*)op_descs, klass->desc->ops.count, sizeof(Eo2_Op_Description), eo2_api_funcs_cmp);

   op_id = klass->base_id;
   for (op_desc = op_descs; op_desc->op_type != EO_OP_TYPE_INVALID; op_desc++)
     {
        if(op_desc->api_func == NULL)
          ERR("Setting implementation for NULL API. Class '%s', Func index: %lu",
              klass->desc->name, (unsigned long) (op_desc - op_descs));

        if (op_desc->op == EO_NOOP)
          {
             op_desc->op = op_id;
             op_id++;
          }
        else if (op_desc->op == EO2_OP_OVERRIDE)
          {
             if (klass->parent == NULL)
               ERR("Can't inherit from a NULL parent. Class '%s', Func index: %lu",
                   klass->desc->name, (unsigned long) (op_desc - op_descs));

             api_desc = _eo2_api_desc_get(op_desc->api_func, klass->parent);

             if (api_desc == NULL)
               ERR("Can't find api func %p description in class hierarchy. Class '%s', Func index: %lu",
                   op_desc->api_func, klass->desc->name, (unsigned long) (op_desc - op_descs));

             op_desc->op = api_desc->op;
             op_desc->doc = api_desc->doc;

             if (op_desc->op == EO_NOOP)
               ERR("API func %p, not found in direct parent '%s'. Class '%s', Func index: %lu",
                   op_desc->api_func, klass->parent->desc->name,
                   klass->desc->name, (unsigned long) (op_desc - op_descs));
          }

        /* printf("%d %p %p %s\n", op_desc->op, op_desc->api_func, op_desc->func, op_desc->doc); */
        _dich_func_set(klass, op_desc->op, op_desc->func);
     }
}

/*****************************************************************************/

#define _EO_OP_ERR_NO_OP_PRINT(file, line, op, klass) \
   do \
      { \
         const _Eo_Class *op_klass = _eo_op_class_get(op); \
         const char *_dom_name = (op_klass) ? op_klass->desc->name : NULL; \
         ERR("in %s:%d: Can't execute function %s:%s (op 0x%x) for class '%s'. Aborting.", \
               file, line, _dom_name, _eo_op_id_name_get(op), op, \
               (klass) ? klass->desc->name : NULL); \
      } \
   while (0)

static inline Eina_Bool
_eo_op_internal(const char *file, int line, _Eo *obj, const _Eo_Class *cur_klass,
                Eo_Op_Type op_type, Eo_Op op, va_list *p_list)
{
#ifdef EO_DEBUG
   const Eo_Op_Description *op_desc = _eo_op_id_desc_get(op);

   if (op_desc)
     {
        if (op_desc->op_type == EO_OP_TYPE_CLASS)
          {
             ERR("in %s:%d: Tried calling a class op '%s' (0x%x) from a non-class context.",
                 file, line, (op_desc) ? op_desc->name : NULL, op);
             return EINA_FALSE;
          }
     }
#endif

     {
        const op_type_funcs *func = _eo_kls_itr_func_get(cur_klass, op);
        if (EINA_LIKELY(func != NULL))
          {
             void *func_data = _eo_data_scope_get(obj, func->src);
             func->func((Eo *)obj->obj_id, func_data, p_list);
             return EINA_TRUE;
          }
     }

   /* Try composite objects */
     {
        Eina_List *itr;
        Eo *emb_obj_id;
        EINA_LIST_FOREACH(obj->composite_objects, itr, emb_obj_id)
          {
             /* FIXME: Clean this up a bit. */
             EO_OBJ_POINTER_RETURN_VAL(emb_obj_id, emb_obj, EINA_FALSE);
             if (_eo_op_internal(file, line, emb_obj, emb_obj->klass, op_type, op, p_list))
               {
                  return EINA_TRUE;
               }
          }
     }
   return EINA_FALSE;
}

static inline Eina_Bool
_eo_dov_internal(const char *file, int line, _Eo *obj, Eo_Op_Type op_type, va_list *p_list)
{
   Eina_Bool prev_error;
   Eina_Bool ret = EINA_TRUE;
   Eo_Op op = EO_NOOP;

   prev_error = obj->do_error;
   _eo_ref(obj);

   op = va_arg(*p_list, Eo_Op);
   while (op)
     {
        if (!_eo_op_internal(file, line, obj, obj->klass, op_type, op, p_list))
          {
             _EO_OP_ERR_NO_OP_PRINT(file, line, op, obj->klass);
             ret = EINA_FALSE;
             break;
          }
        op = va_arg(*p_list, Eo_Op);
     }

   if (obj->do_error)
      ret = EINA_FALSE;

   obj->do_error = prev_error;

   _eo_unref(obj);

   return ret;
}

EAPI Eina_Bool
eo_do_internal(const char *file, int line, Eo *obj_id, Eo_Op_Type op_type, ...)
{
   Eina_Bool ret = EINA_TRUE;
   va_list p_list;

   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);

   va_start(p_list, op_type);

   ret = _eo_dov_internal(file, line, obj, op_type, &p_list);

   va_end(p_list);

   return ret;
}

EAPI Eina_Bool
eo_vdo_internal(const char *file, int line, Eo *obj_id, Eo_Op_Type op_type, va_list *ops)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);

   return _eo_dov_internal(file, line, obj, op_type, ops);
}

EAPI Eina_Bool
eo_do_super_internal(const char *file, int line, Eo *obj_id, const Eo_Class *cur_klass_id,
                     Eo_Op_Type op_type, Eo_Op op, ...)
{
   const _Eo_Class *nklass;
   Eina_Bool ret = EINA_TRUE;
   va_list p_list;

   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);
   _Eo_Class *cur_klass = _eo_class_pointer_get(cur_klass_id);
   EO_MAGIC_RETURN_VAL(cur_klass, EO_CLASS_EINA_MAGIC, EINA_FALSE);

   /* Advance the kls itr. */
   nklass = _eo_kls_itr_next(obj->klass, cur_klass, op);

   va_start(p_list, op);
   if (!_eo_op_internal(file, line, obj, nklass, op_type, op, &p_list))
     {
        _EO_OP_ERR_NO_OP_PRINT(file, line, op, nklass);
        ret = EINA_FALSE;
     }
   va_end(p_list);

   if (obj->do_error)
      ret = EINA_FALSE;

   return ret;
}

static Eina_Bool
_eo_class_op_internal(const char *file, int line, _Eo_Class *klass, const _Eo_Class *cur_klass,
                      Eo_Op op, va_list *p_list)
{
#ifdef EO_DEBUG
   const Eo_Op_Description *op_desc = _eo_op_id_desc_get(op);

   if (op_desc)
     {
        if (op_desc->op_type != EO_OP_TYPE_CLASS)
          {
             ERR("in %s:%d: Tried calling an instance op '%s' (0x%x) from a class context.",
                 file, line, (op_desc) ? op_desc->name : NULL, op);
             return EINA_FALSE;
          }
     }
#else
   (void) file;
   (void) line;
#endif

     {
        const op_type_funcs *func = _eo_kls_itr_func_get(cur_klass, op);
        if (func)
          {
             ((eo_op_func_type_class) func->func)(_eo_class_id_get(klass), p_list);
             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

EAPI Eina_Bool
eo_class_do_internal(const char *file, int line, const Eo_Class *klass_id, ...)
{
   Eina_Bool ret = EINA_TRUE;
   Eo_Op op = EO_NOOP;
   va_list p_list;

   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, EINA_FALSE);

   va_start(p_list, klass_id);

   op = va_arg(p_list, Eo_Op);
   while (op)
     {
        if (!_eo_class_op_internal(file, line, (_Eo_Class *) klass, klass, op, &p_list))
          {
             _EO_OP_ERR_NO_OP_PRINT(file, line, op, klass);
             ret = EINA_FALSE;
             break;
          }
        op = va_arg(p_list, Eo_Op);
     }

   va_end(p_list);

   return ret;
}

EAPI Eina_Bool
eo_class_do_super_internal(const char *file, int line, const Eo_Class *klass_id,
                           const Eo_Class *cur_klass_id, Eo_Op op, ...)
{
   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   _Eo_Class *cur_klass = _eo_class_pointer_get(cur_klass_id);
   const _Eo_Class *nklass;
   Eina_Bool ret = EINA_TRUE;
   va_list p_list;
   EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, EINA_FALSE);
   EO_MAGIC_RETURN_VAL(cur_klass, EO_CLASS_EINA_MAGIC, EINA_FALSE);

   /* Advance the kls itr. */
   nklass = _eo_kls_itr_next(klass, cur_klass, op);

   va_start(p_list, op);
   if (!_eo_class_op_internal(file, line, (_Eo_Class *) klass, nklass, op, &p_list))
     {
        _EO_OP_ERR_NO_OP_PRINT(file, line, op, nklass);
        ret = EINA_FALSE;
     }
   va_end(p_list);

   return ret;
}

EAPI const Eo_Class *
eo_class_get(const Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, NULL);

   if (obj->klass)
      return _eo_class_id_get(obj->klass);
   return NULL;
}

EAPI const char *
eo_class_name_get(const Eo_Class *klass_id)
{
   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, NULL);

   return klass->desc->name;
}

static void
_eo_class_base_op_init(_Eo_Class *klass)
{
   const Eo_Class_Description *desc = klass->desc;

   klass->base_id = _eo_ops_last_id;

   if (desc && desc->ops.base_op_id)
      *(desc->ops.base_op_id) = klass->base_id;

   _eo_ops_last_id += desc->ops.count + 1;

   klass->chain_size = DICH_CHAIN1(_eo_ops_last_id) + 1;
   klass->chain = calloc(klass->chain_size, sizeof(*klass->chain));
}

#ifdef EO_DEBUG
static Eina_Bool
_eo_class_mro_has(const _Eo_Class *klass, const _Eo_Class *find)
{
   const _Eo_Class **itr;
   for (itr = klass->mro ; *itr ; itr++)
     {
        if (*itr == find)
          {
             return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}
#endif

static Eina_List *
_eo_class_list_remove_duplicates(Eina_List* list)
{
   Eina_List *itr1, *itr2, *itr2n;

   itr1 = eina_list_last(list);
   while (itr1)
     {
        itr2 = eina_list_prev(itr1);

        while (itr2)
          {
             itr2n = eina_list_prev(itr2);

             if (eina_list_data_get(itr1) == eina_list_data_get(itr2))
               {
                  list = eina_list_remove_list(list, itr2);
               }

             itr2 = itr2n;
          }

        itr1 = eina_list_prev(itr1);
     }

   return list;
}

static Eina_List *
_eo_class_mro_add(Eina_List *mro, const _Eo_Class *klass)
{
   if (!klass)
     return mro;

   mro = eina_list_append(mro, klass);

   /* Recursively add MIXINS extensions. */
     {
        const _Eo_Class **extn_itr;

        for (extn_itr = klass->extensions ; *extn_itr ; extn_itr++)
          {
             const _Eo_Class *extn = *extn_itr;
             if (extn->desc->type == EO_CLASS_TYPE_MIXIN)
               mro = _eo_class_mro_add(mro, extn);
          }
     }

   mro = _eo_class_mro_add(mro, klass->parent);

   return mro;
}

static Eina_List *
_eo_class_mro_init(const Eo_Class_Description *desc, const _Eo_Class *parent, Eina_List *extensions)
{
   Eina_List *mro = NULL;
   Eina_List *extn_itr = NULL;
   Eina_List *extn_pos = NULL;
   const _Eo_Class *extn = NULL;

   /* Add MIXINS extensions. */
   EINA_LIST_FOREACH(extensions, extn_itr, extn)
     {
        if (extn->desc->type != EO_CLASS_TYPE_MIXIN)
          continue;

        mro = _eo_class_mro_add(mro, extn);
        extn_pos = eina_list_append(extn_pos, eina_list_last(mro));
     }

   /* Check if we can create a consistent mro */
     {
        Eina_List *itr = extn_pos;
        EINA_LIST_FOREACH(extensions, extn_itr, extn)
          {
             if (extn->desc->type != EO_CLASS_TYPE_MIXIN)
                continue;

             /* Get the first one after the extension. */
             Eina_List *extn_list = eina_list_next(eina_list_data_get(itr));

             /* If we found the extension again. */
             if (eina_list_data_find_list(extn_list, extn))
               {
                  eina_list_free(mro);
                  eina_list_free(extn_pos);
                  ERR("Cannot create a consistent method resolution order for class '%s' because of '%s'.", desc->name, extn->desc->name);
                  return NULL;
               }

             itr = eina_list_next(itr);
          }
     }

   eina_list_free(extn_pos);

   mro = _eo_class_mro_add(mro, parent);
   mro = _eo_class_list_remove_duplicates(mro);
   /* Will be replaced with the actual class pointer */
   mro = eina_list_prepend(mro, NULL);

   return mro;
}

static void
_eo_class_constructor(_Eo_Class *klass)
{
   if (klass->constructed)
      return;

   klass->constructed = EINA_TRUE;

   if (klass->desc->class_constructor)
      klass->desc->class_constructor(_eo_class_id_get(klass));
}

EAPI void
eo_class_funcs_set(Eo_Class *klass_id, const Eo_Op_Func_Description *func_descs)
{
   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN(klass, EO_CLASS_EINA_MAGIC);

   const Eo_Op_Func_Description *itr;
   itr = func_descs;
   if (itr)
     {
        for ( ; itr->op_type != EO_OP_TYPE_INVALID ; itr++)
          {
             const Eo_Op_Description *op_desc = _eo_op_id_desc_get(itr->op);

             if (EINA_UNLIKELY(!op_desc || (itr->op == EO_NOOP)))
               {
                  ERR("Setting implementation for non-existent op 0x%x for class '%s'. Func index: %lu", itr->op, klass->desc->name, (unsigned long) (itr - func_descs));
               }
             else if (EINA_LIKELY(itr->op_type == op_desc->op_type))
               {
                  _dich_func_set(klass, itr->op, itr->func);
               }
             else
               {
                  ERR("Set function's op type (0x%x) is different than the one in the op description (%d) for op '%s:%s'. Func index: %lu",
                        itr->op_type,
                        (op_desc) ? op_desc->op_type : EO_OP_TYPE_REGULAR,
                        klass->desc->name,
                        (op_desc) ? op_desc->name : NULL,
                        (unsigned long) (itr - func_descs));
               }
          }
     }
}

static void
eo_class_free(_Eo_Class *klass)
{
   if (klass->constructed)
     {
        if (klass->desc->class_destructor)
           klass->desc->class_destructor(_eo_class_id_get(klass));

        _dich_func_clean_all(klass);
     }

   free(klass);
}

/* DEVCHECK */
static Eina_Bool
_eo_class_check_op_descs(const Eo_Class_Description *desc)
{
   const Eo_Op_Description *itr;
   size_t i;

   if (desc->ops.count > 0)
     {
        if (!desc->ops.base_op_id)
          {
             ERR("Class '%s' has a non-zero ops count, but base_id is NULL.",
                   desc->name);
             return EINA_FALSE;
          }

        if (!desc->ops.descs)
          {
             ERR("Class '%s' has a non-zero ops count, but there are no descs.",
                   desc->name);
             return EINA_FALSE;
          }
     }

   itr = desc->ops.descs;
   for (i = 0 ; i < desc->ops.count ; i++, itr++)
     {
        if (itr->sub_op != i)
          {
             if (itr->name)
               {
                  ERR("Wrong order in Ops description for class '%s'. Expected 0x%lx and got 0x%lx", desc->name, (unsigned long) i, (unsigned long) itr->sub_op);
               }
             else
               {
                  ERR("Found too few Ops description for class '%s'. Expected 0x%lx descriptions, but found 0x%lx.", desc->name, (unsigned long) desc->ops.count, (unsigned long) i);
               }
             return EINA_FALSE;
          }
     }

   if (itr && itr->name)
     {
        ERR("Found extra Ops description for class '%s'. Expected %lu descriptions, but found more.", desc->name, (unsigned long) desc->ops.count);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

/* Not really called, just used for the ptr... */
static void
_eo_class_isa_func(Eo *obj_id EINA_UNUSED, void *class_data EINA_UNUSED, va_list *list EINA_UNUSED)
{
   /* Do nonthing. */
}

EAPI const Eo_Class *
eo_class_new(const Eo_Class_Description *desc, const Eo_Class *parent_id, ...)
{
   _Eo_Class *klass;
   va_list p_list;
   size_t extn_sz, mro_sz, mixins_sz;
   Eina_List *extn_list, *mro, *mixins;

   _Eo_Class *parent = _eo_class_pointer_get(parent_id);
   if (parent && !EINA_MAGIC_CHECK(parent, EO_CLASS_EINA_MAGIC))
     {
        EINA_MAGIC_FAIL(parent, EO_CLASS_EINA_MAGIC);
        return NULL;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(desc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(desc->name, NULL);

   if (desc->version == EO2_VERSION)
     {
        // FIXME: eo2
        /* if (!_eo2_class_check_op_descs(desc)) */
        /*   return NULL; */
     }
   else
     {
        if (!_eo_class_check_op_descs(desc))
          return NULL;
     }

   /* Check restrictions on Interface types. */
   if (desc->type == EO_CLASS_TYPE_INTERFACE)
     {
        EINA_SAFETY_ON_FALSE_RETURN_VAL(!desc->data_size, NULL);
     }

   /* Check parent */
   if (parent)
     {
        /* Verify the inheritance is allowed. */
        switch (desc->type)
          {
           case EO_CLASS_TYPE_REGULAR:
           case EO_CLASS_TYPE_REGULAR_NO_INSTANT:
              if ((parent->desc->type != EO_CLASS_TYPE_REGULAR) &&
                    (parent->desc->type != EO_CLASS_TYPE_REGULAR_NO_INSTANT))
                {
                   ERR("Regular classes ('%s') aren't allowed to inherit from non-regular classes ('%s').",
                       desc->name, parent->desc->name);
                   return NULL;
                }
              break;
           case EO_CLASS_TYPE_INTERFACE:
           case EO_CLASS_TYPE_MIXIN:
              if ((parent->desc->type != EO_CLASS_TYPE_INTERFACE) &&
                    (parent->desc->type != EO_CLASS_TYPE_MIXIN))
                {
                   ERR("Non-regular classes ('%s') aren't allowed to inherit from regular classes ('%s').",
                       desc->name, parent->desc->name);
                   return NULL;
                }
              break;
          }
     }

   /* Build class extensions list */
     {
        DBG("Started building extensions list for class '%s'", desc->name);
        extn_list = NULL;
        const _Eo_Class *extn = NULL;
        const Eo_Class_Id *extn_id = NULL;

        va_start(p_list, parent_id);

        extn_id = va_arg(p_list, Eo_Class_Id *);
        while (extn_id)
          {
             extn = _eo_class_pointer_get((Eo_Class *)extn_id);
             switch (extn->desc->type)
               {
                case EO_CLASS_TYPE_REGULAR:
                case EO_CLASS_TYPE_REGULAR_NO_INSTANT:
                case EO_CLASS_TYPE_INTERFACE:
                case EO_CLASS_TYPE_MIXIN:
                   extn_list = eina_list_append(extn_list, extn);
                   break;
               }

             extn_id = va_arg(p_list, Eo_Class_Id *);
          }

        va_end(p_list);

        extn_list = _eo_class_list_remove_duplicates(extn_list);

        extn_sz = sizeof(_Eo_Class *) * (eina_list_count(extn_list) + 1);

        DBG("Finished building extensions list for class '%s'", desc->name);
     }

   /* Prepare mro list */
     {
        DBG("Started building MRO list for class '%s'", desc->name);

        mro = _eo_class_mro_init(desc, parent, extn_list);
        if (!mro)
          {
             eina_list_free(extn_list);
             return NULL;
          }

        mro_sz = sizeof(_Eo_Class *) * (eina_list_count(mro) + 1);

        DBG("Finished building MRO list for class '%s'", desc->name);
     }

   /* Prepare mixins list */
     {
        Eina_List *itr;
        const _Eo_Class *kls_itr;

        DBG("Started building Mixins list for class '%s'", desc->name);

        mixins = NULL;
        EINA_LIST_FOREACH(mro, itr, kls_itr)
          {
             if ((kls_itr) && (kls_itr->desc->type == EO_CLASS_TYPE_MIXIN) &&
                   (kls_itr->desc->data_size > 0))
               mixins = eina_list_append(mixins, kls_itr);
          }

        mixins_sz = sizeof(Eo_Extension_Data_Offset) * (eina_list_count(mixins) + 1);
        if ((desc->type == EO_CLASS_TYPE_MIXIN) && (desc->data_size > 0))
          mixins_sz += sizeof(Eo_Extension_Data_Offset);

        DBG("Finished building Mixins list for class '%s'", desc->name);
     }

   klass = calloc(1, _eo_class_sz + extn_sz + mro_sz + mixins_sz);
   EINA_MAGIC_SET(klass, EO_CLASS_EINA_MAGIC);
   klass->parent = parent;
   klass->desc = desc;
   klass->extensions = (const _Eo_Class **) ((char *) klass + _eo_class_sz);
   klass->mro = (const _Eo_Class **) ((char *) klass->extensions + extn_sz);
   klass->extn_data_off = (Eo_Extension_Data_Offset *) ((char *) klass->mro + mro_sz);
   if (klass->parent)
     {
        /* FIXME: Make sure this alignment is enough. */
        klass->data_offset = klass->parent->data_offset +
           EO_ALIGN_SIZE(klass->parent->desc->data_size);
     }

   mro = eina_list_remove(mro, NULL);
   mro = eina_list_prepend(mro, klass);
   if ((desc->type == EO_CLASS_TYPE_MIXIN) && (desc->data_size > 0))
     mixins = eina_list_prepend(mixins, klass);

   /* Copy the extensions and free the list */
     {
        const _Eo_Class *extn = NULL;
        const _Eo_Class **extn_itr = klass->extensions;
        EINA_LIST_FREE(extn_list, extn)
          {
             *(extn_itr++) = extn;

             DBG("Added '%s' extension", extn->desc->name);
          }
        *(extn_itr) = NULL;
     }

   /* Copy the mro and free the list. */
     {
        const _Eo_Class *kls_itr = NULL;
        const _Eo_Class **mro_itr = klass->mro;
        EINA_LIST_FREE(mro, kls_itr)
          {
             *(mro_itr++) = kls_itr;

             DBG("Added '%s' to MRO", kls_itr->desc->name);
          }
        *(mro_itr) = NULL;
     }

   size_t extn_data_off = klass->data_offset +
      EO_ALIGN_SIZE(klass->desc->data_size);

   /* Feed the mixins data offsets and free the mixins list. */
     {
        const _Eo_Class *kls_itr = NULL;
        Eo_Extension_Data_Offset *extn_data_itr = klass->extn_data_off;
        EINA_LIST_FREE(mixins, kls_itr)
          {
             extn_data_itr->klass = kls_itr;
             extn_data_itr->offset = extn_data_off;

             extn_data_off += EO_ALIGN_SIZE(extn_data_itr->klass->desc->data_size);
             extn_data_itr++;

             DBG("Added '%s' to Data Offset info", kls_itr->desc->name);
          }
        extn_data_itr->klass = 0;
        extn_data_itr->offset = 0;
     }

   klass->obj_size = _eo_sz + extn_data_off;
   if (getenv("EO_DEBUG"))
     {
        fprintf(stderr, "Eo class '%s' will take %u bytes per object.\n",
                desc->name, klass->obj_size);
     }

   _eo_class_base_op_init(klass);
   /* Flatten the function array */
     {
        const _Eo_Class **mro_itr = klass->mro;
        for (  ; *mro_itr ; mro_itr++)
           ;

        /* Skip ourselves. */
        for ( mro_itr-- ; mro_itr > klass->mro ; mro_itr--)
          {
             _dich_copy_all(klass, *mro_itr);
          }
     }

   /* Mark which classes we implement */
     {
        const _Eo_Class **extn_itr;

        for (extn_itr = klass->extensions ; *extn_itr ; extn_itr++)
          {
             const _Eo_Class *extn = *extn_itr;
             /* Set it in the dich. */
             _dich_func_set(klass, extn->base_id +
                   extn->desc->ops.count, _eo_class_isa_func);
          }

        _dich_func_set(klass, klass->base_id + klass->desc->ops.count,
              _eo_class_isa_func);

        if (klass->parent)
          {
             _dich_func_set(klass,
                   klass->parent->base_id + klass->parent->desc->ops.count,
                   _eo_class_isa_func);
          }
     }

   eina_lock_take(&_eo_class_creation_lock);
   klass->class_id = ++_eo_classes_last_id;
     {
        /* FIXME: Handle errors. */
        size_t arrsize = _eo_classes_last_id * sizeof(*_eo_classes);
        _Eo_Class **tmp;
        tmp = realloc(_eo_classes, arrsize);

        /* If it's the first allocation, memset. */
        if (!_eo_classes)
           memset(tmp, 0, arrsize);

        _eo_classes = tmp;
        _eo_classes[klass->class_id - 1] = klass;
     }
   eina_lock_release(&_eo_class_creation_lock);

   _eo_class_constructor(klass);

   return _eo_class_id_get(klass);
}

EAPI Eina_Bool
eo_isa(const Eo *obj_id, const Eo_Class *klass_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);
   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, EINA_FALSE);
   const op_type_funcs *func = _dich_func_get(obj->klass,
         klass->base_id + klass->desc->ops.count);

   /* Currently implemented by reusing the LAST op id. Just marking it with
    * _eo_class_isa_func. */
   return (func && (func->func == _eo_class_isa_func));
}

EAPI Eina_Bool
eo_parent_set(Eo *obj_id, const Eo *parent_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);
   if (parent_id)
     {
        EO_OBJ_POINTER_RETURN_VAL(parent_id, parent, EINA_FALSE);
     }

   if (obj->parent == parent_id)
      return EINA_TRUE;

   _eo_ref(obj);

   if (eo_composite_is(obj_id))
     {
        eo_composite_detach(obj_id, obj->parent);
     }

   if (obj->parent)
     {
        EO_OBJ_POINTER_RETURN_VAL(obj->parent, obj_parent, EINA_FALSE);
        obj_parent->children = eina_list_remove(obj_parent->children, obj_id);
        eo_xunref(obj_id, obj->parent);
     }

   obj->parent = (Eo *) parent_id;
   if (obj->parent)
     {
        EO_OBJ_POINTER_RETURN_VAL(parent_id, parent, EINA_FALSE);
        parent->children = eina_list_append(parent->children, obj_id);
        eo_xref(obj_id, obj->parent);
     }

   _eo_unref(obj);

   return EINA_TRUE;
}

EAPI Eo *
eo_add_internal(const char *file, int line, const Eo_Class *klass_id, Eo *parent_id, ...)
{
   Eina_Bool do_err;
   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, NULL);

   if (parent_id)
     {
        EO_OBJ_POINTER_RETURN_VAL(parent_id, parent, NULL);
     }

   if (EINA_UNLIKELY(klass->desc->type != EO_CLASS_TYPE_REGULAR))
     {
        ERR("in %s:%d: Class '%s' is not instantiate-able. Aborting.", file, line, klass->desc->name);
        return NULL;
     }

   _Eo *obj = calloc(1, klass->obj_size);
   obj->refcount++;
   obj->klass = klass;

#ifndef HAVE_EO_ID
   EINA_MAGIC_SET(obj, EO_EINA_MAGIC);
#endif
   Eo_Id obj_id = _eo_id_allocate(obj);
   obj->obj_id = obj_id;
   eo_parent_set((Eo *)obj_id, parent_id);

   _eo_condtor_reset(obj);

   _eo_ref(obj);

   if (klass->desc->version == EO2_VERSION)
     eo2_do((Eo *)obj_id, eo2_constructor());
   /* Run the relevant do stuff. */
     {
        va_list p_list;
        va_start(p_list, parent_id);
        do_err = !_eo_dov_internal(file, line, obj, EO_OP_TYPE_REGULAR, &p_list);
        va_end(p_list);
     }

   if (EINA_UNLIKELY(do_err))
     {
        ERR("in %s:%d, Object of class '%s' - One of the object constructors have failed.",
            file, line, klass->desc->name);
        goto fail;
     }

   if (!obj->condtor_done)
     {
        ERR("in %s:%d: Object of class '%s' - Not all of the object constructors have been executed.",
            file, line, klass->desc->name);
        goto fail;
     }

   _eo_unref(obj);

   return (Eo *)obj_id;

fail:
   /* Unref twice, once for the ref above, and once for the basic object ref. */
   _eo_unref(obj);
   _eo_unref(obj);
   return NULL;
}

typedef struct
{
   EINA_INLIST;
   const Eo *ref_obj;
   const char *file;
   int line;
} Eo_Xref_Node;

EAPI Eo *
eo_xref_internal(const char *file, int line, Eo *obj_id, const Eo *ref_obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, obj_id);

   _eo_ref(obj);

#ifdef EO_DEBUG
   Eo_Xref_Node *xref = calloc(1, sizeof(*xref));
   xref->ref_obj = ref_obj_id;
   xref->file = file;
   xref->line = line;

   obj->xrefs = eina_inlist_prepend(obj->xrefs, EINA_INLIST_GET(xref));
#else
   (void) ref_obj_id;
   (void) file;
   (void) line;
#endif

   return obj_id;
}

EAPI void
eo_xunref(Eo *obj_id, const Eo *ref_obj_id)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);
#ifdef EO_DEBUG
   Eo_Xref_Node *xref = NULL;
   EINA_INLIST_FOREACH(obj->xrefs, xref)
     {
        if (xref->ref_obj == ref_obj_id)
          break;
     }

   if (xref)
     {
        obj->xrefs = eina_inlist_remove(obj->xrefs, EINA_INLIST_GET(xref));
        free(xref);
     }
   else
     {
        ERR("ref_obj (%p) does not reference obj (%p). Aborting unref.", ref_obj_id, obj_id);
        return;
     }
#else
   (void) ref_obj_id;
#endif
   _eo_unref(obj);
}

EAPI Eo *
eo_ref(const Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, (Eo *)obj_id);

   _eo_ref(obj);
   return (Eo *)obj_id;
}

EAPI void
eo_unref(const Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);

   _eo_unref(obj);
}

EAPI void
eo_del(const Eo *obj)
{
   eo_parent_set((Eo *) obj, NULL);
   eo_unref(obj);
}

EAPI int
eo_ref_get(const Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, 0);

   return obj->refcount;
}

EAPI Eo *
eo_parent_get(const Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, NULL);

   return obj->parent;
}

EAPI void
eo_error_set_internal(const Eo *obj_id, const char *file, int line)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);

   ERR("Error with obj '%p' at %s:%d", obj, file, line);

   obj->do_error = EINA_TRUE;
}

void
_eo_condtor_done(Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);
   if (obj->condtor_done)
     {
        ERR("Object %p is already constructed at this point.", obj);
        return;
     }

   obj->condtor_done = EINA_TRUE;
}

static inline void *
_eo_data_scope_get(const _Eo *obj, const _Eo_Class *klass)
{
   if (EINA_LIKELY(klass->desc->data_size > 0))
     {
        if (EINA_UNLIKELY(klass->desc->type == EO_CLASS_TYPE_MIXIN))
          {
             Eo_Extension_Data_Offset *doff_itr = obj->klass->extn_data_off;

             if (!doff_itr)
               return NULL;

             while (doff_itr->klass)
               {
                  if (doff_itr->klass == klass)
                    return ((char *) obj) + _eo_sz + doff_itr->offset;
                  doff_itr++;
               }
          }
        else
          {
             return ((char *) obj) + _eo_sz + klass->data_offset;
          }
     }

   return NULL;
}

static inline void *
_eo_data_xref_internal(const char *file, int line, _Eo *obj, const _Eo_Class *klass, const _Eo *ref_obj)
{
   void *data = NULL;
   if (klass != NULL)
     {
        data = _eo_data_scope_get(obj, klass);
        if (data == NULL) return NULL;
     }
   (obj->datarefcount)++;
#ifdef EO_DEBUG
   Eo_Xref_Node *xref = calloc(1, sizeof(*xref));
   xref->ref_obj = (Eo *)ref_obj->obj_id;
   xref->file = file;
   xref->line = line;

   obj->data_xrefs = eina_inlist_prepend(obj->data_xrefs, EINA_INLIST_GET(xref));
#else
   (void) ref_obj;
   (void) file;
   (void) line;
#endif
   return data;
}

static inline void
_eo_data_xunref_internal(_Eo *obj, void *data, const _Eo *ref_obj)
{
#ifdef EO_DEBUG
   const _Eo_Class *klass = obj->klass;
   Eina_Bool in_range = (((char *)data >= (((char *) obj) + _eo_sz) &&
                          ((char *)data < (((char *) obj) + klass->obj_size)))
   if (!in_range)
     {
        ERR("Data %p is not in the data range of the object %p (%s).", data, (Eo *)obj->obj_id, obj->klass->desc->name);
     }
#else
   (void) data;
#endif
   if (obj->datarefcount == 0)
     {
        ERR("Data for object %lx (%s) is already not referenced.", (unsigned long)obj->obj_id, obj->klass->desc->name);
     }
   else
     {
        (obj->datarefcount)--;
     }
#ifdef EO_DEBUG
   Eo_Xref_Node *xref = NULL;
   EINA_INLIST_FOREACH(obj->data_xrefs, xref)
     {
        if (xref->ref_obj == (Eo *)ref_obj->obj_id)
          break;
     }

   if (xref)
     {
        obj->data_xrefs = eina_inlist_remove(obj->data_xrefs, EINA_INLIST_GET(xref));
        free(xref);
     }
   else
     {
        ERR("ref_obj (0x%lx) does not reference data (%p) of obj (0x%lx).", (unsigned long)ref_obj->obj_id, data, (unsigned long)obj->obj_id);
     }
#else
   (void) ref_obj;
#endif
}

EAPI void *
eo_data_get(const Eo *obj_id, const Eo_Class *klass_id)
{
   return eo_data_scope_get(obj_id, klass_id);
}

EAPI void *
eo_data_scope_get(const Eo *obj_id, const Eo_Class *klass_id)
{
   void *ret;
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, NULL);
   _Eo_Class *klass = _eo_class_pointer_get(klass_id);
   EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, NULL);

#ifdef EO_DEBUG
   if (!_eo_class_mro_has(obj->klass, klass))
     {
        ERR("Tried getting data of class '%s' from object of class '%s', but the former is not a direct inheritance of the latter.", klass->desc->name, obj->klass->desc->name);
        return NULL;
     }
#endif

   ret = _eo_data_scope_get(obj, klass);

#ifdef EO_DEBUG
   if (!ret && (klass->desc->data_size == 0))
     {
        ERR("Tried getting data of class '%s', but it has none..", klass->desc->name);
     }
#endif

   return ret;
}

EAPI void *
eo_data_xref_internal(const char *file, int line, const Eo *obj_id, const Eo_Class *klass_id, const Eo *ref_obj_id)
{
   void *ret;
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, NULL);
   EO_OBJ_POINTER_RETURN_VAL(ref_obj_id, ref_obj, NULL);
   _Eo_Class *klass = NULL;
   if (klass_id)
     {
        klass = _eo_class_pointer_get(klass_id);
        EO_MAGIC_RETURN_VAL(klass, EO_CLASS_EINA_MAGIC, NULL);

#ifdef EO_DEBUG
        if (!_eo_class_mro_has(obj->klass, klass))
          {
             ERR("Tried getting data of class '%s' from object of class '%s', but the former is not a direct inheritance of the latter.", klass->desc->name, obj->klass->desc->name);
             return NULL;
          }
#endif
     }

   ret = _eo_data_xref_internal(file, line, obj, klass, ref_obj);

#ifdef EO_DEBUG
   if (klass && !ret && (klass->desc->data_size == 0))
     {
        ERR("Tried getting data of class '%s', but it has none..", klass->desc->name);
     }
#endif

   return ret;
}

EAPI void
eo_data_xunref_internal(const Eo *obj_id, void *data, const Eo *ref_obj_id)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);
   EO_OBJ_POINTER_RETURN(ref_obj_id, ref_obj);
   _eo_data_xunref_internal(obj, data, ref_obj);
}

EAPI Eina_Bool
eo_init(void)
{
   const char *log_dom = "eo";
   if (_eo_init_count++ > 0)
     return EINA_TRUE;

   eina_init();

   _eo_sz = EO_ALIGN_SIZE(sizeof (_Eo));
   _eo_class_sz = EO_ALIGN_SIZE(sizeof (_Eo_Class));

   _eo_classes = NULL;
   _eo_classes_last_id = EO_CLASS_IDS_FIRST - 1;
   _eo_ops_last_id = EO_OP_IDS_FIRST;
   _eo_log_dom = eina_log_domain_register(log_dom, EINA_COLOR_LIGHTBLUE);
   if (_eo_log_dom < 0)
     {
        EINA_LOG_ERR("Could not register log domain: %s", log_dom);
        return EINA_FALSE;
     }

   if (!eina_lock_new(&_eo_class_creation_lock))
     {
        EINA_LOG_ERR("Could not init lock.");
        return EINA_FALSE;
     }

   eina_magic_string_static_set(EO_EINA_MAGIC, EO_EINA_MAGIC_STR);
   eina_magic_string_static_set(EO_FREED_EINA_MAGIC,
                                EO_FREED_EINA_MAGIC_STR);
   eina_magic_string_static_set(EO_CLASS_EINA_MAGIC,
                                EO_CLASS_EINA_MAGIC_STR);

#ifdef EO_DEBUG
   /* Call it just for coverage purposes. Ugly I know, but I like it better than
    * casting everywhere else. */
   _eo_class_isa_func(NULL, NULL, NULL);
#endif

   eina_log_timing(_eo_log_dom,
                   EINA_LOG_STATE_STOP,
                   EINA_LOG_STATE_INIT);

   return EINA_TRUE;
}

EAPI Eina_Bool
eo_shutdown(void)
{
   size_t i;
   _Eo_Class **cls_itr = _eo_classes;

   if (--_eo_init_count > 0)
     return EINA_TRUE;

   eina_log_timing(_eo_log_dom,
                   EINA_LOG_STATE_START,
                   EINA_LOG_STATE_SHUTDOWN);

   for (i = 0 ; i < _eo_classes_last_id ; i++, cls_itr++)
     {
        if (*cls_itr)
          eo_class_free(*cls_itr);
     }

   if (_eo_classes)
     free(_eo_classes);

   eina_lock_free(&_eo_class_creation_lock);

   _eo_free_ids_tables();

   eina_log_domain_unregister(_eo_log_dom);
   _eo_log_dom = -1;

   eina_shutdown();
   return EINA_TRUE;
}

EAPI void
eo_composite_attach(Eo *comp_obj_id, Eo *parent_id)
{
   EO_OBJ_POINTER_RETURN(comp_obj_id, comp_obj);
   EO_OBJ_POINTER_RETURN(parent_id, parent);

   comp_obj->composite = EINA_TRUE;
   parent->composite_objects = eina_list_prepend(parent->composite_objects, comp_obj_id);
   eo_parent_set(comp_obj_id, parent_id);
}

EAPI void
eo_composite_detach(Eo *comp_obj_id, Eo *parent_id)
{
   EO_OBJ_POINTER_RETURN(comp_obj_id, comp_obj);
   EO_OBJ_POINTER_RETURN(parent_id, parent);

   comp_obj->composite = EINA_FALSE;
   parent->composite_objects = eina_list_remove(parent->composite_objects, comp_obj_id);
   eo_parent_set(comp_obj_id, NULL);
}

EAPI Eina_Bool
eo_composite_is(const Eo *comp_obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(comp_obj_id, comp_obj, EINA_FALSE);

   return comp_obj->composite;
}

EAPI Eina_Bool
eo_destructed_is(const Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN_VAL(obj_id, obj, EINA_FALSE);

   return obj->del;
}

EAPI void
eo_manual_free_set(Eo *obj_id, Eina_Bool manual_free)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);
   obj->manual_free = manual_free;
}

EAPI void
eo_manual_free(Eo *obj_id)
{
   EO_OBJ_POINTER_RETURN(obj_id, obj);

   if (EINA_FALSE == obj->manual_free)
     {
        ERR("Tried to manually free the object %p while the option has not been set; see eo_manual_free_set for more information.", obj);
        return;
     }

   if (!obj->del)
     {
        ERR("Tried deleting the object %p while still referenced(%d).", obj_id, obj->refcount);
        return;
     }

   _eo_free(obj);
}

