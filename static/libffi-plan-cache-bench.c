/* A plain libffi program -- ordinary ffi_prep_cif + ffi_call, the way every
   binding uses it.  Same source links against old and new libffi; the only
   difference is the library.  Reports steady-state ns/call per shape. */
#include <ffi.h>
#include <stdio.h>
#include <time.h>

static long c_ptr3 (void *a, void *b, void *c) { return (long)a + (long)b + (long)c; }
static long c_gint (void *a, int b, void *c)   { return (long)a + b + (long)c; }
static double c_dbl3 (double a, double b, double c) { return a + b + c; }
typedef struct { long x, y; } S2;
static long c_sarg (S2 s, long z) { return s.x + s.y + z; }
static void h_gint (ffi_cif *cf, void *r, void **a, void *u) {
  (void)cf;(void)u; *(ffi_arg*)r = (long)*(void**)a[0] + *(int*)a[1] + (long)*(void**)a[2];
}

static double ns (void) {
  struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec*1e9 + (double)t.tv_nsec;
}

#define N 50000000L
#define TIME(label, CALL) do {                                  \
    long i; for (i=0;i<2000000;i++) { CALL; }                   \
    double t0=ns(); for (i=0;i<N;i++) { CALL; acc+=rv; }        \
    printf ("%-22s %7.2f\n", label, (ns()-t0)/(double)N);       \
  } while (0)

int main (void) {
  long acc = 0, rv; double drv;
  void *p=(void*)0x1111,*q=(void*)0x2222,*r=(void*)0x3333; int iv=-9;
  double d1=1.5,d2=2.25,d3=3.125;
  S2 s={5,9}; long z=3;

  printf ("%-22s %7s\n", "shape", "ns/call");

  { ffi_type *at[]={&ffi_type_pointer,&ffi_type_pointer,&ffi_type_pointer};
    ffi_cif cif; ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_sint64,at);
    void *av[]={&p,&q,&r};
    TIME("forward ptr(p,p,p)", ffi_call(&cif,(void(*)(void))c_ptr3,&rv,av)); }

  { ffi_type *at[]={&ffi_type_pointer,&ffi_type_sint,&ffi_type_pointer};
    ffi_cif cif; ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_sint64,at);
    void *av[]={&p,&iv,&r};
    TIME("forward (p,gint,p)", ffi_call(&cif,(void(*)(void))c_gint,&rv,av)); }

  { ffi_type *at[]={&ffi_type_double,&ffi_type_double,&ffi_type_double};
    ffi_cif cif; ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_double,at);
    void *av[]={&d1,&d2,&d3};
    long i; for(i=0;i<2000000;i++) ffi_call(&cif,(void(*)(void))c_dbl3,&drv,av);
    double t0=ns(); for(i=0;i<N;i++){ ffi_call(&cif,(void(*)(void))c_dbl3,&drv,av); acc+=(long)drv; }
    printf("%-22s %7.2f\n","forward (d,d,d)",(ns()-t0)/(double)N); }

  { ffi_type *el[]={&ffi_type_sint64,&ffi_type_sint64,NULL};
    ffi_type st={0,0,FFI_TYPE_STRUCT,el};
    ffi_type *at[]={&st,&ffi_type_sint64};
    ffi_cif cif; ffi_prep_cif(&cif,FFI_DEFAULT_ABI,2,&ffi_type_sint64,at);
    void *av[]={&s,&z};
    TIME("forward struct-by-val", ffi_call(&cif,(void(*)(void))c_sarg,&rv,av)); }

  { ffi_type *at[]={&ffi_type_pointer,&ffi_type_sint,&ffi_type_pointer};
    ffi_cif cif; ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_sint64,at);
    void *code; ffi_closure *cl=ffi_closure_alloc(sizeof(ffi_closure),&code);
    ffi_prep_closure_loc(cl,&cif,h_gint,NULL,code);
    long (*f)(void*,int,void*)=(long(*)(void*,int,void*))code;
    TIME("closure (p,gint,p)", rv=f(p,iv,r)); }

  return (int)acc;
}
