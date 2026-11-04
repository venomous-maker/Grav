// The runtime prelude: a small, dependency-light block of C emitted at the top of
// every generated program (string helpers, RTTI, call-stack tracing, exceptions).
#include "codegen/codegen.h"

namespace grav {

void CodeGen::emitPrelude() {
    typedefs_ += "/* Generated from Grav source by gravc. Do not edit. */\n";
    // Release builds compile the call-stack tracing away entirely.
    if (!trace_) typedefs_ += "#define GRAV_NO_TRACE 1\n";
    typedefs_ += R"C(#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <unistd.h>
#include <pthread.h>

#if defined(__GNUC__) && !defined(__clang__)
/* Locals at real risk across setjmp/longjmp are declared volatile by the
   compiler; GCC's remaining -Wclobbered reports (loop counters around a `try`
   that re-runs setjmp every iteration) are false positives. */
#pragma GCC diagnostic ignored "-Wclobbered"
#endif
#if defined(__GNUC__) || defined(__clang__)
#define GRAV_LIKELY(x) __builtin_expect(!!(x), 1)
#define GRAV_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define GRAV_NORETURN __attribute__((noreturn))
#define GRAV_COLD __attribute__((cold, noinline))
#define GRAV_UNUSED __attribute__((unused))
#else
#define GRAV_UNUSED
#define GRAV_LIKELY(x) (x)
#define GRAV_UNLIKELY(x) (x)
#define GRAV_NORETURN
#define GRAV_COLD
#endif

/* Grav has no garbage collector, so objects alive at exit are expected: keep
   AddressSanitizer (`--sanitize`) focused on real memory errors. */
#if defined(__SANITIZE_ADDRESS__)
const char* __asan_default_options(void) { return "detect_leaks=0"; }
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
const char* __asan_default_options(void) { return "detect_leaks=0"; }
#endif
#endif

/* `grav test`: failures inside a test jump back to the runner instead of exiting. */
static _Thread_local int grav_testing = 0; /* runner thread only */
static jmp_buf grav_test_jmp;
static char grav_test_msg[2048];
static GRAV_COLD GRAV_NORETURN void grav_test_fail(const char* m) {
    snprintf(grav_test_msg, sizeof grav_test_msg, "%s", m);
    longjmp(grav_test_jmp, 1);
}
static GRAV_COLD GRAV_NORETURN void grav_panic(const char* m) {
    if (grav_testing) {
        char b[2048]; snprintf(b, sizeof b, "panic: %s", m); grav_test_fail(b);
    }
    fflush(stdout); fprintf(stderr, "panic: %s\n", m); exit(1);
}
static inline void grav_assert(bool c, const char* m) {
    if (GRAV_UNLIKELY(!c)) {
        if (grav_testing) grav_test_fail(m);
        fflush(stdout); fprintf(stderr, "assertion failed: %s\n", m); exit(1);
    }
}
static GRAV_COLD GRAV_NORETURN void grav_assert_fail_at(const char* m, const char* loc) {
    char b[2048]; snprintf(b, sizeof b, "%s (%s)", m, loc);
    if (grav_testing) grav_test_fail(b);
    fflush(stdout); fprintf(stderr, "assertion failed: %s\n", b); exit(1);
}
static inline void grav_assert_at(bool c, const char* m, const char* loc) {
    if (GRAV_UNLIKELY(!c)) grav_assert_fail_at(m, loc);
}
static void* grav_alloc(size_t n) {
    void* p = malloc(n ? n : 1);
    if (GRAV_UNLIKELY(!p)) grav_panic("out of memory");
    return p;
}
static void* grav_zalloc(size_t n) {
    void* p = calloc(1, n ? n : 1);
    if (GRAV_UNLIKELY(!p)) grav_panic("out of memory");
    return p;
}
static const char* grav_env(const char* n) { const char* v = getenv(n); return v ? v : ""; }
static const char* grav_cwd(void) {
    static _Thread_local char b[4096]; return getcwd(b, sizeof b) ? b : "";
}

typedef struct GravTypeInfo {
    const char* name;
    const struct GravTypeInfo* base;            /* primary base */
    const struct GravTypeInfo* const* extra;    /* secondary bases (MI) */
    int nextra;
    void (*drop)(void*);                        /* destructor chain, or 0 */
    const char* (*str)(void*);                  /* toString(), or 0 */
    bool (*eq)(void*, void*);                   /* operator==(other), or 0 */
} GravTypeInfo;

/* Common object header: vtable pointer + runtime type descriptor. */
struct GravObject { const void* __vt; const GravTypeInfo* __type; };

/* A function value: code pointer + captured environment (0 when nothing is
   captured). Called as ((R (*)(void*, A...))f.fn)(f.env, a...). */
typedef struct GravFn { void (*fn)(void); void* env; } GravFn;
static inline bool grav_fn_eq(GravFn a, GravFn b) { return a.fn == b.fn && a.env == b.env; }

/* An interface value: the object plus its per-class method table. */
struct GravIface { void* obj; const void* itab; };

static inline bool grav_iface_eq(struct GravIface a, struct GravIface b) { return a.obj == b.obj; }

/* `free(obj)`: run the dynamic type's destructor chain, then release the memory. */
static void grav_free_obj(void* o) {
    if (!o) return;
    const GravTypeInfo* t = ((struct GravObject*)o)->__type;
    if (t && t->drop) t->drop(o);
    free(o);
}

/* A binary blob: byte buffer + length (may contain embedded NULs). */
typedef struct GravBytes { unsigned char* data; long long len; } GravBytes;
static GravBytes grav_bytes_from_str(const char* s) {
    long long n = (long long)strlen(s);
    unsigned char* d = (unsigned char*)grav_alloc((size_t)n + 1);
    memcpy(d, s, (size_t)n + 1); GravBytes b; b.data = d; b.len = n; return b;
}
static inline bool grav_bytes_eq(GravBytes a, GravBytes b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, (size_t)a.len) == 0);
}
static const char* grav_bytes_to_str(GravBytes b) {
    char* r = (char*)grav_alloc((size_t)b.len + 1);
    if (b.data) memcpy(r, b.data, (size_t)b.len);
    r[b.len] = 0; return r;
}

