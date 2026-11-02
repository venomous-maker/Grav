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

static GRAV_COLD GRAV_NORETURN void grav_panic(const char* m) {
    fflush(stdout); fprintf(stderr, "panic: %s\n", m); exit(1);
}
static inline void grav_assert(bool c, const char* m) {
    if (GRAV_UNLIKELY(!c)) { fflush(stdout); fprintf(stderr, "assertion failed: %s\n", m); exit(1); }
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
    static char b[4096]; return getcwd(b, sizeof b) ? b : "";
}

typedef struct GravTypeInfo {
    const char* name;
    const struct GravTypeInfo* base;            /* primary base */
    const struct GravTypeInfo* const* extra;    /* secondary bases (MI) */
    int nextra;
    void (*drop)(void*);                        /* destructor chain, or 0 */
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
    bool r = a == b || strcmp(a, b) == 0;
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

/* Call stack for diagnostics: a frame per active function (name+file+line).
   `grav_top` points at the innermost frame so a line mark is a single store. */
typedef struct GravFrame { const char* fn; const char* file; int line; } GravFrame;
#define GRAV_MAXFRAMES 512
static GravFrame grav_frames[GRAV_MAXFRAMES + 1]; /* last slot absorbs overflow */
static int grav_nframes = 0;
static GravFrame* grav_top = &grav_frames[GRAV_MAXFRAMES];
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
static jmp_buf grav_jmp_stack[GRAV_MAXTRY];
static int grav_jmp_top = 0;
static GravExc grav_current_exc;
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
    fflush(stdout);
    fprintf(stderr, "grav: uncaught exception: %s\n",
            (msg && msg[0]) ? msg : (t ? t->name : "?"));
    grav_print_trace(grav_current_exc.trace, grav_current_exc.ntrace);
    exit(1);
}
/* Re-raise the in-flight exception, keeping the trace from its original throw. */
static GRAV_NORETURN void grav_rethrow(void) {
    if (grav_jmp_top > 0) longjmp(grav_jmp_stack[grav_jmp_top - 1], 1);
    fflush(stdout);
    fprintf(stderr, "grav: uncaught exception: %s\n",
            (grav_current_exc.message && grav_current_exc.message[0])
                ? grav_current_exc.message
                : (grav_current_exc.type ? grav_current_exc.type->name : "?"));
    grav_print_trace(grav_current_exc.trace, grav_current_exc.ntrace);
    exit(1);
}

)C";
}

} // namespace grav
