#include "codegen/codegen.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

#include "codegen/mangle.h"

namespace grav {

std::string CodeGen::generate(const Program &program, const Registry &reg) {
    reg_ = &reg;
    program_ = &program;

    emitPrelude();
    emitEnums();
    emitStructs();
    emitVTableTypes();
    emitPrototypes(program);
    // Top-level inline C (`%{ ... %}`): emitted after prototypes so it can use
    // generated types and declare globals/helpers the rest of the program shares.
    for (const auto &declPtr : program.decls)
        if (auto *cb = dynamic_cast<const CBlockDecl *>(declPtr.get()))
            cblocks_ += "/* inline C */\n" + hoistIncludes(cb->code) + "\n";
    if (!cblocks_.empty()) cblocks_ += "\n";
    emitVTableInstances();
    emitTypeInfos();
    emitInterfaceTables();
    emitGlobals();
    emitDefinitions(program);
    emitMainWrapper();

    // `hoisted_` (includes pulled out of inline-C blocks) goes right after the
    // prelude's own includes so every block can rely on them.
    std::string head = typedefs_;
    if (!hoisted_.empty()) head += "/* hoisted includes */\n" + hoisted_ + "\n";
    return head + structs_ + vtableTypes_ + protos_ + cblocks_ + vtables_ +
           globals_ + defs_;
}

void CodeGen::emitGlobals() {
    auto one = [&](const std::string &fq, const Expr *init) {
        const GlobalInfo *gi = reg_->global(fq);
        if (!gi) return;
        globals_ += "static ";
        if (gi->isConst) globals_ += "const ";
        globals_ += cTy(gi->type) + " " + gi->cName + " = " + emitAs(*init, gi->type) + ";\n";
    };
    for (const auto &declPtr : program_->decls) {
        if (auto *g = dynamic_cast<const GlobalVarDecl *>(declPtr.get())) {
            one(g->fqName, g->init.get());
        } else if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            for (const auto &sf : c->staticFields)
                one(c->fqName + "." + sf.name, sf.init.get());
        }
    }
    if (!globals_.empty()) globals_ += "\n";
}