/* Strings. `+` chains are fused at compile time into one grav_concatn/grav_fmt
   call, so `a + b + c` costs a single allocation instead of one per `+`. */
static inline bool grav_streq(const char* a, const char* b) {
    return a == b || strcmp(a, b) == 0;
}
static const char* grav_strdup(const char* s) {
    size_t n = strlen(s);
    char* r = (char*)grav_alloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}
/* Compare, then release whichever operands were temporaries. */
static bool grav_streq_tmp(const char* a, const char* b, int freeA, int freeB) {
    /* no `a == b` shortcut: with it, GCC infers a fresh operand may be the other
       side's literal and warns about freeing it */
    bool r = strcmp(a, b) == 0;
    if (freeA) free((void*)a);
    if (freeB) free((void*)b);
    return r;
}
static const char* grav_str_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* r = (char*)grav_alloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}
static const char* grav_concatn(int n, ...) {
    size_t lens[16], total = 0; va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        size_t l = strlen(va_arg(ap, const char*));
        if (i < 16) lens[i] = l;
        total += l;
    }
    va_end(ap);
    char* r = (char*)grav_alloc(total + 1); char* w = r;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        const char* s = va_arg(ap, const char*);
        size_t l = i < 16 ? lens[i] : strlen(s);
        memcpy(w, s, l); w += l;
    }
    va_end(ap);
    *w = 0; return r;
}
/* printf-style formatting into a fresh heap string (one pass when it fits 256B). */
static const char* grav_fmt(const char* fmt, ...) {
    char buf[256]; va_list ap, ap2;
    va_start(ap, fmt); va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char* r = (char*)grav_alloc((size_t)n + 1);
    if ((size_t)n < sizeof buf) memcpy(r, buf, (size_t)n + 1);
    else vsnprintf(r, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return r;
}
/* Integer power by squaring (`a ** b` on ints; negative exponents yield 0). */
static inline long long grav_ipow(long long b, long long e) {
    if (e < 0) return b == 1 ? 1 : (b == -1 ? ((e & 1) ? -1 : 1) : 0);
    long long r = 1;
    while (e) { if (e & 1) r *= b; b *= b; e >>= 1; }
    return r;
}

static const char* grav_typename(const void* obj) {
    return ((const struct GravObject*)obj)->__type->name;
}

static bool grav_type_is(const GravTypeInfo* t, const GravTypeInfo* want) {
    while (t) {
        if (t == want) return true;
        for (int i = 0; i < t->nextra; i++)
            if (grav_type_is(t->extra[i], want)) return true;
        t = t->base;
    }
    return false;
}
static bool grav_is_instance(const void* obj, const GravTypeInfo* want) {
    return obj && grav_type_is(((const struct GravObject*)obj)->__type, want);
}

/* Value-to-string helpers (used by str()). */
static const char* grav_int_to_str(long long v) {
    char* b = (char*)grav_alloc(24); snprintf(b, 24, "%lld", v); return b;
}
static const char* grav_uint_to_str(unsigned long long v) {
    char* b = (char*)grav_alloc(24); snprintf(b, 24, "%llu", v); return b;
}
static const char* grav_float_to_str(double v) {
    char* b = (char*)grav_alloc(32); snprintf(b, 32, "%g", v); return b;
}

/* Command-line arguments and stdin. */
static int grav_argc = 0;
static char** grav_argv = 0;
static const char* grav_argv_at(int i) {
    return (i >= 0 && i < grav_argc) ? grav_argv[i] : "";
}
static const char* grav_input(void) {
    size_t cap = 128, n = 0; char* b = (char*)grav_alloc(cap); int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(b, cap);
            if (!nb) grav_panic("out of memory");
            b = nb;
        }
        b[n++] = (char)c;
    }
    if (c == EOF && n == 0) { free(b); return ""; }
    if (n > 0 && b[n - 1] == '\r') n--; /* tolerate CRLF input */
    b[n] = 0; return b;
}

