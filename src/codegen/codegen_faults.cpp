// Run-time faults: an out-of-range fixed-array index, integer division by zero,
// and a null (or otherwise invalid) memory access. With the standard library
// loaded the first two throw its IndexError / ArithmeticError, so a `catch` can
// handle them; without it they panic. A bad memory access is caught by a signal
// handler and reported as a panic with the call stack.
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

// Every one-byte string, back to back with their terminators ("\0\0", "\1\0", ...):
// `for (ch in s)` hands these out for ASCII characters instead of allocating.
static std::string byteStringTable() {
    std::string out = "static const char grav_byte_strs[513] =";
    for (int row = 0; row < 16; ++row) {
        out += "\n    \"";
        for (int c = row * 16; c < row * 16 + 16; ++c) {
            char buf[16];
            snprintf(buf, sizeof buf, "\\%03o\\000", c);
            out += buf;
        }
        out += "\"";
    }
    return out + ";\n";
}

void CodeGen::emitFaultSupport() {
    typedefs_ += R"C(#include <signal.h>
#include <sys/resource.h>
static GRAV_COLD GRAV_NORETURN void grav_fault_index_of(const char* what, long long i, long long n);
#define grav_fault_index(i, n) grav_fault_index_of("array", (i), (n))
static GRAV_COLD GRAV_NORETURN void grav_fault_div(void);
static GRAV_COLD GRAV_NORETURN void grav_fault_null(const char* msg);
static GRAV_COLD GRAV_NORETURN void grav_fault_enum(long long v, long long n, const char* name);
static GRAV_COLD GRAV_NORETURN void grav_fault_cast(const GravTypeInfo* from, const GravTypeInfo* to);
/* `n as E`: the number must name one of E's members. */
static inline long long grav_enum_check(long long v, long long n, const char* name) {
    if (GRAV_UNLIKELY((unsigned long long)v >= (unsigned long long)n)) grav_fault_enum(v, n, name);
    return v;
}
/* Interfaces at run time: `x is I` and `x as I` look up the object's table for I. */
static inline const void* grav_itab_find(const void* o, const char* iface) {
    if (!o) return 0;
    const GravTypeInfo* t = ((const struct GravObject*)o)->__type;
    for (int i = 0; i < t->nitabs; i++)
        if (t->itabs[i].iface == iface) return t->itabs[i].tbl;
    return 0;
}
static GRAV_COLD GRAV_NORETURN void grav_fault_iface(const void* o, const char* name);
static inline struct GravIface grav_iface_cast(void* o, const char* iface, const char* name) {
    struct GravIface r = { o, grav_itab_find(o, iface) };
    if (GRAV_UNLIKELY(o && !r.itab)) grav_fault_iface(o, name);
    return r;
}
static inline void* grav_any_objptr(GravAny a) { return a.tag == GRAV_ANY_OBJ ? a.v.p : 0; }
/* `obj as Sub` from a base type: the object must be a Sub (null stays null). */
static inline void* grav_cast_check(void* o, const GravTypeInfo* to) {
    if (GRAV_UNLIKELY(o && !grav_type_is(((struct GravObject*)o)->__type, to)))
        grav_fault_cast(((struct GravObject*)o)->__type, to);
    return o;
}
/* A fixed-array index, checked against the length. */
static inline long long grav_idx(long long i, long long n) {
    if (GRAV_UNLIKELY((unsigned long long)i >= (unsigned long long)n)) grav_fault_index(i, n);
    return i;
}
/* `s[i]`: the byte at index i (checked against the string's length). */
static inline unsigned char grav_str_byte(const char* s, long long i) {
    if (GRAV_UNLIKELY((unsigned long long)i >= (unsigned long long)grav_str_len(s))) grav_fault_index_of("string", i, grav_str_len(s));
    return (unsigned char)s[i];
}
/* `s[lo..hi]`: the bytes in [lo, hi), clamped to the string (a fresh string). */
static const char* grav_str_range(const char* s, long long lo, long long hi) {
    long long n = grav_str_len(s);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    if (hi < lo) hi = lo;
    char* r = grav_str_new((size_t)(hi - lo));
    memcpy(r, s + lo, (size_t)(hi - lo));
    r[hi - lo] = 0;
    return r;
}
)C" + byteStringTable() + R"C(/* `for (ch in s)`: the byte length of the UTF-8 character at p, and a copy of it. */
static inline int grav_utf8_len(const char* p) {
    unsigned char c = (unsigned char)p[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    for (int k = 1; k < n; k++) if (((unsigned char)p[k] & 0xC0) != 0x80) return 1; /* malformed: one byte */
    return n;
}
static inline const char* grav_str_slice(const char* p, int n) {
    if (n == 1) return grav_byte_strs + 2 * (unsigned char)p[0];   /* one byte: a permanent string, no allocation */
    char* r = grav_str_new((size_t)n);
    memcpy(r, p, (size_t)n);
    r[n] = 0;
    return r;
}
/* Integer `/` and `%`: a zero divisor is a fault, and dividing by -1 negates
   (in two's complement, so the most negative value cannot trap). */
#define GRAV_SDIV(T, a, b) ({ T grav_n_ = (a); T grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); \
    grav_d_ == -1 ? (T)(0ULL - (unsigned long long)grav_n_) : (T)(grav_n_ / grav_d_); })