void CodeGen::emitPrelude() {
    typedefs_ +=
        "/* Generated from Grav source by gravc. Do not edit. */\n"
        "#include <stdbool.h>\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <setjmp.h>\n"
        "#include <unistd.h>\n\n"
        "static void grav_panic(const char* m) { fprintf(stderr, \"panic: %s\\n\", m); exit(1); }\n"
        "static void grav_assert(bool c, const char* m) {\n"
        "    if (!c) { fprintf(stderr, \"assertion failed: %s\\n\", m); exit(1); }\n"
        "}\n"
        "static const char* grav_env(const char* n) { const char* v = getenv(n); return v ? v : \"\"; }\n"
        "static const char* grav_cwd(void) {\n"
        "    static char b[4096]; return getcwd(b, sizeof b) ? b : \"\";\n"
        "}\n\n"
        "typedef struct GravTypeInfo {\n"
        "    const char* name;\n"
        "    const struct GravTypeInfo* base;            /* primary base */\n"
        "    const struct GravTypeInfo* const* extra;    /* secondary bases (MI) */\n"
        "    int nextra;\n"
        "} GravTypeInfo;\n\n"
        "/* Common object header: vtable pointer + runtime type descriptor. */\n"
        "struct GravObject { const void* __vt; const GravTypeInfo* __type; };\n\n"
        "/* An interface value: the object plus its per-class method table. */\n"
        "struct GravIface { void* obj; const void* itab; };\n\n"
        "/* A binary blob: byte buffer + length (may contain embedded NULs). */\n"
        "typedef struct GravBytes { unsigned char* data; long long len; } GravBytes;\n"
        "static GravBytes grav_bytes_from_str(const char* s) {\n"
        "    long long n = (long long)strlen(s);\n"
        "    unsigned char* d = (unsigned char*)malloc((size_t)n + 1);\n"
        "    memcpy(d, s, (size_t)n + 1); GravBytes b; b.data = d; b.len = n; return b;\n"
        "}\n"
        "static const char* grav_bytes_to_str(GravBytes b) {\n"
        "    char* r = (char*)malloc((size_t)b.len + 1);\n"
        "    if (b.data) memcpy(r, b.data, (size_t)b.len);\n"
        "    r[b.len] = 0; return r;\n"
        "}\n\n"
        "static const char* grav_str_concat(const char* a, const char* b) {\n"
        "    size_t la = strlen(a), lb = strlen(b);\n"
        "    char* r = (char*)malloc(la + lb + 1);\n"
        "    memcpy(r, a, la);\n"
        "    memcpy(r + la, b, lb + 1);\n"
        "    return r;\n"
        "}\n\n"
        "static const char* grav_typename(const void* obj) {\n"
        "    return ((const struct GravObject*)obj)->__type->name;\n"
        "}\n\n"
        "static bool grav_type_is(const GravTypeInfo* t, const GravTypeInfo* want) {\n"
        "    while (t) {\n"
        "        if (t == want) return true;\n"
        "        for (int i = 0; i < t->nextra; i++)\n"
        "            if (grav_type_is(t->extra[i], want)) return true;\n"
        "        t = t->base;\n"
        "    }\n"
        "    return false;\n"
        "}\n"
        "static bool grav_is_instance(const void* obj, const GravTypeInfo* want) {\n"
        "    return grav_type_is(((const struct GravObject*)obj)->__type, want);\n"
        "}\n\n"
        "/* Value-to-string helpers (used by string interpolation / str()). */\n"
        "static const char* grav_int_to_str(long long v) {\n"
        "    char* b = (char*)malloc(24); snprintf(b, 24, \"%lld\", v); return b;\n"
        "}\n"
        "static const char* grav_uint_to_str(unsigned long long v) {\n"
        "    char* b = (char*)malloc(24); snprintf(b, 24, \"%llu\", v); return b;\n"
        "}\n"
        "static const char* grav_float_to_str(double v) {\n"
        "    char* b = (char*)malloc(32); snprintf(b, 32, \"%g\", v); return b;\n"
        "}\n\n"
        "/* Command-line arguments and stdin. */\n"
        "static int grav_argc = 0;\n"
        "static char** grav_argv = 0;\n"
        "static const char* grav_argv_at(int i) {\n"
        "    return (i >= 0 && i < grav_argc) ? grav_argv[i] : \"\";\n"
        "}\n"
        "static const char* grav_input(void) {\n"
        "    size_t cap = 128, n = 0; char* b = (char*)malloc(cap); int c;\n"
        "    while ((c = getchar()) != EOF && c != '\\n') {\n"
        "        if (n + 1 >= cap) { cap *= 2; b = (char*)realloc(b, cap); }\n"
        "        b[n++] = (char)c;\n"
        "    }\n"
        "    if (c == EOF && n == 0) { free(b); return \"\"; }\n"
        "    b[n] = 0; return b;\n"
        "}\n\n"
        "/* Exceptions: a jmp_buf stack and the in-flight exception. */\n"
        "typedef struct GravExc { void* value; const GravTypeInfo* type; } GravExc;\n"
        "static jmp_buf grav_jmp_stack[64];\n"
        "static int grav_jmp_top = 0;\n"
        "static GravExc grav_current_exc;\n"
        "static void grav_throw(void* v, const GravTypeInfo* t) {\n"
        "    grav_current_exc.value = v; grav_current_exc.type = t;\n"
        "    if (grav_jmp_top > 0) longjmp(grav_jmp_stack[grav_jmp_top - 1], 1);\n"
        "    fprintf(stderr, \"grav: uncaught exception '%s'\\n\", t ? t->name : \"?\");\n"
        "    exit(1);\n"
        "}\n\n";
}

std::string CodeGen::cTy(const TypeRef &t) const {
    // `Self` only survives in an interface's itable type; spell it as a void*.
    if (t.isNamed() && t.name == "Self") return "void*";
    if (t.isNamed() && reg_->isInterface(t.name)) return "struct GravIface";
    // Structs are value types: spelled as the bare struct, never a pointer.
    if (t.isNamed() && reg_->isStruct(t.name)) return structName(t.name);
    // Enums lower to a C enum type (an int), spelled by their tag name.
    if (t.isNamed() && reg_->isEnum(t.name)) return structName(t.name);
    // A Future<T> is resolved eagerly, so at the C level it *is* its value type.
    if (t.isFuture()) return cTy(t.elem ? *t.elem : TypeRef::prim(TypeRef::Kind::Void));
    // A pointer is the C pointer to the (interface-aware) pointee spelling.
    if (t.isPointer()) return cTy(t.elem ? *t.elem : TypeRef::prim(TypeRef::Kind::Void)) + "*";
    // A slice (variadic) is a bare pointer; a fixed-length array is a value struct.
    if (t.isSlice()) return cTy(t.elem ? *t.elem : TypeRef::prim(TypeRef::Kind::Void)) + "*";
    if (t.isArray()) return arrayStructName(t);
    return cType(t);
}