/* `any`: a tagged box for a value of any boxable type. */
enum { GRAV_ANY_NULL, GRAV_ANY_INT, GRAV_ANY_UINT, GRAV_ANY_FLOAT, GRAV_ANY_BOOL, GRAV_ANY_STR,
       GRAV_ANY_OBJ, GRAV_ANY_ENUM, GRAV_ANY_FN };
typedef struct GravAny {
    int tag;
    const char* tname;                  /* enum / function type name */
    const char* (*ename)(long long);    /* enum member names */
    union { long long i; unsigned long long u; double f; bool b; const char* s; void* p; GravFn fn; } v;
} GravAny;
static inline GravAny grav_any_null(void) { GravAny a; memset(&a, 0, sizeof a); return a; }
static inline GravAny grav_any_int(long long x) { GravAny a = grav_any_null(); a.tag = GRAV_ANY_INT; a.v.i = x; return a; }
static inline GravAny grav_any_uint(unsigned long long x) { GravAny a = grav_any_null(); a.tag = GRAV_ANY_UINT; a.v.u = x; return a; }
static inline GravAny grav_any_float(double x) { GravAny a = grav_any_null(); a.tag = GRAV_ANY_FLOAT; a.v.f = x; return a; }
static inline GravAny grav_any_bool(bool x) { GravAny a = grav_any_null(); a.tag = GRAV_ANY_BOOL; a.v.b = x; return a; }
static inline GravAny grav_any_str(const char* x) { GravAny a = grav_any_null(); a.tag = GRAV_ANY_STR; a.v.s = x; return a; }
static inline GravAny grav_any_obj(void* x) {
    GravAny a = grav_any_null();
    if (x) { a.tag = GRAV_ANY_OBJ; a.v.p = x; }
    return a;
}
static inline GravAny grav_any_enum(long long x, const char* tname, const char* (*names)(long long)) {
    GravAny a = grav_any_null(); a.tag = GRAV_ANY_ENUM; a.v.i = x; a.tname = tname; a.ename = names; return a;
}
static inline GravAny grav_any_fn(GravFn f, const char* sig) {
    GravAny a = grav_any_null();
    if (f.fn) { a.tag = GRAV_ANY_FN; a.v.fn = f; a.tname = sig; }
    return a;
}
static const char* grav_any_typename(GravAny a) {
    switch (a.tag) {
        case GRAV_ANY_INT: return "int";
        case GRAV_ANY_UINT: return "uint";
        case GRAV_ANY_FLOAT: return "float";
        case GRAV_ANY_BOOL: return "bool";
        case GRAV_ANY_STR: return "string";
        case GRAV_ANY_OBJ: return ((const struct GravObject*)a.v.p)->__type->name;
        case GRAV_ANY_ENUM: case GRAV_ANY_FN: return a.tname;
        default: return "null";
    }
}
static GRAV_COLD GRAV_NORETURN void grav_any_bad(GravAny a, const char* want) {
    char msg[256];
    snprintf(msg, sizeof msg, "an 'any' holding %s cannot be used as %s (check with `is` first)",
             grav_any_typename(a), want);
    grav_panic(msg);
}
static long long grav_any_to_int(GravAny a) {
    if (a.tag == GRAV_ANY_INT) return a.v.i;
    if (a.tag == GRAV_ANY_UINT) return (long long)a.v.u;
    grav_any_bad(a, "an integer");
}
static unsigned long long grav_any_to_uint(GravAny a) {
    if (a.tag == GRAV_ANY_UINT) return a.v.u;
    if (a.tag == GRAV_ANY_INT) return (unsigned long long)a.v.i;
    grav_any_bad(a, "an integer");
}
static double grav_any_to_float(GravAny a) {
    if (a.tag == GRAV_ANY_FLOAT) return a.v.f;
    if (a.tag == GRAV_ANY_INT) return (double)a.v.i;
    if (a.tag == GRAV_ANY_UINT) return (double)a.v.u;
    grav_any_bad(a, "a float");
}
static bool grav_any_to_bool(GravAny a) { if (a.tag == GRAV_ANY_BOOL) return a.v.b; grav_any_bad(a, "a bool"); }
static const char* grav_any_to_str(GravAny a) { if (a.tag == GRAV_ANY_STR) return a.v.s; grav_any_bad(a, "a string"); }
static long long grav_any_to_enum(GravAny a, const char* name) {
    if (a.tag == GRAV_ANY_ENUM && strcmp(a.tname, name) == 0) return a.v.i;
    grav_any_bad(a, name);
}
static void* grav_any_to_obj(GravAny a, const GravTypeInfo* want) {
    if (a.tag == GRAV_ANY_NULL) return 0;
    if (a.tag == GRAV_ANY_OBJ && grav_type_is(((const struct GravObject*)a.v.p)->__type, want)) return a.v.p;
    grav_any_bad(a, want->name);
}
static GravFn grav_any_to_fn(GravAny a, const char* sig) {
    if (a.tag == GRAV_ANY_NULL) { GravFn z = {0, 0}; return z; }
    if (a.tag == GRAV_ANY_FN && strcmp(a.tname, sig) == 0) return a.v.fn;
    grav_any_bad(a, sig);
}
static inline bool grav_any_is_int(GravAny a) { return a.tag == GRAV_ANY_INT || a.tag == GRAV_ANY_UINT; }
static inline bool grav_any_is_float(GravAny a) { return a.tag == GRAV_ANY_FLOAT; }
static inline bool grav_any_is_bool(GravAny a) { return a.tag == GRAV_ANY_BOOL; }
static inline bool grav_any_is_str(GravAny a) { return a.tag == GRAV_ANY_STR; }
static inline bool grav_any_is_enum(GravAny a, const char* n) { return a.tag == GRAV_ANY_ENUM && strcmp(a.tname, n) == 0; }
static inline bool grav_any_is_fn(GravAny a, const char* s) { return a.tag == GRAV_ANY_FN && strcmp(a.tname, s) == 0; }
static inline bool grav_any_is_obj(GravAny a, const GravTypeInfo* want) {
    return a.tag == GRAV_ANY_OBJ && grav_type_is(((const struct GravObject*)a.v.p)->__type, want);
}
static const char* grav_any_to_string(GravAny a) {
    switch (a.tag) {
        case GRAV_ANY_INT: return grav_int_to_str(a.v.i);
        case GRAV_ANY_UINT: return grav_uint_to_str(a.v.u);
        case GRAV_ANY_FLOAT: return grav_float_to_str(a.v.f);
        case GRAV_ANY_BOOL: return a.v.b ? "true" : "false";
        case GRAV_ANY_STR: return a.v.s;
        case GRAV_ANY_OBJ: {
            const GravTypeInfo* t = ((const struct GravObject*)a.v.p)->__type;
            return t->str ? t->str(a.v.p) : t->name;   /* the dynamic class's toString() */
        }
        case GRAV_ANY_ENUM: return a.ename ? a.ename(a.v.i) : a.tname;
        case GRAV_ANY_FN: return a.tname;
        default: return "null";
    }
}
static void grav_assert_eq(GravAny actual, GravAny expected, const char* msg, const char* loc);
static bool grav_any_eq(GravAny a, GravAny b) {
    bool an = a.tag == GRAV_ANY_INT || a.tag == GRAV_ANY_UINT || a.tag == GRAV_ANY_FLOAT;
    bool bn = b.tag == GRAV_ANY_INT || b.tag == GRAV_ANY_UINT || b.tag == GRAV_ANY_FLOAT;
    if (an && bn) {
        if (a.tag == GRAV_ANY_FLOAT || b.tag == GRAV_ANY_FLOAT)
            return grav_any_to_float(a) == grav_any_to_float(b);
        return a.v.i == b.v.i;
    }
    if (a.tag != b.tag) return false;
    switch (a.tag) {
        case GRAV_ANY_NULL: return true;
        case GRAV_ANY_BOOL: return a.v.b == b.v.b;
        case GRAV_ANY_STR: return grav_streq(a.v.s, b.v.s);
        case GRAV_ANY_OBJ: { /* the class's operator== when it has one */
            if (a.v.p == b.v.p) return true;
            const GravTypeInfo* t = ((const struct GravObject*)a.v.p)->__type;
            return t->eq ? t->eq(a.v.p, b.v.p) : false;
        }
        case GRAV_ANY_ENUM: return strcmp(a.tname, b.tname) == 0 && a.v.i == b.v.i;
        case GRAV_ANY_FN: return grav_fn_eq(a.v.fn, b.v.fn);
        default: return false;
    }
}