#define GRAV_SMOD(T, a, b) ({ T grav_n_ = (a); T grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); \
    grav_d_ == -1 ? (T)0 : (T)(grav_n_ % grav_d_); })
#define GRAV_UDIV(T, a, b) ({ T grav_n_ = (a); T grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); (T)(grav_n_ / grav_d_); })
#define GRAV_UMOD(T, a, b) ({ T grav_n_ = (a); T grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); (T)(grav_n_ % grav_d_); })
/* `x /= y` and `x %= y`: the target is evaluated once in each branch. */
#define GRAV_SDIV_SET(T, x, b) do { __typeof__(b) grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); \
    if (grav_d_ == -1) { T* grav_p_ = &(x); *grav_p_ = (T)(0ULL - (unsigned long long)*grav_p_); } \
    else (x) /= grav_d_; } while (0)
#define GRAV_SMOD_SET(T, x, b) do { __typeof__(b) grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); \
    if (grav_d_ == -1) (x) = 0; else (x) %= grav_d_; } while (0)
#define GRAV_UDIV_SET(T, x, b) do { __typeof__(b) grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); (x) /= grav_d_; } while (0)
#define GRAV_UMOD_SET(T, x, b) do { __typeof__(b) grav_d_ = (b); \
    if (GRAV_UNLIKELY(grav_d_ == 0)) grav_fault_div(); (x) %= grav_d_; } while (0)

)C";
}

namespace {
// `x / k` with a literal k other than 0 and -1 cannot fault.
bool safeDivisor(const Expr &e) {
    auto *lit = dynamic_cast<const IntLiteralExpr *>(&e);
    return lit && lit->value != 0 && lit->value != -1;
}
} // namespace

// A slice (checked and normalized by the checker: bounds in index and more[0]).
std::string CodeGen::emitSlice(const IndexExpr &e) const {
    std::string lo = emitExpr(*e.index), hi = emitExpr(*e.more[0]);
    if (e.base->type.kind == TypeRef::Kind::String)
        return "grav_str_range(" + emitExpr(*e.base) + ", " + lo + ", " + hi + ")";
    // A fixed array with constant bounds: copy the elements into the shorter array.
    std::string items;
    for (int i = 0; i < e.type.arrayLen; ++i)
        items += (i ? ", " : "") + std::string("grav_sl_.data[") + lo + " + " + std::to_string(i) + "]";
    return "({ " + cTy(e.base->type) + " grav_sl_ = " + emitExpr(*e.base) + "; (" + cTy(e.type) + "){ { " + items +
           " } }; })";
}

std::string CodeGen::checkedIndex(const std::string &idx, int length) const {
    return "grav_idx(" + idx + ", " + std::to_string(length) + ")";
}

std::string CodeGen::checkedDivision(BinaryOp op, const TypeRef &type, const std::string &l,
                                     const std::string &r, const Expr &divisor) const {
    if (safeDivisor(divisor)) return "(" + l + " " + binaryOpSymbol(op) + " " + r + ")";
    std::string m = std::string(type.isUnsigned ? "GRAV_U" : "GRAV_S") + (op == BinaryOp::Div ? "DIV" : "MOD");
    return m + "(" + cTy(type) + ", " + l + ", " + r + ")";
}

std::string CodeGen::checkedDivisionAssign(BinaryOp op, const TypeRef &type, const std::string &target,
                                           const std::string &r, const Expr &divisor) const {
    if (safeDivisor(divisor)) return target + " " + binaryOpSymbol(op) + "= " + r;
    std::string m = std::string(type.isUnsigned ? "GRAV_U" : "GRAV_S") + (op == BinaryOp::Div ? "DIV" : "MOD");
    return m + "_SET(" + cTy(type) + ", " + target + ", " + r + ")";
}

