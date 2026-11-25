#include "codegen/codegen.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

#include "codegen/mangle.h"

namespace grav {

// A parameter's default as C, when it is a literal the type takes as it is
// (numbers, bools, text, and null for an object); "" otherwise.
static std::string literalDefault(const Expr &e, const TypeRef &t, const std::function<std::string(const std::string &)> &esc) {
    if (auto *i = as<const IntLiteralExpr>(&e)) return t.isNullable() ? "" : i->raw;
    if (auto *f = as<const FloatLiteralExpr>(&e)) return t.isNullable() ? "" : f->raw;
    if (auto *b = as<const BoolLiteralExpr>(&e)) return t.isNullable() ? "" : (b->value ? "true" : "false");
    if (auto *s = as<const StringLiteralExpr>(&e)) return t.isNullable() ? "" : "\"" + esc(s->value) + "\"";
    if (as<const NullLiteralExpr>(&e)) return t.isNamed() || t.kind == TypeRef::Kind::String ? "0" : "";
    // (`-1` is `0 - 1` by now)
    if (auto *b = as<const BinaryExpr>(&e))
        if (b->op == BinaryOp::Sub && b->left && b->right)
            if (auto *z = as<const IntLiteralExpr>(b->left.get()); z && z->raw == "0")
                if (std::string v = literalDefault(*b->right, t, esc); !v.empty() && v[0] != '"' && v != "true" && v != "false") return "(-" + v + ")";
    return "";
}

std::string CodeGen::generate(const Program &program, const Registry &reg,
                              const std::vector<SourceMapEntry> &srcMap) {
    reg_ = &reg;
    program_ = &program;
    srcMap_ = srcMap;

    emitPrelude();
    emitFaultSupport();
    emitDisplaySupport();
    emitEnums();
    emitStructs();
    emitVTableTypes();
    emitPrototypes(program);
    // Top-level inline C (`%{ ... %}`): emitted after prototypes so it can use
    // generated types and declare globals/helpers the rest of the program shares.
    for (const auto &declPtr : program.decls)
        if (auto *cb = as<const CBlockDecl>(declPtr.get()))
            cblocks_ += "/* inline C */\n" + hoistIncludes(cb->code) + "\n";
    if (!cblocks_.empty()) cblocks_ += "\n";
    emitVTableInstances();
    emitTypeInfos();
    emitInterfaceTables();
    emitGlobals();
    emitLambdas(); // lifted closures (their creation sites come later)
    emitDefinitions(program);
    emitGlobalInits();
    emitFaultRaisers();
    emitMainWrapper();

    // `hoisted_` (includes pulled out of inline-C blocks) goes right after the
    // prelude's own includes so every block can rely on them.
    std::string head = typedefs_;
    if (!hoisted_.empty()) head += "/* hoisted includes */\n" + hoisted_ + "\n";
    std::string thunks;
    if (!lambdaDecls_.empty()) thunks += "/* closures: environments and constructors */\n" + lambdaDecls_;
    if (!asyncDecls_.empty()) thunks += "/* async functions: task records and spawners */\n" + asyncDecls_;
    if (!thunks_.empty()) {
        thunks += "/* dispatch thunks: evaluate the receiver once, then dispatch */\n";
        for (const auto &[name, def] : thunks_) thunks += def;
        thunks += "\n";
    }
    std::string display;
    if (!displayProtos_.empty()) display = "/* display of struct and array values */\n" + displayProtos_ + displayDefs_ + "\n";
    if (!rfDefs_.empty()) thunks += "/* reflection: calling methods and constructors */\n" + rfDefs_ + "\n";
    return head + structs_ + vtableTypes_ + protos_ + cblocks_ + vtables_ + thunks +
           globals_ + display + defs_;
}

SourceMapEntry CodeGen::origin(int combinedLine) const {
    if (combinedLine >= 1 && combinedLine <= static_cast<int>(srcMap_.size()))
        return srcMap_[combinedLine - 1];
    return {"<unknown>", combinedLine};
}

// Whether C can initialize a global with `e` itself: literals, arithmetic on
// them, enum members, and array or struct literals of those.
bool CodeGen::isStaticInit(const Expr &e) const {
    if (as<const IntLiteralExpr>(&e) || as<const FloatLiteralExpr>(&e) ||
        as<const BoolLiteralExpr>(&e) || as<const NullLiteralExpr>(&e) ||
        as<const StringLiteralExpr>(&e))
        return true;
    if (auto *b = as<const BinaryExpr>(&e))
        return !b->stringConcat && b->overloadMethod.empty() && refKind(e.type) == RefKind::None &&
               isStaticInit(*b->left) && isStaticInit(*b->right);
    if (auto *u = as<const UnaryExpr>(&e)) return isStaticInit(*u->operand);
    if (auto *t = as<const TernaryExpr>(&e))
        return refKind(e.type) == RefKind::None && isStaticInit(*t->cond) && isStaticInit(*t->thenExpr) && isStaticInit(*t->elseExpr);
    if (auto *m = as<const MemberExpr>(&e)) return m->kind == MemberKind::EnumValue;
    if (auto *a = as<const ArrayLiteralExpr>(&e)) {
        if (a->built) return false;
        for (const auto &x : a->elements) if (!isStaticInit(*x)) return false;
        return true;
    }
    if (auto *s = as<const StructLiteralExpr>(&e)) {
        for (const auto &f : s->fields) if (!isStaticInit(*f.value)) return false;
        return true;
    }
    return false;
}

void CodeGen::emitGlobals() {
    auto one = [&](const std::string &fq, const Expr *init, int line) {
        const GlobalInfo *gi = reg_->global(fq);
        if (!gi) return;
        if (!isStaticInit(*init)) {
            // Set at startup (grav_init_globals); until then the type's zero.
            std::string ct = cTy(gi->type);
            bool scalar = !ct.empty() && ct.back() == '*';
            if (gi->type.kind == TypeRef::Kind::Int || gi->type.kind == TypeRef::Kind::Float ||
                gi->type.kind == TypeRef::Kind::Bool || (gi->type.isNamed() && reg_->isEnum(gi->type.name)))
                scalar = true;
            std::string zero = gi->type.kind == TypeRef::Kind::String ? "\"\"" : scalar ? "0" : "{0}";
            globals_ += "static GRAV_UNUSED " + ct + " " + gi->cName + " = " + zero + ";\n";
            dynamicGlobals_.push_back({gi->cName, gi->type, init, line});
            return;
        }
        // `T const name`: qualifies the object itself (for strings that yields
        // `const char* const`, not the invalid-looking `const const char*`).
        globals_ += "static GRAV_UNUSED " + cTy(gi->type) + (gi->isConst ? " const " : " ") +
                    gi->cName + " = " + emitOwned(*init, gi->type) + ";\n";
    };
    for (const auto &declPtr : program_->decls) {
        if (auto *g = as<const GlobalVarDecl>(declPtr.get())) {
            one(g->fqName, g->init.get(), g->line);
        } else if (auto *c = as<const ClassDecl>(declPtr.get())) {
            for (const auto &sf : c->staticFields)
                one(c->fqName + "." + sf.name, sf.init.get(), sf.line);
        }
    }
    if (!globals_.empty()) globals_ += "\n";
}

std::string CodeGen::equalExpr(const TypeRef &t, const std::string &a, const std::string &b) const {
    if (t.kind == TypeRef::Kind::String) return "grav_streq(" + a + ", " + b + ")";
    if (t.isNullable())
        return t.elem->kind == TypeRef::Kind::String ? "grav_streq_opt(" + a + ", " + b + ")"
                                                     : optStructName(t) + "__eq(" + a + ", " + b + ")";
    if (t.isBinary()) return "grav_bytes_eq(" + a + ", " + b + ")";
    if (t.isFunction()) return "grav_fn_eq(" + a + ", " + b + ")";
    if (t.isNamed() && reg_->isInterface(t.name)) return "grav_iface_eq(" + a + ", " + b + ")";
    if (t.isNamed() && reg_->isStruct(t.name)) return structName(t.name) + "__eq(" + a + ", " + b + ")";
    if (t.isAny()) return "grav_any_eq(" + a + ", " + b + ")";
    if (t.isArray() && !t.isSlice())
        return "(memcmp(&(" + a + "), &(" + b + "), sizeof(" + a + ")) == 0)";
    return "(" + a + " == " + b + ")";
}

std::string CodeGen::cTy(const TypeRef &t) const {
    // `Self` only survives in an interface's itable type; spell it as a void*.
    if (t.isNamed() && t.name == "Self") return "void*";
    if (t.isNamed() && reg_->isInterface(t.name)) return "struct GravIface";
    // Structs are value types: spelled as the bare struct, never a pointer.
    if (t.isNamed() && reg_->isStruct(t.name)) return structName(t.name);
    // Enums lower to a C enum type (an int), spelled by their tag name.
    if (t.isNamed() && reg_->isEnum(t.name)) return structName(t.name);
    // A Future<T> is a handle to a task on the worker pool.
    if (t.isFuture()) return "GravTask*";
    // A pointer is the C pointer to the (interface-aware) pointee spelling.
    if (t.isPointer()) return cTy(t.elem ? *t.elem : TypeRef::prim(TypeRef::Kind::Void)) + "*";
    // A slice (variadic) is a bare pointer; a fixed-length array is a value struct.
    if (t.isSlice()) return cTy(t.elem ? *t.elem : TypeRef::prim(TypeRef::Kind::Void)) + "*";
    if (t.isArray()) return arrayStructName(t);
    if (t.isNullable() && t.elem->kind != TypeRef::Kind::String) return optStructName(t);
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
    } else if (t.isNullable() && t.elem) {
        if (t.elem->kind != TypeRef::Kind::String) optTypes_[optStructName(t)] = t;
        collectType(*t.elem);
    } else if ((t.isPointer() || t.isFuture()) && t.elem) {
        collectType(*t.elem);
    } else if (t.isFunction()) {
        for (const auto &p : t.fnParams) collectType(p);
        if (t.elem) collectType(*t.elem);
    }
}