/* Call stack for diagnostics: a frame per active function (name+file+line).
   `grav_top` points at the innermost frame so a line mark is a single store. */
typedef struct GravFrame { const char* fn; const char* file; int line; } GravFrame;
#define GRAV_MAXFRAMES 512
/* Per thread: every thread (main and async workers) has its own call stack. */
static _Thread_local GravFrame grav_frames[GRAV_MAXFRAMES + 1]; /* last slot absorbs overflow */
static _Thread_local int grav_nframes = 0;
static _Thread_local GravFrame* grav_top = 0; /* set by the first grav_enter */
#ifdef GRAV_NO_TRACE
#define grav_enter(fn, file) ((void)0)
#define grav_leave() ((void)0)
#define GRAV_LINE(n) ((void)0)
#define grav_frames_restore(n) ((void)(n))
#else
static inline void grav_enter(const char* fn, const char* file) {
    GravFrame* f = &grav_frames[grav_nframes < GRAV_MAXFRAMES ? grav_nframes : GRAV_MAXFRAMES];
    f->fn = fn; f->file = file; f->line = 0;
    grav_top = f;
    grav_nframes++;
}
static inline void grav_frames_restore(int n) {
    grav_nframes = n;
    grav_top = &grav_frames[n == 0 ? GRAV_MAXFRAMES : (n <= GRAV_MAXFRAMES ? n - 1 : GRAV_MAXFRAMES)];
}
static inline void grav_leave(void) { if (grav_nframes > 0) grav_frames_restore(grav_nframes - 1); }
#define GRAV_LINE(n) (grav_top->line = (n))
#endif

