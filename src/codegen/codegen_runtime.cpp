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
#include <strings.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

#if defined(__GNUC__) && !defined(__clang__)
/* Locals at real risk across setjmp/longjmp are declared volatile by the
   compiler; GCC's remaining -Wclobbered reports (loop counters around a `try`
   that re-runs setjmp every iteration) are false positives. */
#pragma GCC diagnostic ignored "-Wclobbered"
/* GCC misreads the reference-count header in front of a string as an
   out-of-bounds write when it inlines retains of string literals. */
#pragma GCC diagnostic ignored "-Wstringop-overflow"
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

/* Unwinding: an owned reference local registers its address and a release
   function for as long as it is in scope. An exception jumps (longjmp) past the
   code that would release it, so before jumping the runtime releases every
   local registered after the handler was set up (their frames are still on the
   stack then). Per thread, like the handlers. */
typedef struct GravUnwind { void* addr; void (*rel)(void*); } GravUnwind;
static _Thread_local GravUnwind* grav_uw = 0;
static _Thread_local int grav_uw_top = 0;
static _Thread_local int grav_uw_cap = 0;
static GRAV_COLD void grav_uw_grow(void) {
    int cap = grav_uw_cap ? grav_uw_cap * 2 : 256;
    GravUnwind* n = (GravUnwind*)realloc(grav_uw, (size_t)cap * sizeof(GravUnwind));
    if (!n) { fprintf(stderr, "out of memory\n"); abort(); }
    grav_uw = n;
    grav_uw_cap = cap;
}
static inline void grav_uw_push(void* addr, void (*rel)(void*)) {
    if (GRAV_UNLIKELY(grav_uw_top == grav_uw_cap)) grav_uw_grow();
    grav_uw[grav_uw_top].addr = addr;
    grav_uw[grav_uw_top].rel = rel;
    grav_uw_top++;
}
/* Releases the locals registered above height `to`, newest first. Each entry is
   removed before its release runs, so a destructor that throws cannot release
   it twice. */
static void grav_uw_unwind(int to) {
    while (grav_uw_top > to) {
        GravUnwind e = grav_uw[--grav_uw_top];
        e.rel(e.addr);
    }
}

/* `grav test`: failures inside a test jump back to the runner instead of exiting. */
static _Thread_local int grav_testing = 0; /* runner thread only */
static jmp_buf grav_test_jmp;
static int grav_test_uw; /* unwind height when the running test started */
static char grav_test_msg[2048];
static GRAV_COLD GRAV_NORETURN void grav_test_fail(const char* m) {
    snprintf(grav_test_msg, sizeof grav_test_msg, "%s", m);
    grav_uw_unwind(grav_test_uw);
    longjmp(grav_test_jmp, 1);
}
/* Panic recovery (see the "Panic recovery" section below). */
static int grav_recovery_active(void);
static GRAV_NORETURN void grav_panic_unwind(const char* m);
static GRAV_COLD GRAV_NORETURN void grav_panic(const char* m) {
    if (grav_recovery_active()) grav_panic_unwind(m);
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
    void (*fields)(void*);                      /* release the object's reference fields, or 0 */
    void (*visit)(void*);                       /* report each object it refers to (cycle collector), or 0 */
    const struct GravItab* itabs;               /* every interface the class implements */
    int nitabs;
    const struct GravClassMeta* meta;           /* reflection (fields, methods, decorators), or 0 */
} GravTypeInfo;
/* An interface a class implements: the interface's marker and the class's table for it. */
typedef struct GravItab { const char* iface; const void* tbl; } GravItab;

/* Common object header: vtable pointer, runtime type descriptor, reference count.
   Closure environments share the header (their type info releases the captures). */
struct GravObject { const void* __vt; const GravTypeInfo* __type; long long __rc; };

/* A function value: code pointer + captured environment (0 when nothing is
   captured). Called as ((R (*)(void*, A...))f.fn)(f.env, a...). */
typedef struct GravFn { void (*fn)(void); void* env; } GravFn;
static inline bool grav_fn_eq(GravFn a, GravFn b) { return a.fn == b.fn && a.env == b.env; }

/* An interface value: the object plus its per-class method table. */
struct GravIface { void* obj; const void* itab; };

static inline bool grav_iface_eq(struct GravIface a, struct GravIface b) { return a.obj == b.obj; }

/* ---- Automatic reference counting ------------------------------------------
   Every object starts with one reference (its creator's). Owning places --
   locals, fields, globals, closure environments, container slots -- hold one
   each; when the count reaches zero the destructor chain runs, the object's
   reference fields are released, and the memory is freed. Counts change
   atomically, so objects may be shared between threads.
   `free(obj)` disposes early: the destructor runs and the fields are released
   now, but the memory stays until the last reference is gone (no dangling
   pointers). GRAV_RC_DEAD marks a disposed object. */
/* The 64-bit count: strong references in bits 0-31, weak references in bits
   32-55, the cycle collector's color in 56-57 and BUFFERED (a cycle candidate) at
   58, DEAD (disposed: destructor and fields done) at 60, GONE (no strong
   references left; the memory waits for the last weak one, or the collector) at 61. */
#define GRAV_RC_DEAD (1LL << 60)
#define GRAV_RC_GONE (1LL << 61)
#define GRAV_RC_BUFFERED (1LL << 58)
#define GRAV_RC_WEAK1 (1LL << 32)
#define GRAV_RC_COUNT(v) ((v) & (GRAV_RC_WEAK1 - 1))
#define GRAV_RC_WEAKS(v) (((v) >> 32) & ((1LL << 24) - 1))
#define GRAV_RC_COLOR(v) ((int)(((v) >> 56) & 3))
enum { GRAV_BLACK = 0, GRAV_GRAY = 1, GRAV_WHITE = 2, GRAV_PURPLE = 3 };
static int grav_mt = 0;
static void grav_obj_teardown(void* o) {
    const GravTypeInfo* t = ((struct GravObject*)o)->__type;
    if (t && t->drop) t->drop(o);
    if (t && t->fields) t->fields(o);
}
/* Destruction is iterative: an object whose count reaches zero while another is
   being torn down is queued, so freeing a long chain (a million-node linked
   list) uses constant stack instead of recursing once per node. */
static _Thread_local void** grav_dead = 0;
static _Thread_local size_t grav_dead_n = 0, grav_dead_cap = 0;
static _Thread_local int grav_destroying = 0;
static void grav_obj_destroy(void* o) {
    if (grav_destroying) {
        if (grav_dead_n == grav_dead_cap) {
            grav_dead_cap = grav_dead_cap ? grav_dead_cap * 2 : 64;
            grav_dead = (void**)realloc(grav_dead, grav_dead_cap * sizeof(void*));
            if (!grav_dead) abort();
        }
        grav_dead[grav_dead_n++] = o;
        return;
    }
    grav_destroying = 1;
    for (;;) {
        long long v = __atomic_load_n(&((struct GravObject*)o)->__rc, __ATOMIC_ACQUIRE);
        if (!(v & GRAV_RC_DEAD)) grav_obj_teardown(o);
        /* Without weak references (none can appear now: a new one is only ever
           copied from another) and outside the collector's list, the memory goes
           at once, with no atomic update. */
        v = __atomic_load_n(&((struct GravObject*)o)->__rc, __ATOMIC_ACQUIRE);
        if (GRAV_RC_WEAKS(v) == 0 && !(v & GRAV_RC_BUFFERED)) free(o);
        else {
            /* weak references keep the memory (not the object) until they go */
            long long old = __atomic_fetch_or(&((struct GravObject*)o)->__rc, GRAV_RC_GONE | GRAV_RC_DEAD, __ATOMIC_ACQ_REL);
            /* a cycle candidate stays until the collector drops it from its list */
            if (GRAV_RC_WEAKS(old) == 0 && !(old & GRAV_RC_BUFFERED)) free(o);
        }
        if (grav_dead_n == 0) break;
        o = grav_dead[--grav_dead_n];
    }
    grav_destroying = 0;
}
/* Counts need atomic updates only once a second thread exists. Until the first
   thread starts (grav_mt_on, called before pthread_create) they are plain
   increments and decrements; afterwards they are atomic for good. */