// Named class/struct/enum types lower to a pointer or value; for sizeof we want
// the underlying object/value, so spell those as the bare struct/enum.
std::string CodeGen::sizeofSpelling(const TypeRef &t) const {
    if (t.isNamed() && (reg_->isClass(t.name) || reg_->isStruct(t.name) ||
                        reg_->isEnum(t.name)))
        return structName(t.name);
    return cTy(t);
}

// ---------------------------------------------------------------------------
// Array-type collection (a pre-pass so backing structs are emitted up front)
// ---------------------------------------------------------------------------

void CodeGen::collectType(const TypeRef &t) {
    if (t.isSlice()) {
        if (t.elem) collectType(*t.elem); // a slice is a pointer, no backing struct
    } else if (t.isArray()) {
        arrayTypes_[arrayStructName(t)] = t;
        if (t.elem) collectType(*t.elem); // nested arrays / element structs-of-arrays
    } else if ((t.isPointer() || t.isFuture()) && t.elem) {
        collectType(*t.elem);
    }
}

void CodeGen::collectInExpr(const Expr &expr) {
    collectType(expr.type);
    if (auto *e = dynamic_cast<const BinaryExpr *>(&expr)) {
        collectInExpr(*e->left); collectInExpr(*e->right);
    } else if (auto *e = dynamic_cast<const UnaryExpr *>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const TernaryExpr *>(&expr)) {
        collectInExpr(*e->cond); collectInExpr(*e->thenExpr); collectInExpr(*e->elseExpr);
    } else if (auto *e = dynamic_cast<const AsExpr *>(&expr)) {
        collectType(e->target); collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const IsExpr *>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const AwaitExpr *>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const AddrOfExpr *>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const DerefExpr *>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const CoalesceExpr *>(&expr)) {
        collectInExpr(*e->left); collectInExpr(*e->right);
    } else if (auto *e = dynamic_cast<const IncDecExpr *>(&expr)) {
        collectInExpr(*e->target);
    } else if (auto *e = dynamic_cast<const CastExpr *>(&expr)) {
        collectType(e->target); collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const SizeofExpr *>(&expr)) {
        if (e->isType) collectType(e->target);
        else if (e->operand) collectInExpr(*e->operand);
    } else if (auto *e = dynamic_cast<const ArrayLiteralExpr *>(&expr)) {
        for (const auto &el : e->elements) collectInExpr(*el);
    } else if (auto *e = dynamic_cast<const IndexExpr *>(&expr)) {
        collectInExpr(*e->base); collectInExpr(*e->index);
    } else if (auto *e = dynamic_cast<const NewExpr *>(&expr)) {
        for (const auto &a : e->args) collectInExpr(*a);
    } else if (auto *e = dynamic_cast<const StructLiteralExpr *>(&expr)) {
        for (const auto &f : e->fields) collectInExpr(*f.value);
    } else if (auto *e = dynamic_cast<const CallExpr *>(&expr)) {
        if (e->callee) collectInExpr(*e->callee);
        for (const auto &a : e->args) collectInExpr(*a);
    } else if (auto *e = dynamic_cast<const MemberExpr *>(&expr)) {
        if (e->object) collectInExpr(*e->object);
    }
}

void CodeGen::collectInStmt(const Stmt &stmt) {
    if (auto *s = dynamic_cast<const LetStmt *>(&stmt)) {
        collectType(s->resolvedType);
        if (s->init) collectInExpr(*s->init);
    } else if (auto *s = dynamic_cast<const AssignStmt *>(&stmt)) {
        collectInExpr(*s->target); collectInExpr(*s->value);
    } else if (auto *s = dynamic_cast<const ReturnStmt *>(&stmt)) {
        if (s->value) collectInExpr(*s->value);
    } else if (auto *s = dynamic_cast<const ExprStmt *>(&stmt)) {
        collectInExpr(*s->expr);
    } else if (auto *s = dynamic_cast<const BlockStmt *>(&stmt)) {
        collectInBlock(s->block);
    } else if (auto *s = dynamic_cast<const IfStmt *>(&stmt)) {
        collectInExpr(*s->cond); collectInBlock(s->thenBlock);
        if (s->elseStmt) collectInStmt(*s->elseStmt);
    } else if (auto *s = dynamic_cast<const WhileStmt *>(&stmt)) {
        collectInExpr(*s->cond); collectInBlock(s->body);
    } else if (auto *s = dynamic_cast<const DoWhileStmt *>(&stmt)) {
        collectInBlock(s->body); collectInExpr(*s->cond);
    } else if (auto *s = dynamic_cast<const ForStmt *>(&stmt)) {
        if (s->init) collectInStmt(*s->init);
        if (s->cond) collectInExpr(*s->cond);
        if (s->update) collectInStmt(*s->update);
        collectInBlock(s->body);
    } else if (auto *s = dynamic_cast<const ForInStmt *>(&stmt)) {
        collectInExpr(*s->lo); collectInExpr(*s->hi); collectInBlock(s->body);
    } else if (auto *s = dynamic_cast<const SwitchStmt *>(&stmt)) {
        collectInExpr(*s->subject);
        for (const auto &c : s->cases) {
            for (const auto &v : c.values) collectInExpr(*v);
            collectInBlock(c.body);
        }
        if (s->hasDefault) collectInBlock(s->defaultBody);
    }
}