/* Exceptions: a jmp_buf stack, the in-flight exception, and a captured trace. */
typedef struct GravExc {
    void* value; const GravTypeInfo* type; const char* message;
    GravFrame trace[GRAV_MAXFRAMES]; int ntrace;
} GravExc;
#define GRAV_MAXTRY 256
/* Per thread: handlers and the in-flight exception never leak across threads. */
static _Thread_local jmp_buf grav_jmp_stack[GRAV_MAXTRY];
static _Thread_local int grav_jmp_top = 0;
static _Thread_local GravExc grav_current_exc;
static inline int grav_try_push(void) {
    if (GRAV_UNLIKELY(grav_jmp_top >= GRAV_MAXTRY)) grav_panic("try blocks nested too deeply");
    return grav_jmp_top++;
}
static void grav_print_trace(const GravFrame* fr, int n) {
    if (n <= 0) return;
    fprintf(stderr, "  stack trace (most recent call last):\n");
    if (n > GRAV_MAXFRAMES) n = GRAV_MAXFRAMES;
    for (int i = 0; i < n; i++)
        fprintf(stderr, "    at %s (%s:%d)\n",
                fr[i].fn ? fr[i].fn : "?", fr[i].file ? fr[i].file : "?", fr[i].line);
}
static GRAV_NORETURN void grav_throw(void* v, const GravTypeInfo* t, const char* msg) {
    grav_current_exc.value = v; grav_current_exc.type = t; grav_current_exc.message = msg;
#ifndef GRAV_NO_TRACE
    /* Snapshot the live call stack at the throw site (before any unwinding). */
    {
        int n = grav_nframes > GRAV_MAXFRAMES ? GRAV_MAXFRAMES : grav_nframes;
        memcpy(grav_current_exc.trace, grav_frames, (size_t)n * sizeof(GravFrame));
        grav_current_exc.ntrace = n;
    }
#endif
    if (grav_jmp_top > 0) longjmp(grav_jmp_stack[grav_jmp_top - 1], 1);
    if (grav_testing) {
        char b[2048];
        snprintf(b, sizeof b, "uncaught exception: %s", (msg && msg[0]) ? msg : (t ? t->name : "?"));
        grav_test_fail(b);
    }
    fflush(stdout);
    fprintf(stderr, "grav: uncaught exception: %s\n",
            (msg && msg[0]) ? msg : (t ? t->name : "?"));
    grav_print_trace(grav_current_exc.trace, grav_current_exc.ntrace);
    exit(1);
}
/* Re-raise the in-flight exception, keeping the trace from its original throw. */
static GRAV_NORETURN void grav_rethrow(void) {
    if (grav_jmp_top > 0) longjmp(grav_jmp_stack[grav_jmp_top - 1], 1);
    if (grav_testing) {
        char b[2048];
        snprintf(b, sizeof b, "uncaught exception: %s", (grav_current_exc.message && grav_current_exc.message[0])
                 ? grav_current_exc.message : (grav_current_exc.type ? grav_current_exc.type->name : "?"));
        grav_test_fail(b);
    }
    fflush(stdout);
    fprintf(stderr, "grav: uncaught exception: %s\n",
            (grav_current_exc.message && grav_current_exc.message[0])
                ? grav_current_exc.message
                : (grav_current_exc.type ? grav_current_exc.type->name : "?"));
    grav_print_trace(grav_current_exc.trace, grav_current_exc.ntrace);
    exit(1);
}