/* Written once, while only one thread exists; read-only afterwards. */
static void grav_mt_on(void) { if (!grav_mt) __atomic_store_n(&grav_mt, 1, __ATOMIC_SEQ_CST); }
static inline void* grav_retain(void* o) {
    if (o) {
        if (GRAV_LIKELY(!grav_mt)) ((struct GravObject*)o)->__rc++;
        else __atomic_add_fetch(&((struct GravObject*)o)->__rc, 1, __ATOMIC_RELAXED);
    }
    return o;
}
/* Cycle candidates: objects whose count dropped but not to zero. Kept only
   while this is the sole thread touching objects (see grav_gc_collect). */
static int grav_gc_others = 0;          /* other threads and async tasks running */
static int grav_gc_running = 0;
static void** grav_gc_roots = 0;
static size_t grav_gc_nroots = 0, grav_gc_cap = 0, grav_gc_threshold = 10000;
static void grav_gc_candidate(void* o) {
    long long* rc = &((struct GravObject*)o)->__rc;
    *rc = (*rc & ~(3LL << 56)) | ((long long)GRAV_PURPLE << 56);
    if (*rc & GRAV_RC_BUFFERED) return;
    if (grav_gc_nroots == grav_gc_cap) {
        size_t cap = grav_gc_cap ? grav_gc_cap * 2 : 1024;
        void** n = (void**)realloc(grav_gc_roots, cap * sizeof(void*));
        if (!n) return;                 /* no room: just not a candidate */
        grav_gc_roots = n; grav_gc_cap = cap;
    }
    *rc |= GRAV_RC_BUFFERED;
    grav_gc_roots[grav_gc_nroots++] = o;
}
static inline void grav_release(void* o) {
    if (!o) return;
    long long v;
    if (GRAV_LIKELY(!grav_mt)) v = --((struct GravObject*)o)->__rc;
    else v = __atomic_sub_fetch(&((struct GravObject*)o)->__rc, 1, __ATOMIC_ACQ_REL);
    if (GRAV_UNLIKELY(GRAV_RC_COUNT(v) == 0)) grav_obj_destroy(o);
    else if (!__atomic_load_n(&grav_gc_others, __ATOMIC_ACQUIRE) && !grav_gc_running &&
             ((struct GravObject*)o)->__type && ((struct GravObject*)o)->__type->visit)
        grav_gc_candidate(o);
}
/* `free(obj)`: run the destructor and release the fields now (once). */
static void grav_free_obj(void* o) {
    if (!o) return;
    long long before = __atomic_fetch_or(&((struct GravObject*)o)->__rc, GRAV_RC_DEAD, __ATOMIC_ACQ_REL);
    if (before & GRAV_RC_DEAD) return;
    grav_obj_teardown(o);
}
/* Unwinding out of a constructor: its object loses the creator's reference. When
   nobody else took one, its fields are released but its destructor never runs
   (the object was never fully built). */
static GRAV_UNUSED void grav_uw_unbuilt(void* p) {
    void* o = *(void**)p;
    long long v = __atomic_load_n(&((struct GravObject*)o)->__rc, __ATOMIC_ACQUIRE);
    if (GRAV_RC_COUNT(v) == 1 && !(v & GRAV_RC_DEAD)) {
        __atomic_fetch_or(&((struct GravObject*)o)->__rc, GRAV_RC_DEAD, __ATOMIC_ACQ_REL);
        const GravTypeInfo* t = ((struct GravObject*)o)->__type;
        if (t && t->fields) t->fields(o);
    }
    grav_release(o);
}
/* Weak references (`weak` fields). */
static inline void grav_weak_retain(void* o) {
    if (!o) return;
    if (GRAV_LIKELY(!grav_mt)) ((struct GravObject*)o)->__rc += GRAV_RC_WEAK1;
    else __atomic_add_fetch(&((struct GravObject*)o)->__rc, GRAV_RC_WEAK1, __ATOMIC_RELAXED);
}
static inline void grav_weak_release(void* o) {
    if (!o) return;
    long long v;
    if (GRAV_LIKELY(!grav_mt)) v = (((struct GravObject*)o)->__rc -= GRAV_RC_WEAK1);
    else v = __atomic_sub_fetch(&((struct GravObject*)o)->__rc, GRAV_RC_WEAK1, __ATOMIC_ACQ_REL);
    if ((v & GRAV_RC_GONE) && GRAV_RC_WEAKS(v) == 0 && !(v & GRAV_RC_BUFFERED)) free(o);
}
static void* grav_autorelease(void* o);
/* Read a weak reference: the object, held until the statement ends, or null
   once it is gone or disposed. Never revives an object that is being freed. */
