#include "Eo.h"
#include "eo2_simple.h"

typedef struct
{
   int a;
   int b;
   int x;
} Private_Data;

static void
_inc(Eo *objid EINA_UNUSED, void *obj_data)
{
   Private_Data *data = (Private_Data *) obj_data;

   data->x += 1;
}
EAPI EO2_VOID_FUNC_BODY(inc2);

static int
_get(Eo *objid EINA_UNUSED, void *obj_data)
{
   const Private_Data *data = (Private_Data *) obj_data;

   return data->x;
}
EAPI EO2_FUNC_BODY(get2, int, 0);

static void
_set(Eo *objid EINA_UNUSED, void *obj_data, int x)
{
   Private_Data *data = (Private_Data *) obj_data;
   data->x = x;
}
EAPI EO2_VOID_FUNC_BODYV(set2, EO2_FUNC_CALL(x), int x);

static void
_constructor(Eo *obj, void *obj_data)
{
   Private_Data *data = (Private_Data *) obj_data;

   eo2_do_super(obj, eo2_constructor());

   data->x = 66;
}

static void
_destructor(Eo *obj, void *obj_data EINA_UNUSED)
{
   eo2_do_super(obj, eo2_destructor());
}

static void
_class_constructor(Eo_Class *klass)
{
   eo2_class_funcs_set(klass);
}

static Eo2_Op_Description op_descs [] = {
       EO2_OP_FUNC_OVERRIDE(_constructor, eo2_constructor, "Constructor"),
       EO2_OP_FUNC_OVERRIDE(_destructor, eo2_destructor, "Destructor"),
       EO2_OP_FUNC(_inc, inc2, "Inc X"),
       EO2_OP_FUNC(_get, get2, "Get X"),
       EO2_OP_FUNC(_set, set2, "Set X"),
       EO2_OP_SENTINEL
};

static const Eo_Class_Description class_desc = {
     EO2_VERSION,
     "Eo2 Simple",
     EO_CLASS_TYPE_REGULAR,
     EO2_CLASS_DESCRIPTION_OPS(op_descs, OP_DESC_SIZE(op_descs)),
     NULL,
     sizeof(Private_Data),
     _class_constructor,
     NULL
};

EO_DEFINE_CLASS(eo2_simple_class_get, &class_desc, EO2_BASE_CLASS, NULL)