void CodeGen::collectInBlock(const Block &block) {
    for (const auto &s : block.statements) collectInStmt(*s);
}

void CodeGen::collectArrayTypes() {
    auto sig = [&](const std::vector<Param> &ps, const TypeRef &ret) {
        for (const auto &p : ps) collectType(p.type);
        collectType(ret);
    };
    for (const auto &declPtr : program_->decls) {
        Decl *d = declPtr.get();
        if (auto *st = dynamic_cast<const StructDecl *>(d)) {
            for (const auto &f : st->fields) collectType(f.type);
        } else if (auto *c = dynamic_cast<const ClassDecl *>(d)) {
            for (const auto &f : c->fields) collectType(f.type);
            if (c->constructor.present) {
                for (const auto &p : c->constructor.params) collectType(p.type);
                collectInBlock(c->constructor.body);
            }
            for (const auto &m : c->methods) {
                sig(m.params, m.returnType);
                if (m.hasBody) collectInBlock(m.body);
            }
        } else if (auto *fn = dynamic_cast<const FunctionDecl *>(d)) {
            sig(fn->params, fn->returnType);
            collectInBlock(fn->body);
        }
    }
}

// Enums become real C enum typedefs. They have no type dependencies, so they
// are emitted up front (into typedefs_) ahead of structs and prototypes.
void CodeGen::emitEnums() {
    bool any = false;
    for (const auto &declPtr : program_->decls) {
        auto *en = dynamic_cast<const EnumDecl *>(declPtr.get());
        if (!en) continue;
        any = true;
        std::string s = structName(en->fqName);
        typedefs_ += "typedef enum " + s + " {\n";
        if (en->members.empty())
            typedefs_ += "    " + s + "__empty /* empty enum */\n";
        for (size_t i = 0; i < en->members.size(); ++i) {
            typedefs_ += "    " + enumConst(en->fqName, en->members[i].name);
            typedefs_ += i + 1 < en->members.size() ? ",\n" : "\n";
        }
        typedefs_ += "} " + s + ";\n";
    }
    if (any) typedefs_ += "\n";
}

std::vector<FieldInfo> CodeGen::collectFields(const std::string &classFq) const {
    std::vector<FieldInfo> out;
    const ClassInfo *ci = reg_->cls(classFq);
    if (!ci) return out;
    if (!ci->baseClass.empty()) out = collectFields(ci->baseClass);
    for (const auto &f : ci->fields) out.push_back(f);
    return out;
}

void CodeGen::emitStructs() {
    collectArrayTypes(); // discover array types so their backing structs precede use
    // Forward typedefs so class/struct names can appear anywhere. Interfaces have
    // no storage of their own, so they alias the common object header.
    for (const auto &declPtr : program_->decls) {
        if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            std::string s = structName(c->fqName);
            typedefs_ += "typedef struct " + s + " " + s + ";\n";
        } else if (auto *st = dynamic_cast<const StructDecl *>(declPtr.get())) {
            std::string s = structName(st->fqName);
            typedefs_ += "typedef struct " + s + " " + s + ";\n";
        }
        // Interfaces compile to the GravIface fat-pointer value (no own struct).
    }
    typedefs_ += "\n";

    // Value structs first: a struct with a struct-typed field needs that field's
    // type fully defined (it is stored by value), so emit them dependency-ordered.
    emitValueStructs();

    for (const auto &declPtr : program_->decls) {
        if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            emitStruct(c->fqName);
        }
    }
}

