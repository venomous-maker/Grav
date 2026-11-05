// The text of any value, for print / str / "${...}": numbers, strings, enums,
// objects (their toString(), else the class name), structs (`Point { x: 1, y: 2 }`),
// fixed arrays (`[1, 2, 3]`), functions (their type), pointers, and null. Inside
// a struct or an array, strings are quoted so the pieces stay apart.
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

void CodeGen::emitDisplaySupport() {
    typedefs_ += R"C(/* Display helpers: every result is the caller's string (+1). */
static const char* grav_obj_str(void* o) {
    if (!o) return "null";
    const GravTypeInfo* t = ((const struct GravObject*)o)->__type;
    return t->str ? t->str(o) : t->name;   /* toString() through the dynamic class */
}
static const char* grav_ptr_str(const void* p) {
    if (!p) return "null";
    char b[32]; snprintf(b, sizeof b, "%p", p); return grav_strdup(b);
}
/* A string in double quotes, with quotes, backslashes and control characters escaped. */
static const char* grav_str_quote(const char* s) {
    size_t n = 2;
    for (const char* p = s; *p; p++) n += (*p == '"' || *p == '\\' || *p == '\n' || *p == '\t') ? 2 : 1;
    char* r = grav_str_new(n); char* w = r;
    *w++ = '"';
    for (const char* p = s; *p; p++) {
        if (*p == '"' || *p == '\\') { *w++ = '\\'; *w++ = *p; }
        else if (*p == '\n') { *w++ = '\\'; *w++ = 'n'; }
        else if (*p == '\t') { *w++ = '\\'; *w++ = 't'; }
        else *w++ = *p;
    }
    *w++ = '"'; *w = 0;
    return r;
}
/* Text built piece by piece; each piece is the builder's to release. */
typedef struct GravText { char* buf; size_t len, cap; } GravText;
static void grav_text_add(GravText* t, const char* piece) {
    size_t n = strlen(piece);
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 64;
        while (cap < t->len + n + 1) cap *= 2;
        char* nb = (char*)realloc(t->buf, cap);
        if (!nb) grav_panic("out of memory");
        t->buf = nb; t->cap = cap;
    }
    memcpy(t->buf + t->len, piece, n + 1);
    t->len += n;
}
static void grav_text_take(GravText* t, const char* piece) { grav_text_add(t, piece); grav_str_release(piece); }
static const char* grav_text_done(GravText* t) {
    const char* r = grav_strdup(t->buf ? t->buf : "");
    free(t->buf);
    return r;
}
/* `${x:spec}`, spec = [<>^][+][0][width][.precision][type]: the text of x with a
   type (d f e x X o b s %), precision, and padding to a width. */
