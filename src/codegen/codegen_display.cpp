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