// The fault raisers need the program's exception classes, so they come after
// every other definition.
void CodeGen::emitFaultRaisers() {
    // `throw new <cls>(msg)` from C, when the standard library defines `cls`.
    auto raise = [&](const std::string &fq) -> std::string {
        const ClassInfo *ci = reg_->cls(fq);
        if (!ci || ci->ctors.empty() || ci->ctors[0]->params.size() != 1 ||
            ci->ctors[0]->params[0].type.kind != TypeRef::Kind::String)
            return "";
        std::string S = structName(fq);
        std::string describe = emitDescribe("e", TypeRef::named(fq));
        return "    const char* m = grav_strdup(msg);\n"
               "    " + S + "* e = " + ctorCName(fq, 0) + "(m);\n"
               "    grav_str_release(m);\n"
               "    grav_throw((void*)e, &" + mangle(fq) + "_typeinfo, " +
               (describe.empty() ? "grav_strdup(msg)" : describe) + ");\n";
    };
    std::string index = raise("std.core.IndexError");
    std::string div = raise("std.core.ArithmeticError");
    std::string value = raise("std.core.ValueError");
    std::string type = raise("std.core.TypeError");
    std::string state = raise("std.core.StateError");
    defs_ += "static GRAV_COLD GRAV_NORETURN void grav_fault_index_of(const char* what, long long i, long long n) {\n"
             "    char msg[160];\n"
             "    snprintf(msg, sizeof msg, \"%s index %lld out of range (length %lld)\", what, i, n);\n" +
             (index.empty() ? "    grav_panic(msg);\n" : index) + "}\n";
    defs_ += "static GRAV_COLD GRAV_NORETURN void grav_fault_div(void) {\n"
             "    const char* msg = \"integer division by zero\";\n" +
             (div.empty() ? "    grav_panic(msg);\n" : div) + "}\n";
    defs_ += "static GRAV_COLD GRAV_NORETURN void grav_fault_null(const char* msg) {\n" +
             (state.empty() ? "    grav_panic(msg);\n" : state) + "}\n";
    defs_ += "static GRAV_COLD GRAV_NORETURN void grav_fault_enum(long long v, long long n, const char* name) {\n"
             "    char msg[200];\n"
             "    if (n < 0) snprintf(msg, sizeof msg, \"%lld is not a %s value\", v, name);\n"
             "    else snprintf(msg, sizeof msg, \"%lld is not a %s (its values are 0 to %lld)\", v, name, n - 1);\n" +
             (value.empty() ? "    grav_panic(msg);\n" : value) + "}\n";
    defs_ += "static GRAV_COLD GRAV_NORETURN void grav_fault_iface(const void* o, const char* name) {\n"
             "    char msg[300];\n"
             "    snprintf(msg, sizeof msg, \"a %s does not implement %s\", ((const struct GravObject*)o)->__type->name, name);\n" +
             (type.empty() ? "    grav_panic(msg);\n" : type) + "}\n";
    defs_ += "static GRAV_COLD GRAV_NORETURN void grav_fault_cast(const GravTypeInfo* from, const GravTypeInfo* to) {\n"
             "    char msg[300];\n"
             "    snprintf(msg, sizeof msg, \"cannot cast a %s to %s\", from->name, to->name);\n" +
             (type.empty() ? "    grav_panic(msg);\n" : type) + "}\n";
    defs_ += R"C(
/* A bad memory access (a null reference, or a stack overflow) stops the program
   with a panic and the Grav call stack. The handler runs on its own stack, so a
   stack overflow can still be reported; inside a recovery region (Panic.recover,
   HTTP handlers, threads) the panic unwinds to it like any other. Sanitizer
   builds keep their own, more detailed reports. */
static char* grav_stack_top = 0;
static size_t grav_stack_size = 0;
static void grav_on_fault(int sig, siginfo_t* si, void* ctx) {
    (void)ctx;
    char* addr = (char*)si->si_addr;
    const char* what;
    if (sig == SIGFPE) what = "arithmetic fault";
    else if ((uintptr_t)addr < 65536) what = "null reference: a field or method was used through a null value";
    else if (grav_stack_top && addr < grav_stack_top && addr + grav_stack_size + (1 << 20) > grav_stack_top)
        what = "stack overflow (is a recursion missing its base case?)";
    else what = "invalid memory access";
    if (grav_recovery_active() || grav_testing) grav_panic(what);
    fflush(stdout);
    fprintf(stderr, "panic: %s\n", what);
#ifndef GRAV_NO_TRACE
    /* A runaway recursion has thousands of identical frames: show the outermost. */
    grav_print_trace(grav_frames, grav_nframes < 24 ? grav_nframes : 24);
    if (grav_nframes > 24) fprintf(stderr, "    ... %d calls deep\n", grav_nframes);
#endif
    _exit(1);
}
static void grav_install_faults(void) {
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    static char altstack[1 << 16];
    stack_t ss;
    ss.ss_sp = altstack; ss.ss_size = sizeof altstack; ss.ss_flags = 0;
    sigaltstack(&ss, 0);
    char here;
    grav_stack_top = &here;
    struct rlimit rl;
    grav_stack_size = getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY ? (size_t)rl.rlim_cur : (8u << 20);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = grav_on_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
#endif
}

)C";
}

} // namespace grav
