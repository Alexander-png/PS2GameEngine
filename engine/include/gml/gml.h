/* gml/gml.h - GameMaker language runtime API used by converter output (converter/gml2c.py) and implemented in engine/core/gml_*.c.
 *
 * Values: gm_val is a tagged 16-byte value (real, string, undefined, ...). All numbers are `gm_real_t` (double on the host;
 * the PS2 EE has no double FPU - the typedef is the one switch to try float later).
 *
 * Strings are immutable. gm_str.rc: -1 constant (static), 0 scratch (frame arena, reset every frame), >0 heap refcount.
 * Storing a string into a variable/array/ds promotes scratch strings to the heap; freeing a heap string is deferred to
 * the end of the frame, so a value read onto the operand stack stays valid until the frame ends.
 */
#ifndef GM_GML_H
#define GM_GML_H
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifdef GM_REAL_FLOAT                  /* PS2: the EE has a single-precision FPU only; double would be software emulated */
typedef float gm_real_t;
#define sin sinf
#define cos cosf
#define atan2 atan2f
#define sqrt sqrtf
#define pow powf
#define floor floorf
#define ceil ceilf
#define fabs fabsf
#define fmod fmodf
#define rint rintf
#else
typedef double gm_real_t;
#endif
typedef struct gm_inst gm_inst;
typedef struct gm_str { int32_t rc, len; const char *s; } gm_str;
enum { GM_UNDEF = 0, GM_REAL, GM_STR, GM_ARR, GM_PTR };
typedef struct gm_arr gm_arr;
typedef struct gm_val {
    uint8_t t;
    union { gm_real_t r; gm_str *s; gm_arr *a; void *p; } u;
} gm_val;

#define GM_UNDEF_V ((gm_val){GM_UNDEF, {0}})
#define GM_WEAK __attribute__((weak))
#define GM_GLOBAL (gm_global_inst)
extern gm_inst *gm_global_inst;                 /* pseudo instance holding global variables */

static inline gm_val gm_real(gm_real_t r) { gm_val v; v.t = GM_REAL; v.u.r = r; return v; }
extern gm_str gm_strs[];                        /* constant strings of the game (gm_tables.c) */
static inline gm_val gm_const_str(int idx) { gm_val v; v.t = GM_STR; v.u.s = &gm_strs[idx]; return v; }
static inline gm_val gm_str_val(gm_str *s) { gm_val v; v.t = GM_STR; v.u.s = s; return v; }

int gm_truth_slow(gm_val v);
static inline int gm_truth(gm_val v) { return v.t == GM_REAL ? v.u.r > 0.5 : gm_truth_slow(v); }
#define GM_T(v) gm_truth(v)

/* ---- operators (stack machine: a = lower operand, b = top) ---- */
gm_val gm_add_slow(gm_val a, gm_val b);
gm_val gm_arith_slow(int op, gm_val a, gm_val b);
static inline gm_val gm_add(gm_val a, gm_val b) { return (a.t == GM_REAL && b.t == GM_REAL) ? gm_real(a.u.r + b.u.r) : gm_add_slow(a, b); }
static inline gm_val gm_sub(gm_val a, gm_val b) { return (a.t == GM_REAL && b.t == GM_REAL) ? gm_real(a.u.r - b.u.r) : gm_arith_slow(1, a, b); }
static inline gm_val gm_mul(gm_val a, gm_val b) { return (a.t == GM_REAL && b.t == GM_REAL) ? gm_real(a.u.r * b.u.r) : gm_arith_slow(2, a, b); }
gm_val gm_div(gm_val a, gm_val b);              /* a / b (b == 0: runtime error, result 0 like GM) */
gm_val gm_idiv(gm_val a, gm_val b);             /* integer division (GML `div`, bytecode rem) */
gm_val gm_mod(gm_val a, gm_val b);
gm_val gm_and(gm_val a, gm_val b);
gm_val gm_or(gm_val a, gm_val b);
gm_val gm_xor(gm_val a, gm_val b);
gm_val gm_shl(gm_val a, gm_val b);
gm_val gm_shr(gm_val a, gm_val b);
gm_val gm_neg(gm_val a);
gm_val gm_lnot(gm_val a);
gm_val gm_bnot(gm_val a);
gm_val gm_cmp_slow(int op, gm_val a, gm_val b);
/* GameMaker compares reals with an epsilon (1e-5): a cutscene waiting for `image_index == 2` after ten additions of 0.2 depends on it. */
#define GM_EPS ((gm_real_t)0.00001)
static inline int gm_rcmp(gm_real_t x, gm_real_t y) { gm_real_t d = x - y; return d < -GM_EPS ? -1 : (d > GM_EPS ? 1 : 0); }
#define GM_CMP_FAST(opc, c, a, b) (((a).t == GM_REAL && (b).t == GM_REAL) ? gm_real(gm_rcmp((a).u.r, (b).u.r) c 0) : gm_cmp_slow(opc, a, b))
static inline gm_val gm_lt(gm_val a, gm_val b) { return GM_CMP_FAST(1, <, a, b); }
static inline gm_val gm_le(gm_val a, gm_val b) { return GM_CMP_FAST(2, <=, a, b); }
static inline gm_val gm_eq(gm_val a, gm_val b) { return GM_CMP_FAST(3, ==, a, b); }
static inline gm_val gm_ne(gm_val a, gm_val b) { return GM_CMP_FAST(4, !=, a, b); }
static inline gm_val gm_ge(gm_val a, gm_val b) { return GM_CMP_FAST(5, >=, a, b); }
static inline gm_val gm_gt(gm_val a, gm_val b) { return GM_CMP_FAST(6, >, a, b); }
gm_val gm_conv_i(gm_val a);
gm_val gm_conv_b(gm_val a);
gm_val gm_conv_s(gm_val a);