// Emits plain value `struct` types *and* the backing structs for fixed-length
// array types, both by value. A value struct stores struct- and array-typed
// fields inline, so every such dependency must be fully defined first; this
// walks the value-containment graph and emits in dependency order.
void CodeGen::emitValueStructs() {
    std::unordered_set<std::string> done; // keyed by C struct name
    std::function<void(const TypeRef &)> ensure;
    std::function<void(const std::string &)> emitStructFq = [&](const std::string &fq) {
        std::string key = structName(fq);
        if (done.count(key)) return;
        done.insert(key);
        const StructInfo *si = reg_->strct(fq);
        if (!si) return;
        for (const auto &f : si->fields) ensure(f.type);
        structs_ += "struct " + key + " {\n";
        if (si->fields.empty())
            structs_ += "    char __empty; /* C has no zero-size structs */\n";
        for (const auto &f : si->fields)
            structs_ += "    " + cTy(f.type) + " " + memberCName(f.name) + ";\n";
        structs_ += "};\n\n";
    };
    std::function<void(const TypeRef &)> emitArray = [&](const TypeRef &t) {
        std::string key = arrayStructName(t);
        if (done.count(key)) return;
        done.insert(key);
        if (t.elem) ensure(*t.elem);
        structs_ += "typedef struct " + key + " {\n    " + cTy(*t.elem) + " data[" +
                    std::to_string(t.arrayLen) + "];\n} " + key + ";\n\n";
    };
    ensure = [&](const TypeRef &t) {
        // Only by-value containment forces an ordering: value structs and arrays.
        // Pointers/classes/interfaces only need the (already-emitted) forward typedef.
        if (t.isArray()) emitArray(t);
        else if (t.isNamed() && reg_->isStruct(t.name)) emitStructFq(t.name);
    };
    for (const auto &declPtr : program_->decls)
        if (auto *st = dynamic_cast<const StructDecl *>(declPtr.get())) emitStructFq(st->fqName);
    for (const auto &[key, t] : arrayTypes_) emitArray(t);
}

void CodeGen::emitStruct(const std::string &classFq) {
    std::string s = structName(classFq);
    structs_ += "struct " + s + " {\n";
    structs_ += "    const void* __vt;\n";
    structs_ += "    const GravTypeInfo* __type;\n";
    for (const auto &f : collectFields(classFq)) {
        structs_ += "    " + cTy(f.type) + " " + memberCName(f.name) + ";\n";
    }
    structs_ += "};\n\n";
}

std::string CodeGen::slotFnType(const VTableSlot &slot) const {
    const MethodInfo *mi = reg_->findMethod(slot.slotOwner, slot.name);
    std::string ret = mi ? cTy(mi->returnType) : "void";
    std::string params = structName(slot.slotOwner) + "*";
    if (mi) {
        for (const auto &pt : mi->paramTypes) params += ", " + cTy(pt);
    }
    return ret + " (*)(" + params + ")";
}

void CodeGen::emitVTableTypes() {
    for (const auto &declPtr : program_->decls) {
        auto *c = dynamic_cast<const ClassDecl *>(declPtr.get());
        if (!c) continue;
        const ClassInfo *ci = reg_->cls(c->fqName);
        if (ci) emitVTableType(*ci);
    }
}

void CodeGen::emitVTableType(const ClassInfo &ci) {
    if (ci.slots.empty()) return;
    std::string vt = vtableType(ci.fqName);
    vtableTypes_ += "typedef struct " + vt + " {\n";
    for (const auto &slot : ci.slots) {
        std::string sig = slotFnType(slot); // "RET (*)(params)"
        std::string member = sig;
        auto pos = member.find("(*)");
        if (pos != std::string::npos) member.replace(pos, 3, "(*" + memberCName(slot.name) + ")");
        vtableTypes_ += "    " + member + ";\n";
    }
    vtableTypes_ += "} " + vt + ";\n\n";
}

void CodeGen::emitVTableInstances() {
    for (const auto &declPtr : program_->decls) {
        auto *c = dynamic_cast<const ClassDecl *>(declPtr.get());
        if (!c) continue;
        const ClassInfo *ci = reg_->cls(c->fqName);
        if (ci) emitVTableInstance(*ci);
    }
}