void CodeGen::collectInExpr(const Expr &expr) {
    collectType(expr.type);
    if (auto *e = as<const BinaryExpr>(&expr)) {
        collectInExpr(*e->left); collectInExpr(*e->right);
    } else if (auto *e = as<const UnaryExpr>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = as<const TernaryExpr>(&expr)) {
        collectInExpr(*e->cond); collectInExpr(*e->thenExpr); collectInExpr(*e->elseExpr);
    } else if (auto *e = as<const AsExpr>(&expr)) {
        collectType(e->target); collectInExpr(*e->operand);
    } else if (auto *e = as<const IsExpr>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = as<const AwaitExpr>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = as<const AddrOfExpr>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = as<const DerefExpr>(&expr)) {
        collectInExpr(*e->operand);
    } else if (auto *e = as<const CoalesceExpr>(&expr)) {
        collectInExpr(*e->left); collectInExpr(*e->right);
    } else if (auto *e = as<const IncDecExpr>(&expr)) {
        collectInExpr(*e->target);
    } else if (auto *e = as<const CastExpr>(&expr)) {
        collectType(e->target); collectInExpr(*e->operand);
    } else if (auto *e = as<const SizeofExpr>(&expr)) {
        if (e->isType) collectType(e->target);
        else if (e->operand) collectInExpr(*e->operand);
    } else if (auto *e = as<const ArrayLiteralExpr>(&expr)) {
        if (e->built) { collectInExpr(*e->built); return; } // a collection literal
        for (const auto &el : e->elements) collectInExpr(*el);
    } else if (auto *e = as<const IndexExpr>(&expr)) {
        collectInExpr(*e->base); collectInExpr(*e->index);
        for (const auto &m : e->more) collectInExpr(*m);
    } else if (auto *e = as<const NewExpr>(&expr)) {
        for (const auto &a : e->args) collectInExpr(*a);
    } else if (auto *e = as<const StructLiteralExpr>(&expr)) {
        for (const auto &f : e->fields) collectInExpr(*f.value);
    } else if (auto *e = as<const CallExpr>(&expr)) {
        if (e->callee) collectInExpr(*e->callee);
        for (const auto &a : e->args) collectInExpr(*a);
    } else if (auto *e = as<const MemberExpr>(&expr)) {
        if (e->bound) { collectInExpr(*e->bound); return; } // a method used as a value
        if (e->object) collectInExpr(*e->object);
    } else if (auto *e = as<const LambdaExpr>(&expr)) {
        // Inner lambdas first, so each is lifted before the code that creates it.
        for (const auto &p : e->params) collectType(p.type);
        for (const auto &c : e->captures) collectType(c.type);
        collectType(e->returnType);
        if (e->exprBody) collectInExpr(*e->exprBody);
        collectInBlock(e->body);
        lambdas_.push_back(e);
        lambdaNames_[e] = lambdaOwner_ + "_" + std::to_string(lambdaOrdinal_[lambdaOwner_]++);
    }
}