/* ---- variables ---- */
gm_val gm_get(gm_inst *i, int vid);                          /* NULL instance -> undefined */
void   gm_set(gm_inst *i, int vid, gm_val v);
void   gm_set_obj(int obj, int vid, gm_val v);                      /* obj.var = v : every instance of the object */
void   gm_seta_obj(int obj, int vid, gm_val index, gm_val v);
gm_val gm_geta(gm_inst *i, int vid, gm_val index);
void   gm_seta(gm_inst *i, int vid, gm_val index, gm_val v);
gm_val gm_geta_v(gm_val *slot, gm_val index);                /* array held in a local */
void   gm_seta_v(gm_val *slot, gm_val index, gm_val v);
gm_val gm_keep(gm_val v);                                    /* value stored into a local: arrays are copied, strings stay */
gm_inst *gm_res(gm_val who, gm_inst *self, gm_inst *other);  /* instance operand -> instance (id, object index, -1 self, -2 other, -5 global) */
gm_inst *gm_obj1(int object_index);                          /* first instance of an object (or a child object) */
#define GM_ARG(n) ((argc) > (n) ? argv[n] : GM_UNDEF_V)
gm_val gm_arg_dyn(int argc, const gm_val *argv, gm_val idx);

/* ---- with ---- */
typedef struct gm_with { gm_inst **list; int n, i; gm_inst *save_self, *save_other; } gm_with_t;
int  gm_with_begin(gm_with_t *w, gm_val who, gm_inst **self, gm_inst **other);   /* 0: no instance -> skip the body */
int  gm_with_next(gm_with_t *w, gm_inst **self, gm_inst **other);               /* 1: continue with the next instance */
void gm_with_end(gm_with_t *w, gm_inst **self, gm_inst **other);

/* ---- functions ---- */
typedef gm_val (*gm_fn)(gm_inst *self, gm_inst *other, int argc, const gm_val *argv);
typedef struct gm_script_def { const char *name; gm_fn fn; } gm_script_def;
extern const gm_fn gm_ftab[];                   /* indexed by FUNC chunk index: user scripts or runtime builtins gmb_<name> */
extern const char *const gm_func_names[];
extern const int gm_nfuncs, gm_nvars, gm_nstrs, gm_nscripts;
extern const char *const gm_var_names[];
extern const gm_script_def gm_scripts[];
gm_val gm_unimpl(const char *name);             /* called by the weak stub of a builtin that is not implemented; logs once */
void   gm_unsupported(const char *entry);
gm_val gm_callv(gm_val fn, gm_val inst, gm_inst *self, gm_inst *other, int argc, const gm_val *argv);

/* GMS2.3+ break sub-operations (array references, statics, refs) */
gm_val gm_break_pushaf(gm_val index, gm_val arr, gm_inst *self, gm_inst *other);
void   gm_break_popaf(gm_val index, gm_val arr, gm_val v, gm_inst *self, gm_inst *other);
gm_val gm_break_pushac(gm_val a, gm_inst *self, gm_inst *other);
gm_val gm_break_pushref(gm_inst *self, gm_inst *other);
void   gm_break_savearef(gm_inst *self, gm_inst *other);
void   gm_break_restorearef(gm_inst *self, gm_inst *other);
void   gm_break_chkindex2(gm_inst *self, gm_inst *other);

#endif