void CodeGen::emitTypeInfos() {
    std::unordered_set<std::string> done;
    std::function<void(const std::string &)> emit = [&](const std::string &fq) {
        if (done.count(fq)) return;
        done.insert(fq);
        const ClassInfo *ci = reg_->cls(fq);
        if (!ci) return;
        std::string base = "0";
        if (!ci->baseClass.empty()) {
            emit(ci->baseClass); // base descriptor must precede derived
            base = "&" + mangle(ci->baseClass) + "_typeinfo";
        }
        // Secondary bases (multiple inheritance) -> a static array of descriptors.
        std::string extra = "0";
        int nextra = 0;
        for (const auto &eb : ci->extraBases) if (!eb.empty()) ++nextra;
        if (nextra > 0) {
            std::string arr = mangle(fq) + "_extra";
            std::string elems;
            for (const auto &eb : ci->extraBases) {
                if (eb.empty()) continue;
                emit(eb); // ensure base descriptor precedes this one
                elems += (elems.empty() ? "" : ", ") + std::string("&") + mangle(eb) + "_typeinfo";
            }
            vtables_ += "static const GravTypeInfo* const " + arr + "[] = { " + elems + " };\n";
            extra = arr;
        }
        vtables_ += "static const GravTypeInfo " + mangle(fq) +
                    "_typeinfo = { \"" + fq + "\", " + base + ", " + extra + ", " +
                    std::to_string(nextra) + " };\n";
    };
    for (const auto &declPtr : program_->decls) {
        if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) emit(c->fqName);
    }
    vtables_ += "\n";
}

void CodeGen::emitInterfaceTables() {
    auto itabType = [](const std::string &i) { return mangle(i) + "_ITAB"; };

    // One method-table struct type per interface.
    for (const auto &declPtr : program_->decls) {
        auto *id = dynamic_cast<const InterfaceDecl *>(declPtr.get());
        if (!id) continue;
        const InterfaceInfo *ii = reg_->iface(id->fqName);
        if (!ii) continue;
        vtableTypes_ += "typedef struct " + itabType(id->fqName) + " {\n";
        for (const auto &m : ii->methods) {
            std::string params = "void*";
            for (const auto &pt : m.paramTypes) params += ", " + cTy(pt);
            vtableTypes_ += "    " + cTy(m.returnType) + " (*" + memberCName(m.name) + ")(" +
                            params + ");\n";
        }
        vtableTypes_ += "} " + itabType(id->fqName) + ";\n\n";
    }

    // One table instance per (class, interface-it-implements) pair.
    for (const auto &declPtr : program_->decls) {
        auto *c = dynamic_cast<const ClassDecl *>(declPtr.get());
        if (!c) continue;
        // Collect interfaces from the whole inheritance chain.
        std::vector<std::string> ifaces;
        std::string cur = c->fqName;
        int guard = 0;
        while (!cur.empty() && guard++ < 100) {
            const ClassInfo *ci = reg_->cls(cur);
            if (!ci) break;
            for (const auto &i : ci->interfaces)
                if (!i.empty() &&
                    std::find(ifaces.begin(), ifaces.end(), i) == ifaces.end())
                    ifaces.push_back(i);
            cur = ci->baseClass;
        }
        for (const auto &ifq : ifaces) {
            const InterfaceInfo *ii = reg_->iface(ifq);
            if (!ii) continue;
            vtables_ += "static " + itabType(ifq) + " " + mangle(c->fqName) +
                        "__itab__" + mangle(ifq) + " = {\n";
            for (const auto &m : ii->methods) {
                std::string impl = reg_->findMethodImpl(c->fqName, m.name);
                std::string sig = cTy(m.returnType) + " (*)(void*";
                for (const auto &pt : m.paramTypes) sig += ", " + cTy(pt);
                sig += ")";
                if (impl.empty())
                    vtables_ += "    0,\n";
                else
                    vtables_ += "    (" + sig + ")" + methodCName(impl, m.name) + ",\n";
            }
            vtables_ += "};\n\n";
        }
    }
}

void CodeGen::emitVTableInstance(const ClassInfo &ci) {
    if (ci.slots.empty()) return;
    vtables_ += "static " + vtableType(ci.fqName) + " " +
                vtableInstance(ci.fqName) + " = {\n";
    for (const auto &slot : ci.slots) {
        std::string impl = reg_->findMethodImpl(ci.fqName, slot.name);
        if (impl.empty()) {
            vtables_ += "    0,\n";
        } else {
            vtables_ += "    (" + slotFnType(slot) + ")" +
                        methodCName(impl, slot.name) + ",\n";
        }
    }
    vtables_ += "};\n\n";
}

} // namespace grav