/* ---- async tasks: a worker pool of GRAV_MAXPROCS threads ------------------
   An async call becomes a task queued for the pool; `await` waits for it. A task
   that is awaited before any worker picked it up runs on the awaiting thread,
   so a bounded pool can never deadlock on tasks waiting for tasks. An exception
   escaping a task is stored and re-thrown by `await`. */
typedef struct GravTask {
    void (*run)(struct GravTask*);   /* executes the body, storing the result */
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int state;                       /* 0 queued, 1 running, 2 done */
    int failed, awaited;
    GravExc* exc;                    /* heap copy of the escaped exception */
    void* result;                    /* the result slot inside the task record */
    int refs;                        /* workers holding it (guarded by grav_pool_mu) */
    struct GravTask* next;           /* run-queue link */
    struct GravTask* allNext;        /* every task, for the join at exit */
} GravTask;

static pthread_mutex_t grav_pool_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t grav_pool_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t grav_refs_cv = PTHREAD_COND_INITIALIZER;  /* a worker released a task */
static GravTask *grav_q_head = 0, *grav_q_tail = 0, *grav_all_tasks = 0;
static int grav_workers = 0, grav_maxprocs = 0;

/* Worker count: GRAV_MAXPROCS from the environment, else the CPU count. */
static int grav_max_procs(void) {
    pthread_mutex_lock(&grav_pool_mu);
    if (grav_maxprocs <= 0) {
        const char* e = getenv("GRAV_MAXPROCS");
        int n = e ? atoi(e) : 0;
        if (n <= 0) n = (int)sysconf(_SC_NPROCESSORS_ONLN);
        grav_maxprocs = n > 0 ? n : 1;
    }
    int n = grav_maxprocs;
    pthread_mutex_unlock(&grav_pool_mu);
    return n;
}
/* Change the limit; the pool grows lazily up to it (it never shrinks). */
static void grav_set_max_procs(int n) {
    pthread_mutex_lock(&grav_pool_mu);
    grav_maxprocs = n > 0 ? n : 1;
    pthread_mutex_unlock(&grav_pool_mu);
}

