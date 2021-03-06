#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "eo_simple.h"
#include "eo_inherit.h"
#include "eo2_simple.h"
#include "eo3_simple.h"
#include "eo2_inherit.h"

static void report(struct timespec t0, struct timespec t1,
                   struct timespec t2, struct timespec t3, int n, int c)
{
   uint64_t dt0, dt1;

   dt0 = ((t1.tv_sec * 1000000000ULL) + t1.tv_nsec) -
      ((t0.tv_sec * 1000000000ULL) + t0.tv_nsec);
   dt1 = ((t3.tv_sec * 1000000000ULL) + t3.tv_nsec) -
      ((t2.tv_sec * 1000000000ULL) + t2.tv_nsec);

   printf("   #%d              %3u %3u                %3u %3u\n",
          n, (unsigned int)(dt0/1000000), (unsigned int)(dt0/c),
          (unsigned int)(dt1/1000000),(unsigned int)(dt1/c));
}

static void check(int val, int expected)
{
   static int count = 0;
   count++;
   if (val != expected)
     {
        fprintf(stderr, "check #%d failed %d != %d\n", count, val, expected);
        exit(count);
     }
}

#define EO_RUN_START                      \
   eo_do(eo_obj, eo_set(0));              \
   clock_gettime(CLOCK_MONOTONIC, &t0);

#define EO_RUN_END                        \
   clock_gettime(CLOCK_MONOTONIC, &t1);   \
   eo_do(eo_obj, eo_get(&v));             \
   check(v, n * k);                       \

#define EO2_RUN_START                     \
  eo2_do(eo2_obj, simple_set(0); );       \
   clock_gettime(CLOCK_MONOTONIC, &t2);   \

#define EO2_RUN_END                       \
   clock_gettime(CLOCK_MONOTONIC, &t3);   \
   eo2_do(eo2_obj, v = simple_get(); );      \
   check(v, n * k);                       \

static void
run_batch(const char *title, Eo* eo_obj, Eo* eo2_obj, int n)
{
   int i, k, v;
   Eina_Bool eo1;
   struct timespec t0, t1, t2, t3;

   if (getenv("EO2ONLY") != NULL)
     {
        eo1 = EINA_FALSE;
        t0.tv_sec = t1.tv_sec = 0;
        t0.tv_nsec = t1.tv_nsec = 0;
     }
   else
     eo1 = EINA_TRUE;

   printf("\n%s - %d calls\ncalls/eo_do()  EO [ms] [ns]/call  - EO2 [ms] [ns]/call\n", title, n);

   /* 1 call per batch */
   k = 1;
   if (eo1)
     {
        EO_RUN_START
           for (i = 0; i < n; i++)
             eo_do(eo_obj, eo_inc());
        EO_RUN_END
     }
   EO2_RUN_START
   for (i = 0; i < n; i++)
     eo2_do(eo2_obj, simple_inc(); );
   EO2_RUN_END
   report(t0, t1, t2, t3, k, n * k);

   /* 3 calls per batch */
   k = 3;
   if (eo1)
     {
        EO_RUN_START
           for (i = 0; i < n; i++)
             eo_do(eo_obj, eo_inc(), eo_inc(), eo_inc());
        EO_RUN_END
     }
   EO2_RUN_START
   for (i = 0; i < n; i++)
     eo2_do(eo2_obj, simple_inc(); simple_inc(); simple_inc(); );
   EO2_RUN_END
   report(t0, t1, t2, t3, k, n * k);

   /* 5 calls per batch */
   k = 5;
   if (eo1)
     {
        EO_RUN_START
           for (i = 0; i < n; i++)
             eo_do(eo_obj, eo_inc(), eo_inc(), eo_inc(), eo_inc(), eo_inc());
        EO_RUN_END
     }
   EO2_RUN_START
   for (i = 0; i < n; i++)
     eo2_do(eo2_obj, simple_inc(); simple_inc(); simple_inc(); simple_inc(); simple_inc(); );
   EO2_RUN_END
   report(t0, t1, t2, t3, k, n * k);

   /* 7 calls per batch */
   k = 7;
   if (eo1)
     {
        EO_RUN_START
           for (i = 0; i < n; i++)
             eo_do(eo_obj, eo_inc(), eo_inc(), eo_inc(), eo_inc(), eo_inc(), eo_inc(), eo_inc());
        EO_RUN_END
     }
   EO2_RUN_START
   for (i = 0; i < n; i++)
     eo2_do(eo2_obj, simple_inc(); simple_inc(); simple_inc(); simple_inc(); simple_inc(); simple_inc(); simple_inc(); );
   EO2_RUN_END
   report(t0, t1, t2, t3, k, n * k);
}

