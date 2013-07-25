#include "Eo.h"
#include "eo2_simple.h"
#include "eo2_inherit.h"

typedef struct
{
   int y;
} Private_Data;

static void
_inc(Eo *objid, void *obj_data)
{
   Private_Data *data = (Private_Data *) obj_data;

   eo2_do_super(objid, inc2());
   data->y += 1;
}

static int
_get(Eo *objid EINA_UNUSED, void *obj_data)
{
   const Private_Data *data = (Private_Data *) obj_data;

   return data->y;
}
EAPI EO2_FUNC_BODY(get3, int, 0);

static void
_constructor(Eo *obj, void *obj_data)
{
   Private_Data *data = (Private_Data *) obj_data;

   eo2_do_super(obj, eo2_constructor());

   data->y = 68;
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
       EO2_OP_FUNC_OVERRIDE(_inc, inc2, "Inc X overridden"),
       EO2_OP_FUNC(_get, get3, "Get Y"),
       EO2_OP_SENTINEL
};

static const Eo_Class_Description class_desc = {
     EO2_VERSION,
     "Eo2 Inherit",
     EO_CLASS_TYPE_REGULAR,
     EO2_CLASS_DESCRIPTION_OPS(op_descs, OP_DESC_SIZE(op_descs)),
     NULL,
     sizeof(Private_Data),
     _class_constructor,
     NULL
};

EO_DEFINE_CLASS(eo2_inherit_class_get, &class_desc, EO2_SIMPLE_CLASS, NULL)