static void grav_task_exec(GravTask* t) {
    pthread_mutex_lock(&t->mu);
    if (t->state != 0) { pthread_mutex_unlock(&t->mu); return; }   /* someone else has it */
    t->state = 1;
    pthread_mutex_unlock(&t->mu);
    /* Run under a handler of our own so an escaping exception is captured into
       the task instead of unwinding whatever this thread was doing. */
    int nf = grav_nframes;
    int slot = grav_try_push();
    if (setjmp(grav_jmp_stack[slot]) == 0) {
        t->run(t);
        grav_jmp_top = slot;
    } else {
        grav_jmp_top = slot;
        grav_frames_restore(nf);
        GravExc* copy = (GravExc*)grav_alloc(sizeof(GravExc));
        memcpy(copy, &grav_current_exc, sizeof(GravExc));
        t->exc = copy;
        t->failed = 1;
    }
    pthread_mutex_lock(&t->mu);
    t->state = 2;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->mu);
}

static void* grav_worker(void* arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&grav_pool_mu);
        while (!grav_q_head) pthread_cond_wait(&grav_pool_cv, &grav_pool_mu);
        GravTask* t = grav_q_head;
        grav_q_head = t->next;
        if (!grav_q_head) grav_q_tail = 0;
        t->refs++; /* keeps free(future) from releasing it while we look at it */
        pthread_mutex_unlock(&grav_pool_mu);
        grav_task_exec(t);
        pthread_mutex_lock(&grav_pool_mu);
        t->refs--;
        pthread_cond_broadcast(&grav_refs_cv);
        pthread_mutex_unlock(&grav_pool_mu);
    }
    return 0;
}

static GravTask* grav_spawn(GravTask* t, void (*run)(GravTask*), void* result) {
    t->run = run;
    t->result = result;
    pthread_mutex_init(&t->mu, 0);
    pthread_cond_init(&t->cv, 0);
    int limit = grav_max_procs();
    pthread_mutex_lock(&grav_pool_mu);
    t->allNext = grav_all_tasks;
    grav_all_tasks = t;
    if (grav_q_tail) grav_q_tail->next = t; else grav_q_head = t;
    grav_q_tail = t;
    /* Start another worker while under the limit (lazily, one per spawn). */
    if (grav_workers < limit) {
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, grav_worker, 0) == 0) grav_workers++;
        pthread_attr_destroy(&attr);
    }
    int none = grav_workers == 0;
    pthread_cond_signal(&grav_pool_cv);
    pthread_mutex_unlock(&grav_pool_mu);
    if (none) grav_task_exec(t); /* no thread could be started: run it here */
    return t;
}

/* `await t`: wait for completion (running it here if still queued), re-throw a
   captured exception, and return a pointer to the result slot. */
static void* grav_await(GravTask* t) {
    if (!t) grav_panic("await on a null future");
    grav_task_exec(t);
    pthread_mutex_lock(&t->mu);
    while (t->state != 2) pthread_cond_wait(&t->cv, &t->mu);
    t->awaited = 1;
    pthread_mutex_unlock(&t->mu);
    if (t->failed) {
        memcpy(&grav_current_exc, t->exc, sizeof(GravExc));
        grav_rethrow();
    }
    return t->result;
}

/* `free(future)`: wait for the task, then release its record (the future must
   not be awaited again afterwards). */