void CodeGen::collectInStmt(const Stmt &stmt) {
    if (auto *s = as<const LetStmt>(&stmt)) {
        collectType(s->resolvedType);
        if (s->init) collectInExpr(*s->init);
    } else if (auto *s = as<const AssignStmt>(&stmt)) {
        collectInExpr(*s->target); collectInExpr(*s->value);
    } else if (auto *s = as<const ReturnStmt>(&stmt)) {
        if (s->value) collectInExpr(*s->value);
    } else if (auto *s = as<const ExprStmt>(&stmt)) {
        collectInExpr(*s->expr);
    } else if (auto *s = as<const BlockStmt>(&stmt)) {
        collectInBlock(s->block);
    } else if (auto *s = as<const IfStmt>(&stmt)) {
        collectInExpr(*s->cond); collectInBlock(s->thenBlock);
        if (s->elseStmt) collectInStmt(*s->elseStmt);
    } else if (auto *s = as<const WhileStmt>(&stmt)) {
        collectInExpr(*s->cond); collectInBlock(s->body);
    } else if (auto *s = as<const DoWhileStmt>(&stmt)) {
        collectInBlock(s->body); collectInExpr(*s->cond);
    } else if (auto *s = as<const ForStmt>(&stmt)) {
        if (s->init) collectInStmt(*s->init);
        if (s->cond) collectInExpr(*s->cond);
        if (s->update) collectInStmt(*s->update);
        collectInBlock(s->body);
    } else if (auto *s = as<const ForInStmt>(&stmt)) {
        collectInExpr(*s->lo);
        if (s->hi) collectInExpr(*s->hi);
        if (s->step) collectInExpr(*s->step);
        collectType(s->varType);
        collectInBlock(s->body);
    } else if (auto *s = as<const DeferStmt>(&stmt)) {
        collectInBlock(s->body);
    } else if (auto *s = as<const ThrowStmt>(&stmt)) {
        collectInExpr(*s->value);
    } else if (auto *s = as<const TryStmt>(&stmt)) {
        collectInBlock(s->tryBlock);
        for (const auto &c : s->catches) collectInBlock(c.block);
        collectInBlock(s->finallyBlock);
    } else if (auto *s = as<const SwitchStmt>(&stmt)) {
        collectInExpr(*s->subject);
        for (const auto &c : s->cases) {
            for (const auto &v : c.values) collectInExpr(*v);
            for (const auto &h : c.rangeHi) if (h) collectInExpr(*h);
            if (c.guard) collectInExpr(*c.guard);
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
    // (a test's name as an identifier: a hash of it, so tests may be renamed or reordered)
    auto nameHash = [](const std::string &s) {
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char ch : s) h = (h ^ ch) * 1099511628211ULL;
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx", h);
        return std::string(buf);
    };
    for (const auto &declPtr : program_->decls) {
        Decl *d = declPtr.get();
        if (auto *st = as<const StructDecl>(d)) {
            for (const auto &f : st->fields) collectType(f.type);
        } else if (auto *c = as<const ClassDecl>(d)) {
            std::string cls = mangle(c->fqName);
            for (const auto &f : c->fields) collectType(f.type);
            if (c->constructor.present) {
                for (const auto &p : c->constructor.params) collectType(p.type);
                lambdaOwner_ = cls + "_new";
                collectInBlock(c->constructor.body);
            }
            for (size_t i = 0; i < c->extraCtors.size(); ++i) {
                for (const auto &p : c->extraCtors[i].params) collectType(p.type);
                lambdaOwner_ = cls + "_new" + std::to_string(i + 1);
                collectInBlock(c->extraCtors[i].body);
            }
            lambdaOwner_ = cls + "_drop";
            if (c->destructor.present) collectInBlock(c->destructor.body);
            for (const auto &m : c->methods) {
                sig(m.params, m.returnType);
                lambdaOwner_ = cls + "_m_" + memberCName(m.name);
                if (m.hasBody) collectInBlock(m.body);
            }
            for (const auto &sf : c->staticFields) {        // (initializers may hold lambdas: `= [..]` builds)
                collectType(sf.type);
                lambdaOwner_ = cls + "_s_" + sf.name;
                if (sf.init) collectInExpr(*sf.init);
            }
        } else if (auto *g = as<const GlobalVarDecl>(d)) {
            collectType(g->resolvedType);
            lambdaOwner_ = mangle(g->fqName);
            if (g->init) collectInExpr(*g->init);
        } else if (auto *t = as<const TestDecl>(d)) {
            lambdaOwner_ = "test_" + nameHash(t->name);
            collectInBlock(t->body);
        } else if (auto *fn = as<const FunctionDecl>(d)) {
            sig(fn->params, fn->returnType);
            lambdaOwner_ = mangle(fn->fqName) + (fn->overloadIndex ? "__ov" + std::to_string(fn->overloadIndex) : "");
            collectInBlock(fn->body);
        }
    }
}

// Enums become real C enum typedefs. They have no type dependencies, so they
// are emitted up front (into typedefs_) ahead of structs and prototypes.
void CodeGen::emitEnums() {
    bool any = false;
    for (const auto &declPtr : program_->decls) {
        auto *en = as<const EnumDecl>(declPtr.get());
        if (!en) continue;
        any = true;
        std::string s = structName(en->fqName);
        typedefs_ += "typedef enum " + s + " {\n";
        if (en->members.empty())
            typedefs_ += "    " + s + "__empty /* empty enum */\n";
        for (size_t i = 0; i < en->members.size(); ++i) {
            typedefs_ += "    " + enumConst(en->fqName, en->members[i].name);
            if (en->customValues) typedefs_ += " = " + std::to_string(en->members[i].value);
            typedefs_ += i + 1 < en->members.size() ? ",\n" : "\n";
        }
        typedefs_ += "} " + s + ";\n";
        // Member-name lookup for print/str/interpolation of enum values.
        typedefs_ += "static inline const char* " + enumNameFn(en->fqName) + "(" + s + " v) {\n";
        if (en->customValues) {
            typedefs_ += "    switch ((long long)v) {\n";
            for (const auto &m : en->members)
                typedefs_ += "        case " + std::to_string(m.value) + "LL: return \"" + m.name + "\";\n";
            typedefs_ += "        default: return \"?\";\n    }\n}\n";
            // `n as E`: the number must be one of the members' values.
            typedefs_ += "static inline long long " + structName(en->fqName) + "__check(long long v) {\n    switch (v) {\n";
            for (const auto &m : en->members) typedefs_ += "        case " + std::to_string(m.value) + "LL:\n";
            typedefs_ += "            return v;\n        default: grav_fault_enum(v, -1, \"" + en->fqName + "\");\n    }\n}\n";
        } else {
            typedefs_ += "    static const char* const names[] = { ";
            for (size_t i = 0; i < en->members.size(); ++i)
                typedefs_ += (i ? ", \"" : "\"") + en->members[i].name + "\"";
            if (en->members.empty()) typedefs_ += "\"?\"";
            typedefs_ += " };\n    return (unsigned)v < sizeof names / sizeof names[0] ? names[v] : \"?\";\n}\n";
        }
        // Same lookup with the signature an `any` box stores.
        typedefs_ += "static inline GRAV_UNUSED const char* " + enumNameFn(en->fqName) + "_any(long long v) { return " +
                     enumNameFn(en->fqName) + "((" + s + ")v); }\n";
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
        if (auto *c = as<const ClassDecl>(declPtr.get())) {
            std::string s = structName(c->fqName);
            typedefs_ += "typedef struct " + s + " " + s + ";\n";
        } else if (auto *st = as<const StructDecl>(declPtr.get())) {
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
        if (auto *c = as<const ClassDecl>(declPtr.get())) {
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
        structs_ += "};\n";
        // Member-wise `==` for the struct (C has no struct comparison).
        std::string body = si->fields.empty() ? "1" : "";
        for (size_t i = 0; i < si->fields.size(); ++i) {
            const auto &f = si->fields[i];
            std::string m = memberCName(f.name);
            body += (i ? " && " : "") + equalExpr(f.type, "a." + m, "b." + m);
        }
        structs_ += "static inline GRAV_UNUSED bool " + key + "__eq(" + key + " a, " + key +
                    " b) { (void)a; (void)b; return " + body + "; }\n\n";
        TypeRef st = TypeRef::named(fq);
        if (refKind(st) == RefKind::Value) structs_ += emitValueArc(key, st);
    };
    std::function<void(const TypeRef &)> emitArray = [&](const TypeRef &t) {
        std::string key = arrayStructName(t);
        if (done.count(key)) return;
        done.insert(key);
        if (t.elem) ensure(*t.elem);
        structs_ += "typedef struct " + key + " {\n    " + cTy(*t.elem) + " data[" +
                    std::to_string(t.arrayLen) + "];\n} " + key + ";\n\n";
        if (refKind(t) == RefKind::Value) structs_ += emitValueArc(key, t);
    };
    // `T?` of a value type: { has, v }, with `==` against another T? (__eq) or a T (__eqv).
    std::function<void(const TypeRef &)> emitOpt = [&](const TypeRef &t) {
        std::string key = optStructName(t);
        if (done.count(key)) return;
        done.insert(key);
        ensure(*t.elem);
        std::string v = cTy(*t.elem);
        structs_ += "typedef struct " + key + " { bool has; " + v + " v; } " + key + ";\n";
        structs_ += "static inline GRAV_UNUSED bool " + key + "__eq(" + key + " a, " + key +
                    " b) { return a.has == b.has && (!a.has || " + equalExpr(*t.elem, "a.v", "b.v") + "); }\n";
        structs_ += "static inline GRAV_UNUSED bool " + key + "__eqv(" + key + " a, " + v +
                    " b) { return a.has && " + equalExpr(*t.elem, "a.v", "b") + "; }\n\n";
    };
    ensure = [&](const TypeRef &t) {
        // Only by-value containment forces an ordering: value structs and arrays.
        // Pointers/classes/interfaces only need the (already-emitted) forward typedef.
        if (t.isArray()) emitArray(t);
        else if (t.isNullable() && t.elem->kind != TypeRef::Kind::String) emitOpt(t);
        else if (t.isNamed() && reg_->isStruct(t.name)) emitStructFq(t.name);
    };
    for (const auto &declPtr : program_->decls)
        if (auto *st = as<const StructDecl>(declPtr.get())) emitStructFq(st->fqName);
    for (const auto &[key, t] : arrayTypes_) emitArray(t);
    for (const auto &[key, t] : optTypes_) emitOpt(t);
}

void CodeGen::emitStruct(const std::string &classFq) {
    std::string s = structName(classFq);
    structs_ += "struct " + s + " {\n";
    structs_ += "    const void* __vt;\n";
    structs_ += "    const GravTypeInfo* __type;\n";
    structs_ += "    long long __rc;\n";
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
        auto *c = as<const ClassDecl>(declPtr.get());
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
        auto *c = as<const ClassDecl>(declPtr.get());
        if (!c) continue;
        const ClassInfo *ci = reg_->cls(c->fqName);
        if (ci) emitVTableInstance(*ci);
    }
}

void CodeGen::emitTypeInfos() {
    // Forward declarations: hooks may name another class's descriptor.
    for (const auto &declPtr : program_->decls)
        if (auto *c = as<const ClassDecl>(declPtr.get()))
            vtables_ += "static const GravTypeInfo " + mangle(c->fqName) + "_typeinfo;\n";
    std::unordered_set<std::string> done;
    std::string reflectedTypes; // "&A_typeinfo, &B_typeinfo, " for every reflected class
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
        // The destructor chain `free(obj)` runs (nearest class with a destructor).
        std::string dtor = reg_->findDestructor(fq);
        std::string drop = dtor.empty() ? "0" : "(void (*)(void*))" + mangle(dtor) + "__drop";
        // toString() for printing a boxed `any` through its dynamic class.
        std::string strHook = "0";
        if (const MethodInfo *ts = reg_->findMethod(fq, "toString");
            ts && !ts->isStatic && ts->paramTypes.empty() && ts->returnType.kind == TypeRef::Kind::String) {
            std::string impl = reg_->findMethodImpl(fq, ts->name);
            if (!impl.empty()) {
                std::string w = mangle(fq) + "__any_str";
                vtables_ += "static const char* " + w + "(void* o) { return " + methodCName(impl, ts->name) +
                            "((" + structName(impl) + "*)o); }\n";
                strHook = w;
            }
        }
        // operator==(other) for comparing boxed `any` values (and assertEq).
        std::string eqHook = "0";
        if (const MethodInfo *eq = reg_->findMethod(fq, "op_eq");
            eq && !eq->isStatic && eq->paramTypes.size() == 1 && eq->paramTypes[0].isNamed() &&
            reg_->isClass(eq->paramTypes[0].name) && eq->returnType.kind == TypeRef::Kind::Bool) {
            std::string impl = reg_->findMethodImpl(fq, eq->name);
            const std::string &pt = eq->paramTypes[0].name;
            if (!impl.empty()) {
                std::string w = mangle(fq) + "__any_eq";
                vtables_ += "static bool " + w + "(void* a, void* b) { return grav_is_instance(b, &" +
                            mangle(pt) + "_typeinfo) && " + methodCName(impl, eq->name) + "((" +
                            structName(impl) + "*)a, (" + structName(pt) + "*)b); }\n";
                eqHook = w;
            }
        }
        // Releasing the reference fields when the object is destroyed.
        emitReleaseFields(fq);
        std::string visit = emitVisitFields(fq);
        // The interface tables (defined with the tables in emitInterfaceTables).
        size_t nitabs = classInterfaces(fq).size();
        std::string itabs = "0";
        if (nitabs) {
            itabs = mangle(fq) + "__itabs";
            vtables_ += "static const GravItab " + itabs + "[" + std::to_string(nitabs) + "];\n";
        }
        std::string memBytes = emitMemBytes(fq);
        std::string meta = emitReflection(fq);
        if (meta != "0") reflectedTypes += "&" + mangle(fq) + "_typeinfo, ";
        vtables_ += "static const GravTypeInfo " + mangle(fq) +
                    "_typeinfo = { \"" + fq + "\", " + base + ", " + extra + ", " +
                    std::to_string(nextra) + ", " + drop + ", " + strHook + ", " + eqHook + ", " +
                    mangle(fq) + "__arc_fields, " + visit + ", " + itabs + ", " + std::to_string(nitabs) + ", " +
                    meta + ", " + memBytes + " };\n";
    };
    for (const auto &declPtr : program_->decls) {
        if (auto *c = as<const ClassDecl>(declPtr.get())) emit(c->fqName);
    }
    std::vector<std::string> classes;
    for (const auto &declPtr : program_->decls)
        if (auto *c = as<const ClassDecl>(declPtr.get())) classes.push_back(c->fqName);
    emitCollectionItems(classes);
    // Every class with reflection, for ClassInfo.forName/all (null-terminated).
    vtables_ += "static const GravTypeInfo* const grav_rf_types_[] = { " + reflectedTypes + "0 };\n"
                "static const GravTypeInfo* const* grav_rf_all_types(void) { return grav_rf_types_; }\n";
    vtables_ += "\n";
}

namespace {
bool hasDecorators(const ClassDecl &c) {
    if (!c.decorators.empty() || !c.constructor.decorators.empty()) return true;
    for (const auto &f : c.fields) if (!f.decorators.empty()) return true;
    for (const auto &m : c.methods) if (!m.decorators.empty()) return true;
    for (const auto &s : c.staticFields) if (!s.decorators.empty()) return true;
    for (const auto &x : c.extraCtors) if (!x.decorators.empty()) return true;
    return false;
}
} // namespace

// A class has metadata when its hierarchy (everything under its root class) has
// a decorator anywhere, or one of its classes is named by reflect(...): ORM-style
// code reads a model's fields through its base class.
bool CodeGen::reflected(const std::string &fq) const {
    auto root = [&](std::string c) {
        for (int g = 0; g < 100; ++g) {
            const ClassInfo *ci = reg_->cls(c);
            if (!ci || ci->baseClass.empty()) break;
            c = ci->baseClass;
        }
        return c;
    };
    if (!reflectScanned_) {
        reflectScanned_ = true;
        for (const auto &d : program_->decls)
            if (auto *c = as<const ClassDecl>(d.get()); c && hasDecorators(*c)) reflectRoots_.insert(root(c->fqName));
        visitExprs(const_cast<Program &>(*program_), [&](Expr &e) {
            auto *c = as<CallExpr>(&e);
            if (!c) return;
            if (c->kind == CallKind::Builtin && c->targetName == "reflect") reflectRoots_.insert(root(c->ownerClass));
            if (c->kind == CallKind::StaticMethod && c->ownerClass == "std.core.ClassInfo" &&
                (c->methodName == "listItems" || c->methodName == "mapEntries"))
                rfItemsUsed_ = true;
        });
        // ...and the classes its fields hold (`Line`, `Line?`, `Array<Line>`,
        // `HashMap<string, Line>`), until none is added: a serializer walking an
        // object reaches them. (Standard-library classes are left out.)
        bool added = true;
        std::function<void(const TypeRef &)> hold = [&](const TypeRef &t) {
            if (t.kind == TypeRef::Kind::Nullable && t.elem) { hold(*t.elem); return; }
            if (t.kind != TypeRef::Kind::Named) return;
            TypeRef elem;
            if (rfCollection(t.name, &elem)) { hold(elem); return; }
            const ClassInfo *ci = reg_->cls(t.name);
            if (!ci || !ci->decl || t.name.rfind("std.", 0) == 0) return;
            if (reflectRoots_.insert(root(t.name)).second) added = true;
        };
        while (added) {
            added = false;
            for (const auto &d : program_->decls)
                if (auto *c = as<const ClassDecl>(d.get()); c && reflectRoots_.count(root(c->fqName)))
                    for (const auto &f : c->fields) hold(f.type);
        }
    }
    return reflectRoots_.count(root(fq)) != 0;
}

// Arrays and string-keyed maps: what their buffers hold (the instance's fields
// `data: E*`, or `ks: string*` and `vs: E*`).
int CodeGen::rfCollection(const std::string &fq, TypeRef *elem) const {
    bool arr = fq.rfind("std.collections.Array$", 0) == 0, map = fq.rfind("std.collections.HashMap$", 0) == 0;
    const ClassInfo *ci = arr || map ? reg_->cls(fq) : nullptr;
    if (!ci || !ci->decl) return 0;
    auto buffer = [&](const char *name) -> const TypeRef * {
        for (const auto &f : ci->decl->fields)
            if (f.name == name && f.type.kind == TypeRef::Kind::Pointer && f.type.elem) return f.type.elem.get();
        return nullptr;
    };
    if (arr) {
        const TypeRef *e = buffer("data");
        if (!e) return 0;
        *elem = *e;
        return 1;
    }
    const TypeRef *k = buffer("ks"), *v = buffer("vs");
    if (!k || !v || k->kind != TypeRef::Kind::String) return 0;
    *elem = *v;
    return 2;
}

std::string CodeGen::rfCollectionFrom(const std::string &fq) {
    if (auto it = rfFrom_.find(fq); it != rfFrom_.end()) return it->second;
    rfFrom_[fq] = "";   // (an Array<Array<…>> of itself cannot happen, but stay finite)
    TypeRef elem;
    int kind = rfCollection(fq, &elem);
    const std::string src = kind == 1 ? "std.collections.Array$any" : "std.collections.HashMap$string$any";
    if (!kind || fq == src || !reg_->cls(src)) return "";
    const std::string m = mangle(fq), st = structName(fq), ss = structName(src), ce = cTy(elem);
    std::string fail = kind == 1 ? "{ grav_release(a_); return 0; }"
                                 : "{ grav_any_release(v); grav_release(ks_); grav_release(a_); return 0; }";
    std::string conv = rfConvert(elem, "it_", fail, false);
    if (conv.empty()) return "";
    std::string fn = m + "__rf_from";
    vtables_ += "static int " + fn + "(GravAny v, " + st + "** out);\n";
    std::string body = "static int " + fn + "(GravAny v, " + st + "** out) {\n"
                       "    if (v.tag == GRAV_ANY_NULL) { *out = 0; return 1; }\n"
                       "    if (v.tag != GRAV_ANY_OBJ) return 0;\n"
                       "    if (grav_is_instance(v.v.p, &" + m + "_typeinfo)) { *out = (" + st + "*)grav_retain(v.v.p); return 1; }\n"
                       "    if (!grav_is_instance(v.v.p, &" + mangle(src) + "_typeinfo)) return 0;\n"
                       "    " + ss + "* src_ = (" + ss + "*)v.v.p;\n"
                       "    " + st + "* a_ = " + ctorCName(fq) + "();\n";
    if (kind == 1) {
        body += "    for (int i_ = 0; i_ < src_->" + memberCName("len") + "; i_++) {\n"
                "        GravAny v = src_->" + memberCName("data") + "[i_];\n"
                "        " + ce + " it_; memset(&it_, 0, sizeof it_);\n"
                "        " + conv + "\n"
                "        " + methodCName(fq, "push") + "(a_, it_);\n"
                "    }\n";
    } else {
        const std::string ks = structName("std.collections.Array$string");
        body += "    " + ks + "* ks_ = " + methodCName(src, "keys") + "(src_);\n"
                "    for (int i_ = 0; i_ < ks_->" + memberCName("len") + "; i_++) {\n"
                "        const char* k_ = ks_->" + memberCName("data") + "[i_];\n"
                "        GravAny v = " + methodCName(src, "get") + "(src_, k_);\n"
                "        " + ce + " it_; memset(&it_, 0, sizeof it_);\n"
                "        " + conv + "\n"
                "        " + methodCName(fq, "put") + "(a_, k_, it_);\n"
                "        grav_any_release(v);\n"
                "    }\n"
                "    grav_release(ks_);\n";
    }
    body += "    *out = a_;\n    return 1;\n}\n";
    rfDefs_ += body;
    rfFrom_[fq] = fn;
    return fn;
}

void CodeGen::emitCollectionItems(const std::vector<std::string> &classes) {
    const std::string anyList = "std.collections.Array$any", anyMap = "std.collections.HashMap$string$any";
    std::string table;
    // (only when the program asks: a reader per Array and map instance costs code)
    if (rfItemsUsed_ && reg_->cls(anyList) && reg_->cls(anyMap)) {
        const std::string al = structName(anyList), am = structName(anyMap), ks = structName("std.collections.Array$string");
        for (const auto &fq : classes) {
            TypeRef elem;
            int kind = rfCollection(fq, &elem);
            if (!kind || fq == anyList || fq == anyMap) continue;
            // Items an `any` can hold: numbers, bools, strings, objects, enums and their T?.
            const TypeRef &base = elem.isNullable() && elem.elem ? *elem.elem : elem;
            bool boxable = base.kind == TypeRef::Kind::Int || base.kind == TypeRef::Kind::Float || base.kind == TypeRef::Kind::Bool ||
                           base.kind == TypeRef::Kind::String || (!elem.isNullable() && base.kind == TypeRef::Kind::Any) ||
                           (base.isNamed() && (reg_->isEnum(base.name) ||
                                               (!elem.isNullable() && (reg_->isClass(base.name) || reg_->isInterface(base.name)))));
            if (!boxable) continue;
            const std::string st = structName(fq), fn = mangle(fq) + "__rf_items";
            std::string body = "static int " + fn + "(void* o, GravAny* out) {\n    " + st + "* c_ = (" + st + "*)o;\n";
            if (kind == 1) {
                body += "    " + al + "* r_ = " + ctorCName(anyList) + "();\n"
                        "    for (int i_ = 0; i_ < c_->" + memberCName("len") + "; i_++) " + methodCName(anyList, "push") + "(r_, " +
                        boxAnyC("c_->" + memberCName("data") + "[i_]", elem) + ");\n";
            } else {
                body += "    " + am + "* r_ = " + ctorCName(anyMap) + "();\n"
                        "    " + ks + "* ks_ = " + methodCName(fq, "keys") + "(c_);\n"
                        "    for (int i_ = 0; i_ < ks_->" + memberCName("len") + "; i_++) {\n"
                        "        const char* k_ = ks_->" + memberCName("data") + "[i_];\n"
                        "        " + cTy(elem) + " x_ = " + methodCName(fq, "get") + "(c_, k_);\n"
                        "        " + methodCName(anyMap, "put") + "(r_, k_, " + boxAnyC("x_", elem) + ");\n"
                        "        " + releaseOf(elem, "x_") + "\n"
                        "    }\n"
                        "    grav_release(ks_);\n";
            }
            body += "    *out = grav_any_obj(r_);\n    return " + std::to_string(kind) + ";\n}\n";
            rfDefs_ += body;
            vtables_ += "static int " + fn + "(void* o, GravAny* out);\n";
            table += "{ &" + mangle(fq) + "_typeinfo, " + fn + " }, ";
        }
    }
    vtables_ += "static const struct GravRfItems { const GravTypeInfo* t; int (*items)(void*, GravAny*); } grav_rf_items_[] = { " +
                table + "{ 0, 0 } };\n"
                "static int grav_rf_items(GravAny v, GravAny* out) {\n"
                "    if (v.tag != GRAV_ANY_OBJ || !v.v.p) return 0;\n"
                "    const GravTypeInfo* t = ((const struct GravObject*)v.v.p)->__type;\n"
                "    for (const struct GravRfItems* e = grav_rf_items_; e->t; e++) if (e->t == t) return e->items(v.v.p, out);\n"
                "    return 0;\n"
                "}\n";
}

std::string CodeGen::rfConvert(const TypeRef &k, const std::string &dst, const std::string &fail, bool own) {
    switch (k.kind) {
        case TypeRef::Kind::Int: {
            int bits = k.bits ? k.bits : 32;
            return "{ long long x_; if (!grav_rf_int(v, " + std::to_string(bits) + ", " + (k.isUnsigned ? "1" : "0") +
                   ", &x_)) " + fail + " " + dst + " = (" + cTy(k) + ")x_; }";
        }
        case TypeRef::Kind::Float:
            return "{ double d_; if (!grav_rf_float(v, &d_)) " + fail + " " + dst + " = (" + cTy(k) + ")d_; }";
        case TypeRef::Kind::Bool:
            return "{ int b_; if (!grav_rf_bool(v, &b_)) " + fail + " " + dst + " = b_; }";
        case TypeRef::Kind::String:
            if (own)
                return "if (v.tag != GRAV_ANY_STR) " + fail + " { const char* old_ = " + dst + "; " + dst +
                       " = grav_str_retain(v.v.s); grav_str_release(old_); }";
            return "if (v.tag != GRAV_ANY_STR) " + fail + " " + dst + " = v.v.s;";
        case TypeRef::Kind::Any:
            return own ? "" : dst + " = v;";
        case TypeRef::Kind::Nullable: {
            if (!k.elem) return "";
            const TypeRef &e = *k.elem;
            bool scalar = e.kind == TypeRef::Kind::Int || e.kind == TypeRef::Kind::Float || e.kind == TypeRef::Kind::Bool ||
                          (e.isNamed() && reg_->isEnum(e.name));
            if (e.kind == TypeRef::Kind::String)
                return "if (v.tag == GRAV_ANY_NULL) { " + (own ? "const char* old_ = " + dst + "; " + dst + " = 0; grav_str_release(old_);"
                                                               : dst + " = 0;") + " } else { " + rfConvert(e, dst, fail, own) + " }";
            if (!scalar) return "";
            return "if (v.tag == GRAV_ANY_NULL) " + dst + ".has = 0; else { " + rfConvert(e, dst + ".v", fail, own) + " " + dst +
                   ".has = 1; }";
        }
        default: break;
    }
    if (k.isNamed() && reg_->isEnum(k.name))
        return "if (!((v.tag == GRAV_ANY_ENUM && v.tname && strcmp(v.tname, \"" + k.name + "\") == 0) || v.tag == GRAV_ANY_INT)) " + fail +
               " " + dst + " = (" + cTy(k) + ")v.v.i;";
    // A list or map: the collection, or one made from an Array<any> / HashMap<string, any>
    // (kept until the caller's statement ends).
    if (!own && k.isNamed() && reg_->isClass(k.name)) {
        std::string from = rfCollectionFrom(k.name);
        if (!from.empty())
            return "{ " + cTy(k) + " c_; if (!" + from + "(v, &c_)) " + fail + " " + dst + " = (" + cTy(k) +
                   ")grav_autorelease((void*)c_); }";
    }
    if (!own && k.isNamed() && reg_->isClass(k.name))
        return "if (v.tag != GRAV_ANY_NULL && (v.tag != GRAV_ANY_OBJ || !grav_is_instance(v.v.p, &" + mangle(k.name) + "_typeinfo))) " +
               fail + " " + dst + " = v.tag == GRAV_ANY_OBJ ? (" + cTy(k) + ")v.v.p : 0;";
    // An interface: an object whose class implements it (its table found by the marker), or null.
    if (!own && k.isNamed() && reg_->isInterface(k.name)) {
        const std::string mk = "&" + mangle(k.name) + "__iface";
        return "{ const void* tab_ = v.tag == GRAV_ANY_OBJ ? grav_itab_find(v.v.p, " + mk + ") : 0; " +
               "if (v.tag != GRAV_ANY_NULL && !tab_) " + fail + " " + dst + ".obj = tab_ ? v.v.p : 0; " + dst + ".itab = tab_; }";
    }
    return "";
}

std::string CodeGen::emitReflection(const std::string &fq) {
    const ClassInfo *ci = reg_->cls(fq);
    if (!ci || !ci->decl || !reflected(fq)) return "0";
    const ClassDecl &cd = *ci->decl;
    const std::string m = mangle(fq), st = structName(fq);
    int counter = 0;
    // A decorator list as a static table: its name and "count, array" for the owner's entry.
    auto decos = [&](const std::vector<Decorator> &ds) -> std::string {
        if (ds.empty()) return "0, 0";
        std::string tbl = m + "__rd" + std::to_string(counter++);
        std::string entries;
        for (const auto &d : ds) {
            std::string args = "0";
            if (!d.args.empty()) {
                args = m + "__ra" + std::to_string(counter++);
                std::string vals;
                for (const auto &a : d.args) {
                    std::string v;
                    switch (a.kind) {
                        case DecoratorArg::Kind::String: case DecoratorArg::Kind::Name:
                            v = "{ GRAV_ANY_STR, 0, 0, { .s = \"" + escapeC(a.text) + "\" } }"; break;
                        case DecoratorArg::Kind::Int: v = "{ GRAV_ANY_INT, 0, 0, { .i = " + std::to_string(a.i) + "LL } }"; break;
                        case DecoratorArg::Kind::Float: {
                            char buf[64]; std::snprintf(buf, sizeof buf, "%.17g", a.f);
                            v = "{ GRAV_ANY_FLOAT, 0, 0, { .f = " + std::string(buf) + " } }"; break;
                        }
                        case DecoratorArg::Kind::Bool: v = std::string("{ GRAV_ANY_BOOL, 0, 0, { .b = ") + (a.b ? "1" : "0") + " } }"; break;
                    }
                    vals += (vals.empty() ? "" : ", ") + v;
                }
                vtables_ += "static const GravAny " + args + "[] = { " + vals + " };\n";
            }
            std::string keys = "0";
            bool anyKey = false;
            for (const auto &a : d.args) if (!a.key.empty()) anyKey = true;
            if (anyKey) {
                keys = m + "__rk" + std::to_string(counter++);
                std::string names;
                for (const auto &a : d.args) names += (names.empty() ? "" : ", ") + std::string("\"") + escapeC(a.key) + "\"";
                vtables_ += "static const char* const " + keys + "[] = { " + names + " };\n";
            }
            entries += (entries.empty() ? "" : ", ") + std::string("{ \"") + escapeC(d.name) + "\", " +
                       std::to_string(d.args.size()) + ", " + args + ", " + keys + " }";
        }
        vtables_ += "static const GravDecoMeta " + tbl + "[] = { " + entries + " };\n";
        return std::to_string(ds.size()) + ", " + tbl;
    };
    auto access = [](Access a) { return a == Access::Private ? 2 : a == Access::Protected ? 4 : 0; };
    // Fields: a reader boxing the value into an `any` (borrowed), and a writer
    // taking one when its value fits the field (1) or not (0).
    std::string fields;
    for (size_t i = 0; i < cd.fields.size(); ++i) {
        const FieldDecl &f = cd.fields[i];
        const TypeRef &t = f.type;
        std::string slot = "o->" + memberCName(f.name);
        std::string fn = m + "__rf" + std::to_string(i);
        bool cls = t.isNamed() && reg_->isClass(t.name);
        bool plain = t.kind == TypeRef::Kind::Int || t.kind == TypeRef::Kind::Float || t.kind == TypeRef::Kind::Bool ||
                     t.kind == TypeRef::Kind::String || t.kind == TypeRef::Kind::Any || t.isFunction() ||
                     (t.isNamed() && (reg_->isEnum(t.name) || reg_->isInterface(t.name))) || cls;
        bool nul = t.isNullable() && t.elem &&
                   (t.elem->kind == TypeRef::Kind::Int || t.elem->kind == TypeRef::Kind::Float ||
                    t.elem->kind == TypeRef::Kind::Bool || t.elem->kind == TypeRef::Kind::String ||
                    (t.elem->isNamed() && reg_->isEnum(t.elem->name)));
        std::string get = "0", set = "0";
        if (plain || nul) {
            std::string read = f.isWeak ? "((" + cTy(t) + ")grav_weak_load((void*)" + slot + "))" : slot;
            vtables_ += "static GravAny " + fn + "_get(void* o_) { " + st + "* o = (" + st + "*)o_; return " +
                        boxAnyC(read, t) + "; }\n";
            get = fn + "_get";
        }
        std::string body;
        if (t.kind == TypeRef::Kind::Any) {
            body = "{ GravAny old_ = " + slot + "; " + slot + " = grav_any_retain(v); grav_any_release(old_); }";
        } else if (cls && !f.isWeak && !rfCollectionFrom(t.name).empty()) {
            body = "{ " + cTy(t) + " new_; if (!" + rfCollectionFrom(t.name) + "(v, &new_)) return 0; { " + cTy(t) + " old_ = " +
                   slot + "; " + slot + " = new_; grav_release((void*)old_); } }";
        } else if (cls) {
            std::string ret = f.isWeak ? "grav_weak_retain" : "grav_retain", rel = f.isWeak ? "grav_weak_release" : "grav_release";
            body = "if (v.tag != GRAV_ANY_NULL && (v.tag != GRAV_ANY_OBJ || !grav_is_instance(v.v.p, &" + mangle(t.name) +
                   "_typeinfo))) return 0; { void* old_ = (void*)" + slot + "; void* new_ = v.tag == GRAV_ANY_OBJ ? v.v.p : 0; " +
                   ret + "(new_); " + slot + " = (" + cTy(t) + ")new_; " + rel + "(old_); }";
        } else if (!f.isWeak) {
            body = rfConvert(t, slot, "return 0;", true);
        }
        if (!body.empty()) {
            vtables_ += "static int " + fn + "_set(void* o_, GravAny v) { " + st + "* o = (" + st + "*)o_; " + body +
                        " return 1; }\n";
            set = fn + "_set";
        }
        bool nullable = t.isNullable() || f.writtenNullable;
        const TypeRef &base = t.isNullable() && t.elem ? *t.elem : t;
        bool isEnum = base.isNamed() && reg_->isEnum(base.name);
        bool isObject = base.isNamed() && (reg_->isClass(base.name) || reg_->isInterface(base.name));
        int flags = (f.isReadonly ? 1 : 0) | access(f.access) | (f.isWeak ? 8 : 0) | (nullable ? 32 : 0) |
                    (isEnum ? 64 : 0) | (isObject ? 128 : 0);
        std::string shown = typeRefName(t) + (nullable && !t.isNullable() ? "?" : "");   // (`DateTime?` as written)
        fields += (fields.empty() ? "" : ",\n    ") + std::string("{ \"") + escapeC(f.name) + "\", \"" +
                  escapeC(shown) + "\", " + std::to_string(flags) + ", " + decos(f.decorators) + ", " + get + ", " + set + " }";
    }
    if (!fields.empty()) vtables_ += "static const GravFieldMeta " + m + "__rfields[] = {\n    " + fields + " };\n";
    // A caller for reflection: checks the count, converts each argument (borrowed:
    // the caller's list holds them), makes the call `call(p0, p1, ...)` and boxes
    // its result (owned). "0" when a parameter or the result cannot go through an any.
    auto returnable = [&](const TypeRef &r) {
        if (r.isVoid() || r.isNullable()) return r.isVoid() || rfConvert(r, "x", "", false) != "";
        switch (r.kind) {
            case TypeRef::Kind::Int: case TypeRef::Kind::Float: case TypeRef::Kind::Bool: case TypeRef::Kind::String:
            case TypeRef::Kind::Any: case TypeRef::Kind::Function: return true;
            default: return r.isNamed() && (reg_->isClass(r.name) || reg_->isInterface(r.name) || reg_->isEnum(r.name));
        }
    };
    auto caller = [&](const std::string &fname, const std::string &head, const std::vector<TypeRef> &ps, const TypeRef &ret,
                      const std::function<std::string(const std::string &)> &call) -> std::string {
        if (!returnable(ret)) return "0";
        std::string body = "    if (n != " + std::to_string(ps.size()) + ") return 2;\n" + head, args;
        for (size_t i = 0; i < ps.size(); ++i) {
            std::string p = "p" + std::to_string(i);
            std::string conv = rfConvert(ps[i], p, "return " + std::to_string(3 + i) + ";", false);
            if (conv.empty()) return "0";
            body += "    " + cTy(ps[i]) + " " + p + "; memset(&" + p + ", 0, sizeof " + p + ");\n    { GravAny v = a[" +
                    std::to_string(i) + "]; " + conv + " }\n";
            args += (args.empty() ? "" : ", ") + p;
        }
        body += ret.isVoid() ? "    " + call(args) + ";\n    *out = grav_any_null();\n"
                             : "    *out = " + boxAnyC(call(args), ret) + ";\n";
        vtables_ += "static int " + fname + "(" + (head.empty() ? "" : "GravAny self_, ") + "const GravAny* a, int n, GravAny* out);\n";
        rfDefs_ += "static int " + fname + "(" + (head.empty() ? "" : "GravAny self_, ") + "const GravAny* a, int n, GravAny* out) {\n" +
                   "    (void)a;\n" + body + "    return 0;\n}\n";
        return fname;
    };
    auto typeList = [&](const std::vector<TypeRef> &ps) {
        std::string out;
        for (const auto &p : ps) out += (out.empty() ? "" : ", ") + typeRefName(p);
        return "\"" + escapeC(out) + "\"";
    };
    // Methods as written (not the copies the compiler makes: `find$User`, `map$int`).
    std::string methods;
    int nmethods = 0;
    for (const auto &md : cd.methods) {
        if (md.name.find('$') != std::string::npos) continue;
        std::string name = md.sourceName.empty() ? md.name : md.sourceName;
        int flags = access(md.access) | (md.isStatic ? 16 : 0);
        const MethodInfo *mi = nullptr;
        for (const auto &x : ci->methods) if (x.name == md.name) { mi = &x; break; }
        std::string fn = "0", params = "\"\"", result = "\"\"";
        size_t np = 0;
        if (mi) {
            np = mi->paramTypes.size();
            params = typeList(mi->paramTypes);
            if (!mi->returnType.isVoid()) result = typeList({mi->returnType});
            std::string fname = m + "__rc" + std::to_string(counter++);
            if (mi->isStatic) {
                std::string direct = methodCName(fq, md.name);
                fn = caller(fname, "    (void)self_;\n", mi->paramTypes, mi->returnType,
                            [&](const std::string &args) { return direct + "(" + args + ")"; });
            } else {
                std::string head = "    if (self_.tag != GRAV_ANY_OBJ || !grav_is_instance(self_.v.p, &" + mangle(fq) +
                                   "_typeinfo)) return 1;\n    " + st + "* o = (" + st + "*)self_.v.p;\n";
                TypeRef recv = TypeRef::named(fq);
                std::string mname = md.name;
                fn = caller(fname, head, mi->paramTypes, mi->returnType,
                            [&](const std::string &args) { return methodCall("o", recv, mname, args); });
            }
        }
        methods += (methods.empty() ? "" : ",\n    ") + std::string("{ \"") + escapeC(name) + "\", " +
                   std::to_string(flags) + ", " + decos(md.decorators) + ", " + std::to_string(np) + ", " + params + ", " + fn + ", " + result + " }";
        ++nmethods;
    }
    // Constructors (none for an abstract class): `new C(...)` by reflection.
    std::string ctors;
    int nctors = 0;
    if (!ci->isAbstract) {
        auto add = [&](const std::vector<TypeRef> &ps, const std::string &cname) {
            std::string fname = m + "__rk" + std::to_string(counter++);
            std::string fn = caller(fname, "", ps, TypeRef::named(fq),
                                    [&](const std::string &args) { return "(void*)" + cname + "(" + args + ")"; });
            ctors += (ctors.empty() ? "" : ",\n    ") + std::string("{ ") + std::to_string(ps.size()) + ", " + typeList(ps) + ", " + fn + " }";
            ++nctors;
        };
        if (ci->ctors.empty()) add({}, ctorCName(fq));
        bool hasNoArgs = ci->ctors.empty();
        for (size_t k = 0; k < ci->ctors.size(); ++k) {
            std::vector<TypeRef> ps;
            bool ok = true;
            for (const auto &p : ci->ctors[k]->params) { if (p.variadic) ok = false; ps.push_back(p.type); }
            if (ok) add(ps, ctorCName(fq, static_cast<int>(k)));
            if (ps.empty()) hasNoArgs = true;
        }
        // A constructor whose parameters all have literal defaults is also one
        // that takes no arguments (`constructor(who: string = "", n: int = 0)`):
        // reflection can make the object with create() alone.
        for (size_t k = 0; k < ci->ctors.size() && !hasNoArgs; ++k) {
            std::string vals;
            bool ok = !ci->ctors[k]->params.empty();
            for (const auto &p : ci->ctors[k]->params) {
                std::string v = p.variadic || !p.defaultValue ? "" : literalDefault(*p.defaultValue, p.type, [](const std::string &x) { return escapeC(x); });
                if (v.empty()) { ok = false; break; }
                vals += (vals.empty() ? "" : ", ") + v;
            }
            if (!ok) continue;
            std::string fname = m + "__rk" + std::to_string(counter++);
            std::string cname = ctorCName(fq, static_cast<int>(k));
            std::string fn = caller(fname, "", {}, TypeRef::named(fq),
                                    [&](const std::string &) { return "(void*)" + cname + "(" + vals + ")"; });
            ctors += (ctors.empty() ? "" : ",\n    ") + std::string("{ 0, \"\", ") + fn + " }";
            ++nctors;
            hasNoArgs = true;
        }
    }
    if (!ctors.empty()) vtables_ += "static const GravCtorMeta " + m + "__rctors[] = {\n    " + ctors + " };\n";
    if (!methods.empty()) vtables_ += "static const GravMethodMeta " + m + "__rmethods[] = {\n    " + methods + " };\n";
    std::string own = decos(cd.decorators);
    // (not const: it keeps the ClassInfo made for it)
    vtables_ += "static GravClassMeta " + m + "__rmeta = { " + std::to_string(cd.fields.size()) + ", " +
                (fields.empty() ? "0" : m + "__rfields") + ", " + std::to_string(nmethods) + ", " +
                (methods.empty() ? "0" : m + "__rmethods") + ", " + own + ", " + std::to_string(nctors) + ", " +
                (ctors.empty() ? "0" : m + "__rctors") + ", 0 };\n";
    return "&" + m + "__rmeta";
}

void CodeGen::emitInterfaceTables() {
    auto itabType = [](const std::string &i) { return mangle(i) + "_ITAB"; };

    // One method-table struct type per interface.
    for (const auto &declPtr : program_->decls) {
        auto *id = as<const InterfaceDecl>(declPtr.get());
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
        vtableTypes_ += "} " + itabType(id->fqName) + ";\n";
        // Its identity at run time (`x is I`, `x as I`): the marker's address.
        vtableTypes_ += "static const GRAV_UNUSED char " + mangle(id->fqName) + "__iface = 0;\n\n";
    }

    // One table instance per (class, interface-it-implements) pair.
    std::string thunks; // (defined after every table, which they may name)
    for (const auto &declPtr : program_->decls) {
        auto *c = as<const ClassDecl>(declPtr.get());
        if (!c) continue;
        std::vector<std::string> ifaces = classInterfaces(c->fqName);
        for (const auto &ifq : ifaces) {
            const InterfaceInfo *ii = reg_->iface(ifq);
            if (!ii) continue;
            vtables_ += "static const GRAV_UNUSED " + itabType(ifq) + " " + mangle(c->fqName) +
                        "__itab__" + mangle(ifq) + " = {\n";
            for (const auto &m : ii->methods) {
                // The class method implementing it (overloads: matched by signature).
                std::string mname = reg_->implementingMethod(c->fqName, m);
                if (mname.empty()) mname = m.name;
                std::string impl = reg_->findMethodImpl(c->fqName, mname);
                std::string sig = cTy(m.returnType) + " (*)(void*";
                for (const auto &pt : m.paramTypes) sig += ", " + cTy(pt);
                sig += ")";
                const MethodInfo *have = impl.empty() ? nullptr : reg_->findMethod(c->fqName, mname);
                if (impl.empty()) {
                    vtables_ += "    0,\n";
                } else if (have && m.returnType.isNamed() && reg_->isInterface(m.returnType.name) &&
                           have->returnType.isNamed() && reg_->isClass(have->returnType.name)) {
                    // It gives a class where the interface gives an interface: a thunk
                    // makes the interface value (the same object, with its table).
                    std::string thunk = mangle(c->fqName) + "__ithunk__" + mangle(ifq) + "__" + memberCName(m.name);
                    std::string params = "void* self", args = "(" + structName(impl) + "*)self";
                    for (size_t i = 0; i < m.paramTypes.size(); ++i) {
                        params += ", " + cTy(m.paramTypes[i]) + " a" + std::to_string(i);
                        args += ", a" + std::to_string(i);
                    }
                    std::string head = "static " + cTy(m.returnType) + " " + thunk + "(" + params + ")";
                    vtables_ = head + ";\n" + vtables_;
                    thunks += head + " {\n    return (struct GravIface){ (void*)" + methodCName(impl, mname) + "(" +
                              args + "), &" + mangle(have->returnType.name) + "__itab__" +
                              mangle(m.returnType.name) + " };\n}\n";
                    vtables_ += "    " + thunk + ",\n";
                } else {
                    vtables_ += "    (" + sig + ")" + methodCName(impl, mname) + ",\n";
                }
            }
            vtables_ += "};\n\n";
        }
        if (!ifaces.empty()) {
            vtables_ += "static const GravItab " + mangle(c->fqName) + "__itabs[" + std::to_string(ifaces.size()) + "] = {";
            for (size_t i = 0; i < ifaces.size(); ++i)
                vtables_ += std::string(i ? ", " : " ") + "{ &" + mangle(ifaces[i]) + "__iface, &" + mangle(c->fqName) +
                            "__itab__" + mangle(ifaces[i]) + " }";
            vtables_ += " };\n\n";
        }
    }
    vtables_ += thunks;
}

// Every interface `classFq` implements, through its whole chain of bases.
std::vector<std::string> CodeGen::classInterfaces(const std::string &classFq) const {
    std::vector<std::string> ifaces;
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = reg_->cls(cur);
        if (!ci) break;
        for (const auto &i : ci->interfaces)
            if (!i.empty() && reg_->iface(i) && std::find(ifaces.begin(), ifaces.end(), i) == ifaces.end())
                ifaces.push_back(i);
        cur = ci->baseClass;
    }
    return ifaces;
}

void CodeGen::emitVTableInstance(const ClassInfo &ci) {
    if (ci.slots.empty()) return;
    // Read-only tables let the C compiler fold loads and devirtualize calls.
    vtables_ += "static const " + vtableType(ci.fqName) + " " +
                vtableInstance(ci.fqName) + " = {\n";
    for (const auto &slot : ci.slots) {
        std::string impl = reg_->findMethodImpl(ci.fqName, slot.name);
        // A slot no subclass overrides is never read (every call to it is direct),
        // so it holds nothing: the method stays unreferenced unless something calls
        // it, and the C compiler drops it.
        if (impl.empty() || !reg_->isOverridden(slot.slotOwner, slot.name)) {
            vtables_ += "    0,\n";
        } else {
            vtables_ += "    (" + slotFnType(slot) + ")" +
                        methodCName(impl, slot.name) + ",\n";
        }
    }
    vtables_ += "};\n\n";
}

} // namespace grav