static void* grav_weak_load(void* o) {
    if (!o) return 0;
    long long* rc = &((struct GravObject*)o)->__rc;
    if (GRAV_LIKELY(!grav_mt)) {
        if ((*rc & (GRAV_RC_DEAD | GRAV_RC_GONE)) || GRAV_RC_COUNT(*rc) == 0) return 0;
        ++*rc;
        return grav_autorelease(o);
    }
    long long v = __atomic_load_n(rc, __ATOMIC_ACQUIRE);
    for (;;) {
        if ((v & (GRAV_RC_DEAD | GRAV_RC_GONE)) || GRAV_RC_COUNT(v) == 0) return 0;
        if (__atomic_compare_exchange_n(rc, &v, v + 1, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return grav_autorelease(o);
    }
}
static inline GravFn grav_fn_retain(GravFn f) { grav_retain(f.env); return f; }
static inline void grav_fn_release(GravFn f) { grav_release(f.env); }
static inline struct GravIface grav_iface_retain(struct GravIface i) { grav_retain(i.obj); return i; }
static inline void grav_iface_release(struct GravIface i) { grav_release(i.obj); }
/* Unwind release functions (see grav_uw_push), by the local's C type. */
static GRAV_UNUSED void grav_uw_obj(void* p) { grav_release(*(void**)p); }
static GRAV_UNUSED void grav_uw_fn(void* p) { grav_fn_release(*(GravFn*)p); }
static GRAV_UNUSED void grav_uw_iface(void* p) { grav_iface_release(*(struct GravIface*)p); }

/* ---- Heap strings -----------------------------------------------------------
   Strings are `const char*`. Literals, strings from C (getenv, strerror, static
   buffers) and Grav-built strings look alike, so Grav builds its strings in a
   reserved address range, each behind a 16-byte header: a reference count and a
   check word tying the header to its address. A pointer is a counted string only
   if it lies in the range AND its header checks out; anything else (a literal, a
   C string, a pointer into the middle of a string) is permanent: retain and
   release do nothing, so an unrecognized string can leak but never be freed.
   Blocks come in size classes; freed blocks go to a per-thread cache (no lock),
   which spills to a shared list when it grows large. */
/* len caches strlen (-1 until first asked, or past 2 GiB): strings never change once
   Grav code holds them, so `s.length` and `s[i]` cost O(1) instead of a scan. */
/* hash caches the string's hash for hash maps (high bit set once computed). */
typedef struct GravStrHdr { int rc; int len; unsigned int meta; unsigned int hash; } GravStrHdr;
#define GRAV_SCLASSES 34
static size_t grav_ssize[GRAV_SCLASSES];
static char* grav_sbase = 0;            /* reserved range [grav_sbase, grav_slimit) */
static char* grav_slimit = 0;
static char* grav_sbump = 0;            /* next unused byte */
static char* grav_scommit = 0;          /* end of the committed (readable) part */
static unsigned long long grav_skey = 0;
static pthread_mutex_t grav_slock = PTHREAD_MUTEX_INITIALIZER;
static void* grav_sshared[GRAV_SCLASSES];       /* shared free lists (under grav_slock) */
static _Thread_local void* grav_scache[GRAV_SCLASSES];
static _Thread_local int grav_scache_n[GRAV_SCLASSES];
static int grav_sinit_done = 0;
static long long grav_str_nlive = 0;     /* counted strings alive (diagnostics) */
static GRAV_UNUSED long long grav_str_live(void) { return __atomic_load_n(&grav_str_nlive, __ATOMIC_RELAXED); }
static void grav_sinit(void) {
    pthread_mutex_lock(&grav_slock);
    if (!grav_sinit_done) {
        static const size_t small[] = {32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536,
                                       2048, 3072, 4096, 8192, 16384, 32768, 65536, 131072};
        int k = 0;
        for (; k < 20; k++) grav_ssize[k] = small[k];
        for (size_t z = 262144; k < GRAV_SCLASSES; k++, z *= 2) grav_ssize[k] = z;
        for (int shift = 38; shift >= 30 && !grav_sbase; shift--) {   /* 256 GiB .. 1 GiB of address space */
            void* p = mmap(0, (size_t)1 << shift, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (p != MAP_FAILED) { grav_slimit = (char*)p + ((size_t)1 << shift); __atomic_store_n(&grav_sbase, (char*)p, __ATOMIC_RELEASE); }
        }
        grav_scommit = grav_sbase;
        __atomic_store_n(&grav_sbump, grav_sbase, __ATOMIC_RELEASE);
        grav_skey = ((unsigned long long)(uintptr_t)&grav_skey * 0x9E3779B97F4A7C15ULL) ^ (unsigned long long)getpid() ^ 0x5bd1e995ULL;
        __atomic_store_n(&grav_sinit_done, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&grav_slock);
}
/* The check word: 24 bits derived from the header's address and a per-process
   key, then the size class. */
static inline unsigned int grav_smeta(const GravStrHdr* h, int cls) {
    unsigned long long x = (((unsigned long long)(uintptr_t)h) ^ grav_skey) * 0x9E3779B97F4A7C15ULL;
    return ((unsigned int)(x >> 40) << 8) | (unsigned int)cls;
}
static inline GravStrHdr* grav_shdr(const char* s) {
    /* The range only grows (under grav_slock); a string reaching this thread came
       through synchronization, so even a relaxed read covers it. */
    char* base = __atomic_load_n(&grav_sbase, __ATOMIC_RELAXED);
    uintptr_t off = (uintptr_t)s - (uintptr_t)base;          /* the header must lie inside the used range */
    if (off < sizeof(GravStrHdr) || off >= (uintptr_t)(__atomic_load_n(&grav_sbump, __ATOMIC_RELAXED) - base) || ((uintptr_t)s & 15)) return 0;
    GravStrHdr* h = (GravStrHdr*)(s - sizeof(GravStrHdr));
    int cls = (int)(h->meta & 0xff);
    if (cls >= GRAV_SCLASSES || h->meta != grav_smeta(h, cls)) return 0;
    return h;
}
/* Fills this thread's empty cache for `cls` with a batch of blocks under one
   lock: up to 32 from the shared free list, else a run of fresh blocks carved
   from the range (small classes; one block for large ones). */
static void grav_srefill(int cls) {
    int want = cls < 8 ? 32 : cls < 14 ? 8 : 1;
    pthread_mutex_lock(&grav_slock);
    for (int k = 0; k < want && grav_sshared[cls]; k++) {
        void* h = grav_sshared[cls];
        grav_sshared[cls] = *(void**)((GravStrHdr*)h + 1);
        *(void**)((GravStrHdr*)h + 1) = grav_scache[cls];
        grav_scache[cls] = h;
        grav_scache_n[cls]++;
    }
    if (!grav_scache[cls]) {
        size_t size = grav_ssize[cls];
        size_t room = (size_t)(grav_slimit - grav_sbump) / size;
        int n = (size_t)want < room ? want : (int)room;
        if (n > 0) {
            char* end = grav_sbump + (size_t)n * size;
            if (end > grav_scommit) {          /* commit in 4 MiB steps (or the whole run) */
                size_t step = (size_t)(end - grav_scommit);
                if (step < ((size_t)4 << 20)) step = (size_t)4 << 20;
                step = (step + 4095) & ~(size_t)4095;     /* whole pages: mprotect needs an aligned start next time */
                if ((size_t)(grav_slimit - grav_scommit) < step) step = (size_t)(grav_slimit - grav_scommit);
                if (mprotect(grav_scommit, step, PROT_READ | PROT_WRITE) == 0) grav_scommit += step;
            }
            if (end > grav_scommit) n = (int)((size_t)(grav_scommit - grav_sbump) / size);
            char* first = grav_sbump;
            if (n > 0) __atomic_store_n(&grav_sbump, first + (size_t)n * size, __ATOMIC_RELEASE);
            for (int k = n - 1; k >= 0; k--) {     /* in address order once popped */
                GravStrHdr* h = (GravStrHdr*)(first + (size_t)k * size);
                h->meta = 0;                       /* not a string until handed out */
                *(void**)(h + 1) = grav_scache[cls];
                grav_scache[cls] = h;
                grav_scache_n[cls]++;
            }
        }
    }
    pthread_mutex_unlock(&grav_slock);
}
/* A fresh counted string with room for n bytes plus a terminator (count 1). Falls
   back to malloc (a permanent string) if the reserved range is unavailable. */
static char* grav_str_new(size_t n) {
    if (GRAV_UNLIKELY(!__atomic_load_n(&grav_sinit_done, __ATOMIC_ACQUIRE))) grav_sinit();
    size_t need = n + 1 + sizeof(GravStrHdr);
    int cls = 0;
    while (cls < GRAV_SCLASSES && grav_ssize[cls] < need) cls++;
    GravStrHdr* h = 0;
    if (cls < GRAV_SCLASSES && grav_sbase) {
        if (!grav_scache[cls]) grav_srefill(cls);
        if (grav_scache[cls]) {
            h = (GravStrHdr*)grav_scache[cls];
            grav_scache[cls] = *(void**)(h + 1);
            grav_scache_n[cls]--;
        }
    }
    if (!h) {
        char* m = (char*)malloc(n + 1);           /* permanent: never counted, never freed */
        if (GRAV_UNLIKELY(!m)) grav_panic("out of memory");
        return m;
    }
    h->rc = 1;
    h->len = -1;
    h->meta = grav_smeta(h, cls);
    h->hash = 0;
    if (GRAV_LIKELY(!grav_mt)) grav_str_nlive++;
    else __atomic_add_fetch(&grav_str_nlive, 1, __ATOMIC_RELAXED);
    char* s = (char*)(h + 1);
    s[0] = 0;
    return s;
}
#if defined(__SANITIZE_ADDRESS__)
#define GRAV_STR_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GRAV_STR_ASAN 1
#endif
#endif
#ifdef GRAV_STR_ASAN
void __asan_poison_memory_region(void const volatile* addr, size_t size);
#endif
static void grav_sblock_free(GravStrHdr* h) {
    int cls = (int)(h->meta & 0xff);
    if (GRAV_LIKELY(!grav_mt)) grav_str_nlive--;
    else __atomic_sub_fetch(&grav_str_nlive, 1, __ATOMIC_RELAXED);
    h->meta = 0;                                  /* no longer a valid string */
#ifdef GRAV_STR_ASAN
    /* --sanitize: never reuse a freed string, and make any later access an error */
    __asan_poison_memory_region(h, grav_ssize[cls]);
    return;
#endif
    if (cls >= 20) madvise((char*)h + 4096, grav_ssize[cls] - 4096, MADV_DONTNEED); /* give large blocks' pages back */
    if (grav_scache_n[cls] < 256 || cls >= 20) {
        if (cls >= 20) {                          /* large blocks: always shared */
            pthread_mutex_lock(&grav_slock);
            *(void**)(h + 1) = grav_sshared[cls];
            grav_sshared[cls] = h;
            pthread_mutex_unlock(&grav_slock);
            return;
        }
        *(void**)(h + 1) = grav_scache[cls];
        grav_scache[cls] = h;
        grav_scache_n[cls]++;
        return;
    }
    /* The cache is full: hand it and half its blocks to the shared list in one lock. */
    *(void**)(h + 1) = grav_scache[cls];
    grav_scache[cls] = h;
    grav_scache_n[cls]++;
    void* first = grav_scache[cls];
    void* last = first;
    int moved = 1;
    while (moved < 128 && *(void**)((GravStrHdr*)last + 1)) { last = *(void**)((GravStrHdr*)last + 1); moved++; }
    grav_scache[cls] = *(void**)((GravStrHdr*)last + 1);
    grav_scache_n[cls] -= moved;
    pthread_mutex_lock(&grav_slock);
    *(void**)((GravStrHdr*)last + 1) = grav_sshared[cls];
    grav_sshared[cls] = first;
    pthread_mutex_unlock(&grav_slock);
}
/* For C helpers in libraries: a buffer of `bytes` bytes (terminator included)
   that becomes a counted string, and growing one. */
static char* grav_str_alloc(size_t bytes) { return grav_str_new(bytes ? bytes - 1 : 0); }
static char* grav_str_realloc(char* s, size_t n);
static char* grav_str_grow(char* s, size_t bytes) { return grav_str_realloc(s, bytes ? bytes - 1 : 0); }
/* Free a counted string outright (an unshared buffer a C helper is discarding). */
static void grav_str_free(const char* s) {
    GravStrHdr* h = s ? grav_shdr(s) : 0;
    if (h) grav_sblock_free(h);
}
/* Grow a string buffer a C helper is filling (only it holds the string). */
static char* grav_str_realloc(char* s, size_t n) {
    if (!s) return grav_str_new(n);
    GravStrHdr* h = grav_shdr(s);
    if (h && grav_ssize[h->meta & 0xff] >= n + 1 + sizeof(GravStrHdr)) { h->len = -1; h->hash = 0; return s; }
    char* r = grav_str_new(n);
    size_t old = h ? grav_ssize[h->meta & 0xff] - sizeof(GravStrHdr) : strlen(s) + 1;
    memcpy(r, s, old < n + 1 ? old : n + 1);
    if (h) grav_sblock_free(h);
    return r;
}
/* A string's hash for hash maps (FNV-1a, 31 bits): cached in a counted string's
   header after the first call, so a key is hashed once however often it is looked
   up or rehashed. */
static inline long long grav_str_hash(const char* s) {
    GravStrHdr* h = grav_shdr(s);
    if (h) {
        unsigned int c = __atomic_load_n(&h->hash, __ATOMIC_RELAXED);
        if (c & 0x80000000u) return (long long)(c & 0x7fffffffu);
    }
    unsigned long long v = 1469598103934665603ULL;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) { v ^= *p; v *= 1099511628211ULL; }
    unsigned int r = (unsigned int)(v & 0x7fffffffu);
    if (h) __atomic_store_n(&h->hash, r | 0x80000000u, __ATOMIC_RELAXED);
    return (long long)r;
}
/* The length of a string in bytes: cached in a counted string's header after the
   first call; a scan for literals and C strings. */
static inline long long grav_str_len(const char* s) {
    GravStrHdr* h = grav_shdr(s);
    if (!h) return (long long)strlen(s);
    int n = __atomic_load_n(&h->len, __ATOMIC_RELAXED);
    if (n >= 0) return n;
    size_t m = strlen(s);
    if (m < 0x7fffffff) __atomic_store_n(&h->len, (int)m, __ATOMIC_RELAXED);
    return (long long)m;
}
/* A counted copy of a malloc'd buffer the caller owns (the buffer is freed). */
static const char* grav_str_take(char* m) {
    if (!m) return "";
    size_t n = strlen(m);
    char* r = grav_str_new(n);
    memcpy(r, m, n + 1);
    free(m);
    return r;
}
static inline const char* grav_str_retain(const char* s) {
    GravStrHdr* h = s ? grav_shdr(s) : 0;
    if (h) {
        if (GRAV_LIKELY(!grav_mt)) h->rc++;
        else __atomic_add_fetch(&h->rc, 1, __ATOMIC_RELAXED);
    }
    return s;
}
static inline void grav_str_release(const char* s) {
    GravStrHdr* h = s ? grav_shdr(s) : 0;
    if (!h) return;
    long long v;
    if (GRAV_LIKELY(!grav_mt)) v = --h->rc;
    else v = __atomic_sub_fetch(&h->rc, 1, __ATOMIC_ACQ_REL);
    if (v == 0) grav_sblock_free(h);
}
static GRAV_UNUSED void grav_uw_str(void* p) { grav_str_release(*(const char**)p); }

/* The release list: results of calls and constructors used inside an expression
   are borrowed until the statement ends, then released together. Per thread; a
   statement records the depth before it runs and releases back to it after, so
   an exception's skipped releases are made by whoever handles it. */
static _Thread_local void** grav_pool = 0;
static _Thread_local size_t grav_pool_top = 0;
static _Thread_local size_t grav_pool_cap = 0;
static void grav_pool_grow(void) {
    grav_pool_cap = grav_pool_cap ? grav_pool_cap * 2 : 64;
    grav_pool = (void**)realloc(grav_pool, grav_pool_cap * sizeof(void*));
    if (!grav_pool) abort();
}
static inline void* grav_autorelease(void* o) {
    if (!o) return o;
    if (GRAV_UNLIKELY(grav_pool_top == grav_pool_cap)) grav_pool_grow();
    grav_pool[grav_pool_top++] = o;
    return o;
}
/* Releases a string whose header was validated when it was put on the list. */
static inline void grav_str_release_hdr(GravStrHdr* h) {
    long long v;
    if (GRAV_LIKELY(!grav_mt)) v = --h->rc;
    else v = __atomic_sub_fetch(&h->rc, 1, __ATOMIC_ACQ_REL);
    if (v == 0) grav_sblock_free(h);
}
/* Release-list entries: an object, a string's header (tag 1), or a boxed copy of
   a value struct with references (tag 2: its release function and bytes). */
typedef struct GravBox { void (*rel)(void*); long long data[]; } GravBox;
static size_t grav_gc_collect(void);
static void grav_pool_drain_slow(size_t mark) {
    if (GRAV_UNLIKELY(grav_gc_nroots >= grav_gc_threshold)) grav_gc_collect();
    while (grav_pool_top > mark) {
        void* p = grav_pool[--grav_pool_top];
        uintptr_t tag = (uintptr_t)p & 3;
        if (tag == 1) grav_str_release_hdr((GravStrHdr*)((uintptr_t)p & ~(uintptr_t)3));
        else if (tag == 2) {
            GravBox* b = (GravBox*)((uintptr_t)p & ~(uintptr_t)3);
            b->rel(b->data);
            free(b);
        } else grav_release(p);
    }
}
/* After each statement: usually there is nothing to release. */
static inline void grav_pool_drain(size_t mark) {
    if (grav_pool_top > mark || GRAV_UNLIKELY(grav_gc_nroots >= grav_gc_threshold)) grav_pool_drain_slow(mark);
}
/* ---- Cycle collector (trial deletion, after Bacon & Rajan) -------------------
   Reference counting alone never frees objects that refer to each other. From
   the candidates, subtract every reference that comes from inside the candidate
   graph (gray); what still has references is reachable from outside and gets them
   back (black); what drops to zero is garbage (white). White objects get their
   real counts back, are held, disposed (destructors run, fields released) and then
   released, so ordinary reference counting frees them. It runs only while this is
   the only thread touching objects; with other threads or async tasks running,
   candidates are not even recorded. */
static void (*grav_gc_cb)(void*) = 0;
static void** grav_gc_stack = 0;
static size_t grav_gc_sn = 0, grav_gc_scap = 0;
static void grav_gc_push(void* o) {
    if (grav_gc_sn == grav_gc_scap) {
        grav_gc_scap = grav_gc_scap ? grav_gc_scap * 2 : 1024;
        grav_gc_stack = (void**)realloc(grav_gc_stack, grav_gc_scap * sizeof(void*));
        if (!grav_gc_stack) abort();
    }
    grav_gc_stack[grav_gc_sn++] = o;
}
static GRAV_UNUSED void grav_gc_visit(void* o) { if (o) grav_gc_cb(o); }
static inline long long* grav_gc_rc(void* o) { return &((struct GravObject*)o)->__rc; }
static inline int grav_gc_color(void* o) { return GRAV_RC_COLOR(*grav_gc_rc(o)); }
static inline void grav_gc_paint(void* o, int c) { long long* rc = grav_gc_rc(o); *rc = (*rc & ~(3LL << 56)) | ((long long)c << 56); }
static void grav_gc_children(void* o, void (*cb)(void*)) {
    const GravTypeInfo* t = ((struct GravObject*)o)->__type;
    if (!t || !t->visit || (*grav_gc_rc(o) & GRAV_RC_DEAD)) return; /* disposed: holds nothing */
    grav_gc_cb = cb;
    t->visit(o);
}
static size_t grav_gc_work = 0;           /* objects the last pass traversed */
static void grav_gc_gray_child(void* t) { --*grav_gc_rc(t); if (grav_gc_color(t) != GRAV_GRAY) { grav_gc_paint(t, GRAV_GRAY); grav_gc_push(t); grav_gc_work++; } }
static void grav_gc_black_child(void* t) { ++*grav_gc_rc(t); if (grav_gc_color(t) != GRAV_BLACK) { grav_gc_paint(t, GRAV_BLACK); grav_gc_push(t); } }
static void grav_gc_scan_child(void* t) { grav_gc_push(t); }
static void grav_gc_white_child(void* t) { if (grav_gc_color(t) == GRAV_WHITE) { grav_gc_paint(t, GRAV_BLACK); grav_gc_push(t); } }
static void grav_gc_restore_child(void* t) { ++*grav_gc_rc(t); }
static void grav_gc_scan_black(void* o) {
    size_t base = grav_gc_sn;
    grav_gc_paint(o, GRAV_BLACK);
    grav_gc_push(o);
    while (grav_gc_sn > base) grav_gc_children(grav_gc_stack[--grav_gc_sn], grav_gc_black_child);
}
static size_t grav_gc_collect(void) {
    if (grav_gc_running || __atomic_load_n(&grav_gc_others, __ATOMIC_ACQUIRE) || grav_destroying) return 0;
    grav_gc_running = 1;
    size_t k = 0;
    /* 1. Keep the live candidates; forget the rest (freeing the ones that died meanwhile). */
    for (size_t i = 0; i < grav_gc_nroots; i++) {
        void* o = grav_gc_roots[i];
        long long* rc = grav_gc_rc(o);
        *rc &= ~GRAV_RC_BUFFERED;
        if (*rc & GRAV_RC_GONE) { if (GRAV_RC_WEAKS(*rc) == 0) free(o); continue; }
        if (GRAV_RC_COUNT(*rc) == 0 || (*rc & GRAV_RC_DEAD)) { grav_gc_paint(o, GRAV_BLACK); continue; }
        grav_gc_roots[k++] = o;
    }
    grav_gc_nroots = k;
    grav_gc_work = k;
    /* 2. Gray: take away the references from inside the graph. */
    for (size_t i = 0; i < k; i++) {
        void* o = grav_gc_roots[i];
        if (grav_gc_color(o) == GRAV_GRAY) continue;
        grav_gc_paint(o, GRAV_GRAY);
        grav_gc_push(o);
        while (grav_gc_sn) grav_gc_children(grav_gc_stack[--grav_gc_sn], grav_gc_gray_child);
    }
    /* 3. Scan: still referenced -> black (references given back), else white. */
    for (size_t i = 0; i < k; i++) {
        grav_gc_push(grav_gc_roots[i]);
        while (grav_gc_sn) {
            void* o = grav_gc_stack[--grav_gc_sn];
            if (grav_gc_color(o) != GRAV_GRAY) continue;
            if (GRAV_RC_COUNT(*grav_gc_rc(o)) > 0) { grav_gc_scan_black(o); continue; }
            grav_gc_paint(o, GRAV_WHITE);
            grav_gc_children(o, grav_gc_scan_child);
        }
    }
    /* 4. Gather the white objects (painted black as they are taken). */
    void** white = 0; size_t nw = 0, cw = 0;
    for (size_t i = 0; i < k; i++) {
        void* r = grav_gc_roots[i];
        if (grav_gc_color(r) != GRAV_WHITE) { grav_gc_paint(r, GRAV_BLACK); continue; }
        grav_gc_paint(r, GRAV_BLACK);
        grav_gc_push(r);
        while (grav_gc_sn) {
            void* o = grav_gc_stack[--grav_gc_sn];
            if (nw == cw) { cw = cw ? cw * 2 : 256; white = (void**)realloc(white, cw * sizeof(void*)); if (!white) abort(); }
            white[nw++] = o;
            grav_gc_children(o, grav_gc_white_child);
        }
    }
    grav_gc_nroots = 0;
    /* 5. Give the garbage its real counts back, hold it, dispose of it, let go. */
    for (size_t i = 0; i < nw; i++) grav_gc_children(white[i], grav_gc_restore_child);
    for (size_t i = 0; i < nw; i++) ++*grav_gc_rc(white[i]);
    for (size_t i = 0; i < nw; i++) grav_free_obj(white[i]);
    for (size_t i = 0; i < nw; i++) grav_release(white[i]);
    free(white);
    /* The next pass waits for twice as many candidates as this one traversed, so
       collecting costs at most a constant per candidate (walking a long list
       that is still in use does not make every pass re-walk it). What a pass
       frees does not count against it: productive passes keep the pace. */
    size_t wasted = grav_gc_work > nw ? grav_gc_work - nw : 0;   /* traversed but still alive */
    grav_gc_threshold = wasted * 2 > 10000 ? wasted * 2 : 10000;
    grav_gc_running = 0;
    return nw;
}
static void grav_autorelease_box(void (*rel)(void*), const void* v, size_t n) {
    GravBox* b = (GravBox*)grav_alloc(sizeof(GravBox) + n);
    b->rel = rel;
    memcpy(b->data, v, n);
    grav_autorelease((void*)((uintptr_t)b | 2));
}
/* A thread is ending: release what it still holds and give back its per-thread
   buffers (release list, destruction queue, cached string blocks), so threads
   that come and go (one per HTTP connection) leave nothing behind. */
static void grav_thread_end(void) {
    grav_pool_drain(0);
    __atomic_sub_fetch(&grav_gc_others, 1, __ATOMIC_SEQ_CST);  /* counted in Thread.start */
    free(grav_pool); grav_pool = 0; grav_pool_cap = 0;
    free(grav_dead); grav_dead = 0; grav_dead_cap = 0; grav_dead_n = 0;
    free(grav_uw); grav_uw = 0; grav_uw_cap = 0; grav_uw_top = 0;
    pthread_mutex_lock(&grav_slock);
    for (int c = 0; c < GRAV_SCLASSES; c++) {
        while (grav_scache[c]) {
            void* h = grav_scache[c];
            grav_scache[c] = *(void**)((GravStrHdr*)h + 1);
            *(void**)((GravStrHdr*)h + 1) = grav_sshared[c];
            grav_sshared[c] = h;
        }
        grav_scache_n[c] = 0;
    }
    pthread_mutex_unlock(&grav_slock);
}
static inline const char* grav_str_autorelease(const char* s) {
    GravStrHdr* h = s ? grav_shdr(s) : 0;
    if (h) grav_autorelease((void*)((uintptr_t)h | 1));
    return s;
}
static inline GravFn grav_fn_autorelease(GravFn f) { grav_autorelease(f.env); return f; }
static inline struct GravIface grav_iface_autorelease(struct GravIface i) { grav_autorelease(i.obj); return i; }

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
    char* r = grav_str_new((size_t)b.len);
    if (b.data) memcpy(r, b.data, (size_t)b.len);
    r[b.len] = 0; return r;
}

/* Strings. `+` chains are fused at compile time into one grav_concatn/grav_fmt
   call, so `a + b + c` costs a single allocation instead of one per `+`. */
static inline bool grav_streq(const char* a, const char* b) {
    return a == b || strcmp(a, b) == 0;
}
static inline GRAV_UNUSED bool grav_streq_opt(const char* a, const char* b) { /* either may be NULL */
    return a == b || (a && b && strcmp(a, b) == 0);
}
static const char* grav_strdup(const char* s) {
    size_t n = strlen(s);
    char* r = grav_str_new(n);
    memcpy(r, s, n + 1);
    return r;
}
static const char* grav_str_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* r = grav_str_new(la + lb);
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
    char* r = grav_str_new(total); char* w = r;
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
/* Records the length of a string just built (so `.length` needs no scan). */
static inline void grav_str_set_len(char* s, size_t n) {
    GravStrHdr* h = grav_shdr(s);
    if (h && n < 0x7fffffff) h->len = (int)n;
}
/* Decimal digits of v, written backwards ending at `end`; returns the first. */
static inline char* grav_udigits(unsigned long long v, char* end) {
    do { *--end = (char)('0' + v % 10); v /= 10; } while (v);
    return end;
}
/* Interpolation: a format of literal text with %s, %lld and %llu (and %% for a
   percent sign), which is all the compiler emits. Built directly, without the
   printf machinery; anything else falls back to vsnprintf. */
static const char* grav_fmt(const char* fmt, ...) {
    char stack[512];
    char* buf = stack;
    size_t len = 0, cap = sizeof stack;
    va_list ap;
    va_start(ap, fmt);
    for (const char* f = fmt; *f;) {
        const char* piece;
        size_t n;
        char num[24];
        if (*f != '%') {
            const char* q = f;
            while (*q && *q != '%') q++;
            piece = f; n = (size_t)(q - f); f = q;
        } else if (f[1] == '%') {
            piece = "%"; n = 1; f += 2;
        } else if (f[1] == 's') {
            piece = va_arg(ap, const char*);
            n = (size_t)grav_str_len(piece); f += 2;
        } else if (f[1] == 'l' && f[2] == 'l' && f[3] == 'd') {
            long long v = va_arg(ap, long long);
            char* d = grav_udigits(v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v, num + sizeof num);
            if (v < 0) *--d = '-';
            piece = d; n = (size_t)(num + sizeof num - d); f += 4;
        } else if (f[1] == 'l' && f[2] == 'l' && f[3] == 'u') {
            char* d = grav_udigits(va_arg(ap, unsigned long long), num + sizeof num);
            piece = d; n = (size_t)(num + sizeof num - d); f += 4;
        } else {                                       /* not one of ours: let printf do all of it */
            va_end(ap);
            if (buf != stack) free(buf);
            va_start(ap, fmt);
            va_list ap2; va_copy(ap2, ap);
            int m = vsnprintf(0, 0, fmt, ap);
            va_end(ap);
            char* r = grav_str_new((size_t)(m < 0 ? 0 : m));
            if (m > 0) vsnprintf(r, (size_t)m + 1, fmt, ap2);
            va_end(ap2);
            return r;
        }
        if (len + n + 1 > cap) {
            size_t nc = cap * 2;
            while (nc < len + n + 1) nc *= 2;
            char* nb = (char*)malloc(nc);
            if (!nb) abort();
            memcpy(nb, buf, len);
            if (buf != stack) free(buf);
            buf = nb; cap = nc;
        }
        if (n <= 16) { for (size_t k = 0; k < n; k++) buf[len + k] = piece[k]; }   /* short pieces: no libc call */
        else memcpy(buf + len, piece, n);
        len += n;
    }
    va_end(ap);
    char* r = grav_str_new(len);
    if (len <= 32) { for (size_t k = 0; k < len; k++) r[k] = buf[k]; }
    else memcpy(r, buf, len);
    r[len] = 0;
    if (buf != stack) free(buf);
    grav_str_set_len(r, len);
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
    char num[24];
    char* d = grav_udigits(v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v, num + sizeof num);
    if (v < 0) *--d = '-';
    size_t n = (size_t)(num + sizeof num - d);
    char* b = grav_str_new(n); memcpy(b, d, n); b[n] = 0; return b;
}
static const char* grav_uint_to_str(unsigned long long v) {
    char num[24];
    char* d = grav_udigits(v, num + sizeof num);
    size_t n = (size_t)(num + sizeof num - d);
    char* b = grav_str_new(n); memcpy(b, d, n); b[n] = 0; return b;
}
/* Floats print in the shortest form that reads back as the same value: 2.5 is
   2.5, 0.1 + 0.2 is 0.30000000000000004, 1e21 is 1e+21; NaN and the infinities
   as NaN, Infinity and -Infinity. `b` holds 32 bytes. */
static char* grav_dtoa(double v, char* b) {
    if (v != v) { strcpy(b, "NaN"); return b; }
    if (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) { strcpy(b, v > 0 ? "Infinity" : "-Infinity"); return b; }
    for (int p = 15; p <= 17; p++) {
        snprintf(b, 32, "%.*g", p, v);
        if (p == 17 || strtod(b, 0) == v) break;
    }
    return b;
}
static char* grav_ftoa(float v, char* b) { /* float32: shortest that reads back as the float */
    if (v != v || v > 3.4028235e38f || v < -3.4028235e38f) return grav_dtoa((double)v, b);
    for (int p = 6; p <= 9; p++) {
        snprintf(b, 32, "%.*g", p, (double)v);
        if (p == 9 || strtof(b, 0) == v) break;
    }
    return b;
}
static const char* grav_float_to_str(double v) { char b[32]; return grav_strdup(grav_dtoa(v, b)); }
static const char* grav_float32_to_str(float v) { char b[32]; return grav_strdup(grav_ftoa(v, b)); }

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
    b[n] = 0; return grav_str_take(b);
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
/* An `any` holding an object or a closure owns a reference to it like any other
   owning place; the other kinds are plain values. */
static inline GravAny grav_any_retain(GravAny a) {
    if (a.tag == GRAV_ANY_OBJ) grav_retain(a.v.p);
    else if (a.tag == GRAV_ANY_FN) grav_retain(a.v.fn.env);
    else if (a.tag == GRAV_ANY_STR) grav_str_retain(a.v.s);
    return a;
}
static inline void grav_any_release(GravAny a) {
    if (a.tag == GRAV_ANY_OBJ) grav_release(a.v.p);
    else if (a.tag == GRAV_ANY_FN) grav_release(a.v.fn.env);
    else if (a.tag == GRAV_ANY_STR) grav_str_release(a.v.s);
}
static GRAV_UNUSED void grav_uw_any(void* p) { grav_any_release(*(GravAny*)p); }

/* Reflection (std.core.ClassInfo): what the compiler records about a class whose
   hierarchy has decorators or is named by reflect(...). A field's reader boxes its
   value (borrowed); its writer stores an `any` when it fits (1) or refuses (0).
   Either is 0 for a type that cannot go through an `any`. A method's or
   constructor's caller converts its arguments the same way and gives back its
   result (owned): 0 done, 1 `self` is not of the class, 2 the wrong number of
   arguments, 3 + i argument i does not fit. */
typedef struct GravDecoMeta { const char* name; int nargs; const GravAny* args; } GravDecoMeta;
enum { GRAV_RM_READONLY = 1, GRAV_RM_PRIVATE = 2, GRAV_RM_PROTECTED = 4, GRAV_RM_WEAK = 8, GRAV_RM_STATIC = 16 };
typedef struct GravFieldMeta {
    const char* name; const char* type; int flags;
    int ndecos; const GravDecoMeta* decos;
    GravAny (*get)(void* obj);
    int (*set)(void* obj, GravAny v);
} GravFieldMeta;
typedef struct GravMethodMeta {
    const char* name; int flags; int ndecos; const GravDecoMeta* decos;
    int nparams; const char* params;          /* its parameter types, "string, int" */
    int (*call)(GravAny self, const GravAny* args, int n, GravAny* out);   /* 0 when it cannot be called by reflection */
} GravMethodMeta;
typedef struct GravCtorMeta {
    int nparams; const char* params;
    int (*make)(const GravAny* args, int n, GravAny* out);
} GravCtorMeta;
typedef struct GravClassMeta {
    int nfields; const GravFieldMeta* fields;
    int nmethods; const GravMethodMeta* methods;
    int ndecos; const GravDecoMeta* decos;
    int nctors; const GravCtorMeta* ctors;    /* none for an abstract class */
    void* info;                               /* its ClassInfo once made (set once, atomically) */
} GravClassMeta;
/* Reflection's conversions of an `any` to a field's or parameter's type: 1 with
   the value in *out when it fits, else 0. An integer type `bits` wide (signed
   or not) takes whole numbers in its range: integers, bools, floats without a
   fraction, and strings holding one ("42", as databases give DECIMAL and BIGINT).
   A float takes numbers and numeric strings; a bool takes bools, 0 and 1, and
   "true"/"false", "t"/"f", "yes"/"no", "1"/"0". */
static GRAV_UNUSED int grav_rf_int(GravAny v, int bits, int uns, long long* out) {
    long long x = 0;
    unsigned long long u = 0;
    int big = 0;                              /* (u holds a value above LLONG_MAX) */
    switch (v.tag) {
        case GRAV_ANY_INT: case GRAV_ANY_ENUM: x = v.v.i; break;
        case GRAV_ANY_UINT: if (v.v.u > 9223372036854775807ULL) { u = v.v.u; big = 1; } else x = (long long)v.v.u; break;
        case GRAV_ANY_BOOL: x = v.v.b ? 1 : 0; break;
        case GRAV_ANY_FLOAT: {
            double d = v.v.f;
            if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0) || d != (double)(long long)d) return 0;
            x = (long long)d;
            break;
        }
        case GRAV_ANY_STR: {
            const char* s = v.v.s;
            char* end = 0;
            if (!s || !*s) return 0;
            while (*s == ' ') s++;
            errno = 0;
            if (*s == '-') x = strtoll(s, &end, 10);
            else { u = strtoull(s, &end, 10); if (u > 9223372036854775807ULL) big = 1; else x = (long long)u; }
            if (end == s || *end || errno) return 0;
            break;
        }
        default: return 0;
    }
    if (big) { if (!uns || bits < 64) return 0; *out = (long long)u; return 1; }
    if (uns) { if (x < 0 || (bits < 64 && ((unsigned long long)x >> bits) != 0)) return 0; }
    else if (bits < 64) { long long lim = 1LL << (bits - 1); if (x < -lim || x >= lim) return 0; }
    *out = x;
    return 1;
}
static GRAV_UNUSED int grav_rf_float(GravAny v, double* out) {
    switch (v.tag) {
        case GRAV_ANY_FLOAT: *out = v.v.f; return 1;
        case GRAV_ANY_INT: case GRAV_ANY_ENUM: *out = (double)v.v.i; return 1;
        case GRAV_ANY_UINT: *out = (double)v.v.u; return 1;
        case GRAV_ANY_STR: {
            char* end = 0;
            if (!v.v.s || !*v.v.s) return 0;
            double d = strtod(v.v.s, &end);
            if (*end) return 0;
            *out = d;
            return 1;
        }
        default: return 0;
    }
}
static GRAV_UNUSED int grav_rf_bool(GravAny v, int* out) {
    switch (v.tag) {
        case GRAV_ANY_BOOL: *out = v.v.b ? 1 : 0; return 1;
        case GRAV_ANY_INT: case GRAV_ANY_UINT: if (v.v.u > 1) return 0; *out = (int)v.v.u; return 1;
        case GRAV_ANY_STR: {
            static const char* const yes[] = { "true", "t", "yes", "y", "1", 0 }, * const no[] = { "false", "f", "no", "n", "0", 0 };
            for (int i = 0; yes[i]; i++) if (!strcasecmp(v.v.s, yes[i])) { *out = 1; return 1; }
            for (int i = 0; no[i]; i++) if (!strcasecmp(v.v.s, no[i])) { *out = 0; return 1; }
            return 0;
        }
        default: return 0;
    }
}
static inline GravAny grav_any_autorelease(GravAny a) {
    if (a.tag == GRAV_ANY_OBJ) grav_autorelease(a.v.p);
    else if (a.tag == GRAV_ANY_FN) grav_autorelease(a.v.fn.env);
    else if (a.tag == GRAV_ANY_STR) grav_str_autorelease(a.v.s);
    return a;
}
static GRAV_UNUSED void grav_gc_visit_any(GravAny a) {
    if (a.tag == GRAV_ANY_OBJ) grav_gc_visit(a.v.p);
    else if (a.tag == GRAV_ANY_FN) grav_gc_visit(a.v.fn.env);
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
    /* want == 0: any object (an interface cast checks it next). */
    if (a.tag == GRAV_ANY_OBJ && (!want || grav_type_is(((const struct GravObject*)a.v.p)->__type, want))) return a.v.p;
    grav_any_bad(a, want ? want->name : "an object");
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
        case GRAV_ANY_STR: return grav_str_retain(a.v.s);   /* results are the caller's */
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
static _Thread_local int grav_jmp_uw[GRAV_MAXTRY]; /* unwind height when each handler was set */
static _Thread_local GravExc grav_current_exc;
/* Panic recovery points (Panic.recover, servers, threads): each remembers the try
   depth and call depth where it was set. An exception that would leave the region
   stops there, and so does a panic while recovery is enabled for this thread. */
#define GRAV_MAXRECOVER 64
static _Thread_local jmp_buf grav_recover_stack[GRAV_MAXRECOVER];
static _Thread_local int grav_recover_try[GRAV_MAXRECOVER];
static _Thread_local int grav_recover_nf[GRAV_MAXRECOVER];
static _Thread_local int grav_recover_uw[GRAV_MAXRECOVER];
static _Thread_local int grav_recover_top = 0;
static _Thread_local char grav_recover_msg[2048];
/* A recovered panic unwinds as an exception of this type: no `catch` matches it
   (there is no object), so `finally` blocks run and it reaches the recovery point. */
static const GravTypeInfo grav_panic_typeinfo = { "panic", 0, 0, 0, 0, 0, 0, 0, 0 };
/* The try depth below which handlers belong outside the innermost recovery region. */
static inline int grav_try_floor(void) {
    return grav_recover_top > 0 ? grav_recover_try[grav_recover_top - 1] : 0;
}
static GRAV_NORETURN void grav_recover_land(const char* what, const char* msg, const GravTypeInfo* t) {
    snprintf(grav_recover_msg, sizeof grav_recover_msg, "%s: %s", what,
             (msg && msg[0]) ? msg : (t ? t->name : "?"));
    int k = grav_recover_top - 1;
    if (k < 0 || k >= GRAV_MAXRECOVER) abort(); /* only called inside a recovery region */
    grav_uw_unwind(grav_recover_uw[k]);
    longjmp(grav_recover_stack[k], 1);
}
static inline int grav_try_push(void) {
    if (GRAV_UNLIKELY(grav_jmp_top >= GRAV_MAXTRY)) grav_panic("try blocks nested too deeply");
    grav_jmp_uw[grav_jmp_top] = grav_uw_top;
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
/* The in-flight exception owns one reference to its object and its message.
   A `catch` moves the object out (grav_exc_take); a `finally` on the way out
   saves and restores it (grav_exc_save/grav_exc_restore). */
static void grav_exc_clear(void) {
    void* v = grav_current_exc.value; const char* m = grav_current_exc.message;
    grav_current_exc.value = 0; grav_current_exc.message = 0;
    grav_release(v);
    grav_str_release(m);
}
static void* grav_exc_take(void) {
    void* v = grav_current_exc.value;
    grav_current_exc.value = 0;
    grav_str_release(grav_current_exc.message);
    grav_current_exc.message = 0;
    return v;
}
static void grav_exc_save(GravExc* out) {
    memcpy(out, &grav_current_exc, sizeof(GravExc));
    grav_current_exc.value = 0; grav_current_exc.message = 0;
}
static void grav_exc_restore(const GravExc* in) {
    grav_exc_clear();
    memcpy(&grav_current_exc, in, sizeof(GravExc));
}
static GRAV_NORETURN void grav_throw(void* v, const GravTypeInfo* t, const char* msg) {
    if (grav_current_exc.value != v) grav_exc_clear();
    else grav_str_release(grav_current_exc.message);
    grav_current_exc.value = v; grav_current_exc.type = t; grav_current_exc.message = msg;
#ifndef GRAV_NO_TRACE
    /* Snapshot the live call stack at the throw site (before any unwinding). */
    {
        int n = grav_nframes > GRAV_MAXFRAMES ? GRAV_MAXFRAMES : grav_nframes;
        memcpy(grav_current_exc.trace, grav_frames, (size_t)n * sizeof(GravFrame));
        grav_current_exc.ntrace = n;
    }
#endif
    if (grav_jmp_top > grav_try_floor()) {
        grav_uw_unwind(grav_jmp_uw[grav_jmp_top - 1]);
        longjmp(grav_jmp_stack[grav_jmp_top - 1], 1);
    }
    if (grav_recover_top > 0) grav_recover_land(t == &grav_panic_typeinfo ? "panic" : "uncaught exception", msg, t);
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
    if (grav_jmp_top > grav_try_floor()) {
        grav_uw_unwind(grav_jmp_uw[grav_jmp_top - 1]);
        longjmp(grav_jmp_stack[grav_jmp_top - 1], 1);
    }
    if (grav_recover_top > 0)
        grav_recover_land(grav_current_exc.type == &grav_panic_typeinfo ? "panic" : "uncaught exception",
                          grav_current_exc.message, grav_current_exc.type);
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

/* ---- Panic recovery ------------------------------------------------------
   Effective setting = this thread's override if it has one, else the global
   setting (default on; GRAV_PANIC_RECOVERY=off|0|false turns it off at start). */
static int grav_recovery_global = -1;               /* -1: not read from the environment yet */
static _Thread_local int grav_recovery_thread = -1; /* -1: follow the global setting */
static int grav_recovery_enabled(void) {
    if (grav_recovery_thread >= 0) return grav_recovery_thread;
    int g = __atomic_load_n(&grav_recovery_global, __ATOMIC_SEQ_CST);
    if (g < 0) {
        const char* e = getenv("GRAV_PANIC_RECOVERY");
        g = (e && (strcmp(e, "off") == 0 || strcmp(e, "0") == 0 || strcmp(e, "false") == 0)) ? 0 : 1;
        __atomic_store_n(&grav_recovery_global, g, __ATOMIC_SEQ_CST);
    }
    return g;
}
static void grav_set_recovery_global(int on) { __atomic_store_n(&grav_recovery_global, on ? 1 : 0, __ATOMIC_SEQ_CST); }
static void grav_set_recovery_thread(int mode) { grav_recovery_thread = mode < 0 ? -1 : (mode ? 1 : 0); }
static int grav_recovery_active(void) { return grav_recover_top > 0 && grav_recovery_enabled(); }
/* A panic in a recovery region: unwind like an exception nobody can catch. */
static GRAV_NORETURN void grav_panic_unwind(const char* m) {
    static _Thread_local char msg[1024];
    snprintf(msg, sizeof msg, "%s", m ? m : "");
    grav_throw(0, &grav_panic_typeinfo, msg);
}
/* Run the closure `f` as a recovery point. Returns "" when it finished, else
   "panic: <message>" or "uncaught exception: <message>" (a fresh string). */
static const char* grav_recover_call(GravFn f) {
    if (grav_recover_top >= GRAV_MAXRECOVER) grav_panic("Panic.recover nested too deeply");
    int k = grav_recover_top;
    grav_recover_try[k] = grav_jmp_top;
    grav_recover_nf[k] = grav_nframes;
    grav_recover_uw[k] = grav_uw_top;
    grav_recover_top = k + 1;
    if (setjmp(grav_recover_stack[k]) == 0) {
        if (f.fn) ((void (*)(void*))f.fn)(f.env);
        grav_recover_top = k;
        return "";
    }
    grav_recover_top = k;
    grav_jmp_top = grav_recover_try[k];
    grav_frames_restore(grav_recover_nf[k]);
    grav_exc_clear(); /* recovered: its message is in grav_recover_msg */
    size_t n = strlen(grav_recover_msg);
    char* out = grav_str_new(n);
    memcpy(out, grav_recover_msg, n + 1);
    return out;
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
        grav_exc_save(copy);              /* the task record now owns the exception */
        t->exc = copy;
        t->failed = 1;
    }
    __atomic_sub_fetch(&grav_gc_others, 1, __ATOMIC_SEQ_CST);
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
    __atomic_add_fetch(&grav_gc_others, 1, __ATOMIC_SEQ_CST); /* until the task finishes */
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
        grav_mt_on();
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
        grav_exc_restore(t->exc);
        grav_retain(grav_current_exc.value);  /* the task keeps its copy too */
        grav_str_retain(grav_current_exc.message);
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
    if (t->exc) { grav_release(t->exc->value); grav_str_release(t->exc->message); }
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
        grav_test_uw = grav_uw_top;
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