static void grav_task_free(GravTask* t) {
    if (!t) return;
    grav_task_exec(t);
    pthread_mutex_lock(&t->mu);
    while (t->state != 2) pthread_cond_wait(&t->cv, &t->mu);
    pthread_mutex_unlock(&t->mu);
    pthread_mutex_lock(&grav_pool_mu);
    for (GravTask** p = &grav_all_tasks; *p; p = &(*p)->allNext)
        if (*p == t) { *p = t->allNext; break; }
    /* A task run by its awaiter may still be queued: unlink it so no worker
       ever pops the freed record. */
    GravTask* prev = 0;
    for (GravTask* q = grav_q_head; q; prev = q, q = q->next) {
        if (q != t) continue;
        if (prev) prev->next = q->next; else grav_q_head = q->next;
        if (grav_q_tail == q) grav_q_tail = prev;
        break;
    }
    while (t->refs > 0) pthread_cond_wait(&grav_refs_cv, &grav_pool_mu); /* a worker still holds it */
    pthread_mutex_unlock(&grav_pool_mu);
    pthread_mutex_destroy(&t->mu);
    pthread_cond_destroy(&t->cv);
    free(t->exc);
    free(t); /* the task is the first member of its record */
}

static int grav_pending_tasks(void) {
    int n = 0;
    pthread_mutex_lock(&grav_pool_mu);
    for (GravTask* t = grav_all_tasks; t; t = t->allNext) {
        pthread_mutex_lock(&t->mu);
        if (t->state != 2) n++;
        pthread_mutex_unlock(&t->mu);
    }
    pthread_mutex_unlock(&grav_pool_mu);
    return n;
}

/* At exit: finish every task (even ones never awaited) and report failures
   nobody observed, like an uncaught exception. */
static void grav_join_all(void) {
    for (;;) {
        pthread_mutex_lock(&grav_pool_mu);
        GravTask* open = 0;
        for (GravTask* t = grav_all_tasks; t && !open; t = t->allNext) {
            pthread_mutex_lock(&t->mu);
            if (t->state != 2) open = t;
            pthread_mutex_unlock(&t->mu);
        }
        pthread_mutex_unlock(&grav_pool_mu);
        if (!open) break;
        grav_task_exec(open);
        pthread_mutex_lock(&open->mu);
        while (open->state != 2) pthread_cond_wait(&open->cv, &open->mu);
        pthread_mutex_unlock(&open->mu);
    }
    for (GravTask* t = grav_all_tasks; t; t = t->allNext) {
        if (t->failed && !t->awaited) {
            fflush(stdout);
            fprintf(stderr, "grav: uncaught exception in an async task: %s\n",
                    (t->exc->message && t->exc->message[0]) ? t->exc->message
                    : (t->exc->type ? t->exc->type->name : "?"));
            grav_print_trace(t->exc->trace, t->exc->ntrace);
            exit(1);
        }
    }
}

/* ---- assertEq and the test runner ---------------------------------------- */
static void grav_assert_eq(GravAny actual, GravAny expected, const char* msg, const char* loc) {
    if (grav_any_eq(actual, expected)) return;
    /* Strings are quoted so leading/trailing whitespace differences show. */
    const char* eq = expected.tag == GRAV_ANY_STR ? "\"" : "";
    const char* aq = actual.tag == GRAV_ANY_STR ? "\"" : "";
    char b[1024];
    snprintf(b, sizeof b, "%s%sexpected %s%s%s but got %s%s%s", msg, (msg && msg[0]) ? ": " : "",
             eq, grav_any_to_string(expected), eq, aq, grav_any_to_string(actual), aq);
    grav_assert_fail_at(b, loc);
}
static int grav_run_tests(const char* const* names, void (*const* fns)(void), int n) {
    const char* filter = getenv("GRAV_TEST_FILTER");
    volatile int passed = 0, failed = 0, skipped = 0;
    printf("running %d test%s\n", n, n == 1 ? "" : "s");
    fflush(stdout);
    grav_testing = 1;
    for (int i = 0; i < n; i++) {
        if (filter && filter[0] && !strstr(names[i], filter)) { skipped++; continue; }
        grav_jmp_top = 0;
        grav_frames_restore(0);
        if (setjmp(grav_test_jmp) == 0) {
            fns[i]();
            printf("  ok    %s\n", names[i]);
            passed++;
        } else {
            printf("  FAIL  %s\n        %s\n", names[i], grav_test_msg);
            failed++;
        }
        fflush(stdout);
    }
    grav_testing = 0;
    printf("result: %d passed, %d failed", passed, failed);
    if (skipped) printf(", %d filtered out", skipped);
    printf("\n");
    return failed ? 1 : 0;
}

)C";
}

} // namespace grav
