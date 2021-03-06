#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "Eo.h"
#include "function_overrides_simple.h"
#include "function_overrides_inherit.h"
#include "function_overrides_inherit2.h"
#include "function_overrides_inherit3.h"

#include "../eunit_tests.h"

int
main(int argc, char *argv[])
{
   (void) argc;
   (void) argv;
   eo_init();

   Eo *obj = eo_add(INHERIT2_CLASS, NULL);

   eo_do(obj, simple_a_set(1));
   Simple_Public_Data *pd = eo_data_scope_get(obj, SIMPLE_CLASS);
   fail_if(pd->a != 2);

   eo_unref(obj);

   obj = eo_add(INHERIT3_CLASS, NULL);

   eo_do(obj, simple_a_set(1));
   pd = eo_data_scope_get(obj, SIMPLE_CLASS);
   fail_if(pd->a != 3);

   eo_unref(obj);

   obj = eo_add(INHERIT2_CLASS, NULL);
   fail_if(!eo_do(obj, inherit2_print()));
   fail_if(!eo_do(obj, inherit2_print(), inherit2_print()));
   eo_unref(obj);

   obj = eo_add(SIMPLE_CLASS, NULL);
   fail_if(eo_do(obj, inherit2_print2()));

#ifdef EO_DEBUG
   fail_if(eo_do(obj, simple_class_print()));
#endif

   fail_if(!eo_class_do(SIMPLE_CLASS, simple_class_print()));
   fail_if(!eo_class_do(INHERIT_CLASS, simple_class_print()));
   fail_if(!eo_class_do(INHERIT2_CLASS, simple_class_print()));
   fail_if(!eo_class_do(INHERIT3_CLASS, simple_class_print()));

#ifdef EO_DEBUG
   fail_if(eo_class_do(SIMPLE_CLASS, simple_a_print()));
#endif

   eo_do_super(obj, SIMPLE_CLASS, eo_constructor());
   eo_do_super(obj, SIMPLE_CLASS, eo_destructor());

   eo_unref(obj);

   eo_shutdown();
   return 0;
}