typedef struct GravSpec { char align, type; int plus, zero, width, prec; } GravSpec;
static GravSpec grav_spec(const char* s) {
    GravSpec sp = { 0, 0, 0, 0, 0, -1 };
    if (*s == '<' || *s == '>' || *s == '^') sp.align = *s++;
    if (*s == '+') { sp.plus = 1; s++; }
    if (*s == '0') { sp.zero = 1; s++; }
    while (*s >= '0' && *s <= '9') sp.width = sp.width * 10 + (*s++ - '0');
    if (*s == '.') { s++; sp.prec = 0; while (*s >= '0' && *s <= '9') sp.prec = sp.prec * 10 + (*s++ - '0'); }
    if (*s) sp.type = *s;
    return sp;
}
/* Pads `text` (numbers default to the right, with zeros after any sign). */
static const char* grav_spec_pad(const char* text, GravSpec sp, int numeric) {
    size_t n = strlen(text), w = sp.width > 0 ? (size_t)sp.width : 0;
    if (n >= w) return grav_strdup(text);
    size_t gap = w - n;
    char* r = grav_str_new(w);
    if (sp.zero && numeric && !sp.align) {
        size_t sign = (text[0] == '-' || text[0] == '+') ? 1 : 0;
        memcpy(r, text, sign);
        memset(r + sign, '0', gap);
        memcpy(r + sign + gap, text + sign, n - sign + 1);
        return r;
    }
    char align = sp.align ? sp.align : (numeric ? '>' : '<');
    size_t left = align == '>' ? gap : align == '^' ? gap / 2 : 0;
    memset(r, ' ', w);
    memcpy(r + left, text, n);
    r[w] = 0;
    return r;
}
static const char* grav_fmt_int(long long v, int uns, const char* spec) {
    GravSpec sp = grav_spec(spec);
    char b[80];
    unsigned long long u = uns ? (unsigned long long)v : (v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v);
    const char* sign = (!uns && v < 0) ? "-" : sp.plus ? "+" : "";
    switch (sp.type) {
        case 'x': snprintf(b, sizeof b, "%s%llx", sign, u); break;
        case 'X': snprintf(b, sizeof b, "%s%llX", sign, u); break;
        case 'o': snprintf(b, sizeof b, "%s%llo", sign, u); break;
        case 'b': {
            char d[72]; int k = 71; d[k] = 0;
            do { d[--k] = (char)('0' + (u & 1)); u >>= 1; } while (u);
            snprintf(b, sizeof b, "%s%s", sign, d + k);
            break;
        }
        case 'f': case 'e': case '%': {
            double x = uns ? (double)(unsigned long long)v : (double)v;
            if (sp.type == '%') x *= 100;
            snprintf(b, sizeof b, sp.type == 'e' ? "%s%.*e" : "%s%.*f", sp.plus && x >= 0 ? "+" : "",
                     sp.prec < 0 ? (sp.type == '%' ? 0 : 6) : sp.prec, x);
            if (sp.type == '%') strcat(b, "%");
            break;
        }
        default: snprintf(b, sizeof b, "%s%llu", sign, u);
    }
    return grav_spec_pad(b, sp, 1);
}
static const char* grav_fmt_float(double v, const char* spec) {
    GravSpec sp = grav_spec(spec);
    char b[400];
    const char* plus = sp.plus && v >= 0 ? "+" : "";
    if (sp.type == 'e') snprintf(b, sizeof b, "%s%.*e", plus, sp.prec < 0 ? 6 : sp.prec, v);
    else if (sp.type == '%') snprintf(b, sizeof b, "%s%.*f%%", plus, sp.prec < 0 ? 0 : sp.prec, v * 100);
    else if (sp.type == 'f' || sp.prec >= 0) snprintf(b, sizeof b, "%s%.*f", plus, sp.prec < 0 ? 6 : sp.prec, v);
    else { char d[32]; snprintf(b, sizeof b, "%s%s", plus, grav_dtoa(v, d)); }
    return grav_spec_pad(b, sp, 1);
}
/* Any other value: its text (taken over), cut to the precision, then padded. */
static const char* grav_fmt_text(const char* text, const char* spec) {
    GravSpec sp = grav_spec(spec);
    const char* r;
    if (sp.prec >= 0 && strlen(text) > (size_t)sp.prec) {
        char* cut = grav_str_new((size_t)sp.prec);
        memcpy(cut, text, (size_t)sp.prec);
        cut[sp.prec] = 0;
        r = grav_spec_pad(cut, sp, 0);
        grav_str_release(cut);
    } else {
        r = grav_spec_pad(text, sp, 0);
    }
    grav_str_release(text);
    return r;
}

)C";
}

// A C expression for the text of `c` (of type `t`) that the caller owns.
std::string CodeGen::displayC(const std::string &c, const TypeRef &t, bool nested) const {
    switch (t.kind) {
        case TypeRef::Kind::String:
            return nested ? "grav_str_quote(" + c + ")" : "grav_str_retain(" + c + ")";
        case TypeRef::Kind::Int:
            return t.isUnsigned ? "grav_uint_to_str((unsigned long long)(" + c + "))"
                                : "grav_int_to_str((long long)(" + c + "))";
        case TypeRef::Kind::Float:
            return (t.numBits() == 32 ? "grav_float32_to_str(" : "grav_float_to_str(") + c + ")";
        case TypeRef::Kind::Bool: return "((" + c + ") ? \"true\" : \"false\")";
        case TypeRef::Kind::Binary: return "grav_bytes_to_str(" + c + ")";
        case TypeRef::Kind::Any: return "grav_any_to_string(" + c + ")";
        case TypeRef::Kind::Null: return "\"null\"";
        case TypeRef::Kind::Function: return "\"" + escapeC(typeRefName(t)) + "\"";
        case TypeRef::Kind::Future: return "\"<future>\"";
        default: break;
    }
    if (t.isPointer()) return "grav_ptr_str((const void*)(" + c + "))";
    if (t.isArray() && !t.isSlice()) return displayFn(t) + "(" + c + ")";
    if (t.isNamed()) {
        if (reg_->isEnum(t.name)) return enumNameFn(t.name) + "(" + c + ")";
        if (reg_->isClass(t.name)) return "grav_obj_str((void*)(" + c + "))";
        if (reg_->isInterface(t.name)) return "grav_obj_str((" + c + ").obj)";
        if (reg_->isStruct(t.name)) return displayFn(t) + "(" + c + ")";
    }
    return "\"\"";
}