static void
do_batch_test()
{
   int a, b, c;
   Eo *eo_obj, *eo2_obj;

   eo_obj = eo_add(EO_SIMPLE_CLASS, NULL);
   eo2_obj = eo2_add_custom(EO3_GET_CLASS(EO3_SIMPLE_CLASS)/*EO2_SIMPLE_CLASS*/, NULL, /*eo2_*/simple_constructor(66));

   /* EO check */
   a = b = c = 0;
   eo_do(eo_obj, eo_get(&a), eo_set(10), eo_inc(), eo_get(&b), eo_inc(), eo_inc(), eo_get(&c));
   check(a, 66);
   check(b, 11);
   check(c, 13);
   /* EO2 check */
   a = b = c = 0;
   eo2_do(eo2_obj,
          a = simple_get();
          simple_set(10);
          simple_inc();
          b = simple_get();
          simple_inc();
          simple_inc();
          c = simple_get();
          );
   check(a, 66);
   check(b, 11);
   check(c, 13);

   run_batch("simple inc()", eo_obj, eo2_obj, 99999);

   eo_del(eo_obj);
   eo_del(eo2_obj);
}

/* static void */
/* override_batch_test() */
/* { */
/*    int a, b; */
/*    Eo *eo_obj, *eo2_obj; */

/*    a = b = 0; */
/*    eo_obj = eo_add(EO_INHERIT_CLASS, NULL); */
/*    eo2_obj = eo2_add(EO2_INHERIT_CLASS, NULL); */

/*    /\* EO check *\/ */
/*    eo_do(eo_obj, eo_set(65), eo_get(&a), eo_inherit_get(&b)); */
/*    check(a, 65); */
/*    check(b, 68); */
/*    eo_do(eo_obj, eo_inc(), eo_get(&a), eo_inherit_get(&b)); */
/*    check(a, 66); */
/*    check(b, 69); */

/*    /\* EO2 check *\/ */
/*    eo2_do(eo2_obj, eo2_set(65); a = eo2_get(); b = eo2_inherit_get(); ); */
/*    check(a, 65); */
/*    check(b, 68); */
/*    eo2_do(eo2_obj, eo2_inc(); a = eo2_get(); b = eo2_inherit_get(); ); */
/*    check(a, 66); */
/*    check(b, 69); */

/*    run_batch("overriden inc", eo_obj, eo2_obj, 99999); */

/*    eo_del(eo_obj); */
/*    eo_del(eo2_obj); */
/* } */

/* static void */
/* virtual_test() */
/* { */
/*    int a; */
/*    Eo *eo2_obj; */

/*    a = 0; */
/*    eo2_obj = eo2_add_custom(EO2_SIMPLE_CLASS, NULL, eo2_simple_constructor(66)); */
/*    eo2_do(eo2_obj, a = eo2_virtual(10); ); */
/*    check(a, -1); */
/*    eo_del(eo2_obj); */

/*    a = 0; */
/*    eo2_obj = eo2_add(EO2_INHERIT_CLASS, NULL); */
/*    eo2_do(eo2_obj, a = eo2_virtual(10); ); */
/*    check(a, 20); */
/*    eo_del(eo2_obj); */
/* } */

static void inner_return(Eo *eo2_obj)
{
   eo2_do(eo2_obj,
          simple_set(0);
          return;
          simple_inc();
          );
}

static int
cleanup_test()
{
   int a;
   Eo *eo2_obj;

   eo2_obj = eo2_add_custom(EO3_GET_CLASS(EO3_SIMPLE_CLASS)/*EO2_SIMPLE_CLASS*/, NULL, /*eo2_*/simple_constructor(66));

   /* break */
   a = 0;
   check(eo2_call_stack_depth(), 0);
   eo2_do(eo2_obj,
          simple_set(0);
          check(eo2_call_stack_depth(), 1);
          a = simple_get();
          break;
          simple_inc();
          a = simple_get();
          );
   check(a, 0);
   check(eo2_call_stack_depth(), 0);

   /* return */
   a = 0;
   check(eo2_call_stack_depth(), 0);
   inner_return(eo2_obj);
   eo2_do(eo2_obj, a = simple_get(); );
   check(a, 0);
   check(eo2_call_stack_depth(), 0);

   /* goto */
   a = 0;
   check(eo2_call_stack_depth(), 0);
   eo2_do(eo2_obj,
          simple_set(0);
          a = simple_get();
          check(eo2_call_stack_depth(), 1);
          goto check;
          simple_inc();
          a = simple_get();
          );

check:
   check(a, 0);
   check(eo2_call_stack_depth(), 0);

   /* wrong object */
   check(eo2_call_stack_depth(), 0);
   // segfault if eo2_do_end is called !!
   eo2_do((Eo *)69, simple_set(0););

   eo_del(eo2_obj);

   return 0;
}

int
main(int argc EINA_UNUSED, char** argv EINA_UNUSED, char** env EINA_UNUSED)
{
   eo_init();

   do_batch_test();
   /* override_batch_test(); */
   /* virtual_test(); */
   /* if(cleanup_test()) */
   /*   printf("something went wrong in cleanup_test()\n"); */

   /* eo2_class_do(EO2_INHERIT_CLASS, eo2_class_hello(2); ); */

   eo_shutdown();

   return 0;
}