// A struct's name as written: `Point`, `Pair<int, string>` for a generic instance.
std::string CodeGen::displayTypeName(const std::string &fq) const {
    auto last = [](const std::string &n) { auto d = n.rfind('.'); return d == std::string::npos ? n : n.substr(d + 1); };
    auto it = program_->generics.instances.find(fq);
    if (it == program_->generics.instances.end()) return last(fq);
    std::string out = last(it->second.first) + "<";
    for (size_t i = 0; i < it->second.second.size(); ++i) {
        const TypeRef &a = it->second.second[i];
        out += (i ? ", " : "") + (a.isNamed() && a.args.empty() ? displayTypeName(a.name) : typeRefName(a));
    }
    return out + ">";
}

// `${x:spec}`: the text of `c` (of type `t`) formatted by the spec (a +1 string).
std::string CodeGen::formatC(const std::string &c, const TypeRef &t, const std::string &spec) const {
    std::string sp = "\"" + escapeC(spec) + "\"";
    if (t.kind == TypeRef::Kind::Int)
        return "grav_fmt_int((long long)(" + c + "), " + (t.isUnsigned ? "1" : "0") + ", " + sp + ")";
    if (t.kind == TypeRef::Kind::Float) return "grav_fmt_float((double)(" + c + "), " + sp + ")";
    return "grav_fmt_text(" + displayC(c, t, false) + ", " + sp + ")";
}

// The display function for a struct or fixed-array type, generated on first use.
std::string CodeGen::displayFn(const TypeRef &t) const {
    std::string key = t.isArray() ? arrayStructName(t) : structName(t.name);
    std::string fn = key + "__display";
    if (displayDone_.count(fn)) return fn;
    displayDone_.insert(fn);
    std::string ty = cTy(t);
    displayProtos_ += "static const char* " + fn + "(" + ty + " v);\n";
    std::string body = "static const char* " + fn + "(" + ty + " v) {\n    GravText t = {0, 0, 0};\n";
    if (t.isArray()) {
        body += "    grav_text_add(&t, \"[\");\n";
        body += "    for (int i = 0; i < " + std::to_string(t.arrayLen) + "; i++) {\n";
        body += "        if (i) grav_text_add(&t, \", \");\n";
        body += "        grav_text_take(&t, " + displayC("v.data[i]", *t.elem, true) + ");\n    }\n";
        body += "    grav_text_add(&t, \"]\");\n";
    } else if (const StructInfo *si = reg_->strct(t.name)) {
        std::string head = displayTypeName(t.name);
        body += "    grav_text_add(&t, \"" + escapeC(head) + (si->fields.empty() ? " {}" : " { ") + "\");\n";
        for (size_t i = 0; i < si->fields.size(); ++i) {
            const auto &f = si->fields[i];
            body += "    grav_text_add(&t, \"" + std::string(i ? ", " : "") + escapeC(f.name) + ": \");\n";
            body += "    grav_text_take(&t, " + displayC("v." + memberCName(f.name), f.type, true) + ");\n";
        }
        if (!si->fields.empty()) body += "    grav_text_add(&t, \" }\");\n";
    }
    body += "    return grav_text_done(&t);\n}\n";
    displayDefs_ += body;
    return fn;
}

} // namespace grav
