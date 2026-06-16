#include "sema/symbols.h"

#include <algorithm>
#include <functional>

namespace grav {

namespace {
std::vector<std::string> splitDots(const std::string &s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            if (i > start) out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}
std::string joinDots(const std::vector<std::string> &parts, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        if (i) out += '.';
        out += parts[i];
    }
    return out;
}
// FQ name -> C identifier (dots become double underscores), matching codegen.
std::string cmangle(const std::string &fq) {
    std::string out;
    for (char c : fq) { if (c == '.') out += "__"; else out += c; }
    return out;
}
} // namespace

void Registry::error(int line, int col, const std::string &msg) {
    errors_.emplace_back("sema", line, col, msg);
}

std::string Registry::namespaceOf(const std::string &fqName) {
    auto pos = fqName.find_last_of('.');
    return pos == std::string::npos ? "" : fqName.substr(0, pos);
}

const ClassInfo *Registry::cls(const std::string &fq) const {
    auto it = classes_.find(fq);
    return it == classes_.end() ? nullptr : &it->second;
}
const InterfaceInfo *Registry::iface(const std::string &fq) const {
    auto it = interfaces_.find(fq);
    return it == interfaces_.end() ? nullptr : &it->second;
}
const StructInfo *Registry::strct(const std::string &fq) const {
    auto it = structs_.find(fq);
    return it == structs_.end() ? nullptr : &it->second;
}
const EnumInfo *Registry::en(const std::string &fq) const {
    auto it = enums_.find(fq);
    return it == enums_.end() ? nullptr : &it->second;
}
bool Registry::hasEnumMember(const std::string &enumFq,
                             const std::string &member) const {
    const EnumInfo *ei = en(enumFq);
    if (!ei) return false;
    for (const auto &m : ei->members)
        if (m == member) return true;
    return false;
}
const FunctionInfo *Registry::func(const std::string &fq) const {
    auto it = functions_.find(fq);
    return it == functions_.end() ? nullptr : &it->second;
}
const GlobalInfo *Registry::global(const std::string &fq) const {
    auto it = globals_.find(fq);
    return it == globals_.end() ? nullptr : &it->second;
}

std::string Registry::resolveGlobal(const std::string &name,
                                    const std::string &nsContext) const {
    auto parts = splitDots(nsContext);
    for (size_t i = parts.size() + 1; i-- > 0;) {
        std::string prefix = joinDots(parts, i);
        std::string cand = prefix.empty() ? name : prefix + "." + name;
        if (globals_.count(cand)) return cand;
    }
    return "";
}

const GlobalInfo *Registry::findStaticField(const std::string &classFq,
                                            const std::string &name) const {
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        auto it = globals_.find(cur + "." + name);
        if (it != globals_.end() && !it->second.ownerClass.empty()) return &it->second;
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        cur = ci->baseClass;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Name resolution
// ---------------------------------------------------------------------------

std::string Registry::resolveType(const std::string &name,
                                  const std::string &nsContext) const {
    auto parts = splitDots(nsContext);
    for (size_t i = parts.size() + 1; i-- > 0;) {
        std::string prefix = joinDots(parts, i);
        std::string cand = prefix.empty() ? name : prefix + "." + name;
        if (isType(cand)) return cand;
    }
    return "";
}

std::string Registry::resolveFunc(const std::string &name,
                                  const std::string &nsContext) const {
    auto parts = splitDots(nsContext);
    for (size_t i = parts.size() + 1; i-- > 0;) {
        std::string prefix = joinDots(parts, i);
        std::string cand = prefix.empty() ? name : prefix + "." + name;
        if (functions_.count(cand)) return cand;
    }
    return "";
}

std::string Registry::resolveNamespace(const std::string &name,
                                       const std::string &nsContext) const {
    auto parts = splitDots(nsContext);
    for (size_t i = parts.size() + 1; i-- > 0;) {
        std::string prefix = joinDots(parts, i);
        std::string cand = prefix.empty() ? name : prefix + "." + name;
        if (std::find(namespaces_.begin(), namespaces_.end(), cand) !=
            namespaces_.end()) {
            return cand;
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Lookups across the inheritance chain
// ---------------------------------------------------------------------------

const FieldInfo *Registry::findStructField(const std::string &structFq,
                                           const std::string &name) const {
    const StructInfo *si = strct(structFq);
    if (!si) return nullptr;
    for (const auto &f : si->fields)
        if (f.name == name) return &f;
    return nullptr;
}

const FieldInfo *Registry::findField(const std::string &classFq,
                                     const std::string &name) const {
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        for (const auto &f : ci->fields)
            if (f.name == name) return &f;
        cur = ci->baseClass;
    }
    return nullptr;
}

const MethodInfo *Registry::findMethod(const std::string &classFq,
                                       const std::string &name) const {
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        for (const auto &m : ci->methods)
            if (m.name == name) return &m;
        cur = ci->baseClass;
    }
    return nullptr;
}

const MethodInfo *Registry::findInterfaceMethod(const std::string &ifaceFq,
                                                const std::string &name) const {
    const InterfaceInfo *ii = iface(ifaceFq);
    if (!ii) return nullptr;
    for (const auto &m : ii->methods)
        if (m.name == name) return &m;
    return nullptr;
}

std::string Registry::findMethodImpl(const std::string &classFq,
                                     const std::string &name) const {
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        for (const auto &m : ci->methods)
            if (m.name == name && m.hasBody) return cur;
        cur = ci->baseClass;
    }
    return "";
}

bool Registry::isSubclass(const std::string &sub, const std::string &base) const {
    std::string cur = sub;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        if (cur == base) return true;
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        cur = ci->baseClass;
    }
    return false;
}

bool Registry::classImplements(const std::string &classFq,
                               const std::string &ifaceFq) const {
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        for (const auto &i : ci->interfaces)
            if (i == ifaceFq) return true;
        cur = ci->baseClass;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Build pipeline
// ---------------------------------------------------------------------------

const std::vector<GravError> &Registry::build(Program &program) {
    registerDecls(program);
    canonicalizeAliases();
    canonicalize();
    synthesizeAccessors();
    computeSlots();
    checkHierarchies();
    checkStructCycles();
    return errors_;
}

// Auto-generate get_<field>/set_<field> methods for each private field, unless
// the class already declares a method with that name. These become regular
// (virtual) methods callable from outside the class.
void Registry::synthesizeAccessors() {
    for (auto &[fq, ci] : classes_) {
        auto hasMethod = [&](const std::string &n) {
            for (const auto &m : ci.methods)
                if (m.name == n) return true;
            return false;
        };
        std::vector<MethodInfo> added;
        for (const auto &f : ci.fields) {
            if (f.access != Access::Private) continue;

            std::string getter = "get_" + f.name;
            if (!hasMethod(getter)) {
                MethodInfo g;
                g.name = getter;
                g.access = Access::Public;
                g.returnType = f.type;
                g.definingClass = fq;
                g.hasBody = true;
                g.accessor = AccessorKind::Getter;
                g.accessorField = f.name;
                added.push_back(std::move(g));
            }

            std::string setter = "set_" + f.name;
            if (!f.isReadonly && !hasMethod(setter)) {
                MethodInfo s;
                s.name = setter;
                s.access = Access::Public;
                s.returnType = TypeRef::prim(TypeRef::Kind::Void);
                s.paramTypes.push_back(f.type);
                s.paramNames.push_back("value");
                s.definingClass = fq;
                s.hasBody = true;
                s.accessor = AccessorKind::Setter;
                s.accessorField = f.name;
                added.push_back(std::move(s));
            }
        }
        for (auto &m : added) ci.methods.push_back(std::move(m));
    }
}

void Registry::registerDecls(Program &program) {
    auto noteNamespace = [&](const std::string &fq) {
        auto parts = splitDots(namespaceOf(fq));
        for (size_t i = 1; i <= parts.size(); ++i) {
            std::string ns = joinDots(parts, i);
            if (std::find(namespaces_.begin(), namespaces_.end(), ns) ==
                namespaces_.end())
                namespaces_.push_back(ns);
        }
    };

    for (auto &declPtr : program.decls) {
        Decl *d = declPtr.get();
        noteNamespace(d->fqName);

        if (auto *c = dynamic_cast<ClassDecl *>(d)) {
            if (isType(c->fqName) || isAlias(c->fqName)) { error(c->line, c->col, "duplicate type '" + c->fqName + "'"); continue; }
            ClassInfo info;
            info.fqName = c->fqName;
            info.isAbstract = c->isAbstract;
            info.baseClass = c->baseName;        // raw, canonicalized later
            info.interfaces = c->interfaceNames; // raw
            info.decl = c;
            info.hasConstructor = c->constructor.present;
            info.constructor = c->constructor.present ? &c->constructor : nullptr;
            for (auto &f : c->fields) {
                FieldInfo fi{f.access, f.isReadonly, f.name, f.type, c->fqName};
                info.fields.push_back(std::move(fi));
            }
            for (auto &m : c->methods) {
                MethodInfo mi;
                mi.name = m.name;
                mi.access = m.access;
                mi.isStatic = m.isStatic;
                mi.isAbstract = m.isAbstract;
                mi.returnType = m.returnType;
                mi.definingClass = c->fqName;
                mi.decl = &m;
                mi.hasBody = m.hasBody;
                for (auto &p : m.params) {
                    mi.paramTypes.push_back(p.type);
                    mi.paramNames.push_back(p.name);
                }
                info.methods.push_back(std::move(mi));
            }
            classes_[c->fqName] = std::move(info);
            // Static fields lower to C globals named Class.field.
            for (auto &sf : c->staticFields) {
                std::string fq = c->fqName + "." + sf.name;
                if (globals_.count(fq)) { error(sf.line, sf.col, "duplicate static field '" + fq + "'"); continue; }
                GlobalInfo gi;
                gi.fqName = fq;
                gi.type = sf.type; // canonicalized later
                gi.isConst = sf.isConst;
                gi.access = sf.access;
                gi.ownerClass = c->fqName;
                gi.cName = cmangle(c->fqName) + "_s_" + sf.name;
                gi.line = sf.line; gi.col = sf.col;
                globals_[fq] = gi;
            }
        } else if (auto *st = dynamic_cast<StructDecl *>(d)) {
            if (isType(st->fqName) || isAlias(st->fqName)) { error(st->line, st->col, "duplicate type '" + st->fqName + "'"); continue; }
            StructInfo info;
            info.fqName = st->fqName;
            info.decl = st;
            for (auto &f : st->fields) {
                FieldInfo fi{f.access, f.isReadonly, f.name, f.type, st->fqName};
                info.fields.push_back(std::move(fi));
            }
            structs_[st->fqName] = std::move(info);
        } else if (auto *ifc = dynamic_cast<InterfaceDecl *>(d)) {
            if (isType(ifc->fqName) || isAlias(ifc->fqName)) { error(ifc->line, ifc->col, "duplicate type '" + ifc->fqName + "'"); continue; }
            InterfaceInfo info;
            info.fqName = ifc->fqName;
            info.decl = ifc;
            for (auto &m : ifc->methods) {
                MethodInfo mi;
                mi.name = m.name;
                mi.returnType = m.returnType;
                mi.definingClass = ifc->fqName;
                mi.hasBody = false;
                for (auto &p : m.params) {
                    mi.paramTypes.push_back(p.type);
                    mi.paramNames.push_back(p.name);
                }
                info.methods.push_back(std::move(mi));
            }
            interfaces_[ifc->fqName] = std::move(info);
        } else if (auto *en = dynamic_cast<EnumDecl *>(d)) {
            if (isType(en->fqName) || isAlias(en->fqName)) { error(en->line, en->col, "duplicate type '" + en->fqName + "'"); continue; }
            EnumInfo info;
            info.fqName = en->fqName;
            info.decl = en;
            for (auto &m : en->members) {
                if (std::find(info.members.begin(), info.members.end(), m.name) !=
                    info.members.end())
                    error(m.line, m.col, "duplicate enum member '" + m.name +
                                             "' in '" + en->fqName + "'");
                else
                    info.members.push_back(m.name);
            }
            enums_[en->fqName] = std::move(info);
        } else if (auto *ta = dynamic_cast<TypeAliasDecl *>(d)) {
            if (isType(ta->fqName) || isAlias(ta->fqName)) { error(ta->line, ta->col, "duplicate type '" + ta->fqName + "'"); continue; }
            AliasInfo info;
            info.fqName = ta->fqName;
            info.target = ta->target; // raw; canonicalized in canonicalizeAliases()
            info.decl = ta;
            aliases_[ta->fqName] = std::move(info);
        } else if (auto *fn = dynamic_cast<FunctionDecl *>(d)) {
            if (functions_.count(fn->fqName)) { error(fn->line, fn->col, "duplicate function '" + fn->fqName + "'"); continue; }
            FunctionInfo info;
            info.fqName = fn->fqName;
            info.isAsync = fn->isAsync;
            info.returnType = fn->returnType;
            info.decl = fn;
            for (auto &p : fn->params) {
                info.paramTypes.push_back(p.type);
                info.paramNames.push_back(p.name);
                if (p.variadic) info.isVariadic = true;
            }
            functions_[fn->fqName] = std::move(info);
        } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d)) {
            if (globals_.count(g->fqName)) { error(g->line, g->col, "duplicate global '" + g->fqName + "'"); continue; }
            if (!g->hasDeclaredType) {
                error(g->line, g->col, "a top-level '" + std::string(g->isConst ? "const" : "let") +
                                           "' needs an explicit type, e.g. `" +
                                           (g->isConst ? "const " : "let ") + g->name + ": int = ...`");
                continue;
            }
            GlobalInfo gi;
            gi.fqName = g->fqName;
            gi.type = g->declaredType; // canonicalized later
            gi.isConst = g->isConst;
            gi.cName = "gv_" + cmangle(g->fqName);
            gi.line = g->line; gi.col = g->col;
            globals_[g->fqName] = gi;
        }
    }
}

std::string Registry::resolveTypeOrAlias(const std::string &name,
                                         const std::string &nsContext) const {
    auto parts = splitDots(nsContext);
    for (size_t i = parts.size() + 1; i-- > 0;) {
        std::string prefix = joinDots(parts, i);
        std::string cand = prefix.empty() ? name : prefix + "." + name;
        if (isType(cand) || isAlias(cand)) return cand;
    }
    return "";
}

TypeRef Registry::resolveCanonical(const TypeRef &t, const std::string &nsContext,
                                   bool &ok) const {
    if (t.isPointer())
        return TypeRef::pointer(resolveCanonical(*t.elem, nsContext, ok));
    if (t.isArray())
        return TypeRef::array(resolveCanonical(*t.elem, nsContext, ok), t.arrayLen);
    if (t.isFuture())
        return TypeRef::future(resolveCanonical(*t.elem, nsContext, ok));
    if (!t.isNamed()) return t;
    std::string fq = resolveTypeOrAlias(t.name, nsContext);
    if (fq.empty()) {
        ok = false;
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    auto it = aliases_.find(fq);
    if (it != aliases_.end()) return it->second.target; // already canonical
    return TypeRef::named(fq);
}

// Expands one alias to its fully-canonical target, resolving nested aliases and
// guarding against cycles via the per-alias `state` flag.
TypeRef Registry::resolveAlias(const std::string &fq) {
    AliasInfo &ai = aliases_[fq];
    if (ai.state == 2) return ai.target;
    int line = ai.decl ? ai.decl->line : 0, col = ai.decl ? ai.decl->col : 0;
    if (ai.state == 1) {
        error(line, col, "type alias '" + fq + "' is cyclic");
        ai.target = TypeRef::prim(TypeRef::Kind::Error);
        ai.state = 2;
        return ai.target;
    }
    ai.state = 1;
    std::string ns = namespaceOf(fq);
    TypeRef raw = ai.target; // currently the raw, as-written target
    std::function<TypeRef(const TypeRef &)> expand = [&](const TypeRef &t) -> TypeRef {
        if (t.isPointer()) return TypeRef::pointer(expand(*t.elem));
        if (t.isArray()) return TypeRef::array(expand(*t.elem), t.arrayLen);
        if (t.isFuture()) return TypeRef::future(expand(*t.elem));
        if (!t.isNamed()) return t;
        std::string r = resolveTypeOrAlias(t.name, ns);
        if (r.empty()) {
            error(line, col, "unknown type '" + t.name + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        if (isAlias(r)) return resolveAlias(r);
        return TypeRef::named(r);
    };
    ai.target = expand(raw);
    ai.state = 2;
    return ai.target;
}

void Registry::canonicalizeAliases() {
    for (auto &[fq, ai] : aliases_) {
        (void)ai;
        resolveAlias(fq);
    }
}

TypeRef Registry::canonType(const TypeRef &t, const std::string &nsContext,
                            int line, int col) {
    bool ok = true;
    TypeRef r = resolveCanonical(t, nsContext, ok);
    if (!ok) error(line, col, "unknown type '" + typeRefName(t) + "'");
    return r;
}

// Rewrites every Named type reference (in the AST decl nodes and the mirrored
// registry info) to its fully-qualified form, and resolves base/interface names.
void Registry::canonicalize() {
    for (auto &[fq, ci] : classes_) {
        std::string ns = namespaceOf(fq);
        ClassDecl *cd = ci.decl;
        int line = cd->line, col = cd->col;

        if (!ci.baseClass.empty()) {
            std::string b = resolveType(ci.baseClass, ns);
            if (b.empty() || !isClass(b)) {
                error(line, col, "unknown base class '" + ci.baseClass + "'");
                ci.baseClass.clear();
            } else {
                ci.baseClass = b;
            }
        }
        for (auto &in : ci.interfaces) {
            std::string r = resolveType(in, ns);
            if (r.empty() || !isInterface(r)) {
                error(line, col, "unknown interface '" + in + "'");
                in.clear();
            } else {
                in = r;
            }
        }
        for (size_t i = 0; i < cd->fields.size(); ++i) {
            cd->fields[i].type = canonType(cd->fields[i].type, ns,
                                           cd->fields[i].line, cd->fields[i].col);
            ci.fields[i].type = cd->fields[i].type;
        }
        for (auto &p : cd->constructor.params) p.type = canonType(p.type, ns, line, col);
        for (size_t i = 0; i < cd->methods.size(); ++i) {
            MethodDecl &m = cd->methods[i];
            m.returnType = canonType(m.returnType, ns, m.line, m.col);
            ci.methods[i].returnType = m.returnType;
            for (size_t j = 0; j < m.params.size(); ++j) {
                m.params[j].type = canonType(m.params[j].type, ns, m.line, m.col);
                ci.methods[i].paramTypes[j] = m.params[j].type;
            }
        }
    }
    for (auto &[fq, si] : structs_) {
        std::string ns = namespaceOf(fq);
        StructDecl *sd = si.decl;
        for (size_t i = 0; i < sd->fields.size(); ++i) {
            sd->fields[i].type = canonType(sd->fields[i].type, ns,
                                           sd->fields[i].line, sd->fields[i].col);
            si.fields[i].type = sd->fields[i].type;
        }
    }
    for (auto &[fq, ii] : interfaces_) {
        std::string ns = namespaceOf(fq);
        InterfaceDecl *id = ii.decl;
        for (size_t i = 0; i < id->methods.size(); ++i) {
            MethodDecl &m = id->methods[i];
            m.returnType = canonType(m.returnType, ns, m.line, m.col);
            ii.methods[i].returnType = m.returnType;
            for (size_t j = 0; j < m.params.size(); ++j) {
                m.params[j].type = canonType(m.params[j].type, ns, m.line, m.col);
                ii.methods[i].paramTypes[j] = m.params[j].type;
            }
        }
    }
    for (auto &[fq, fi] : functions_) {
        std::string ns = namespaceOf(fq);
        FunctionDecl *fd = fi.decl;
        fd->returnType = canonType(fd->returnType, ns, fd->line, fd->col);
        fi.returnType = fd->returnType;
        for (size_t j = 0; j < fd->params.size(); ++j) {
            fd->params[j].type = canonType(fd->params[j].type, ns, fd->line, fd->col);
            fi.paramTypes[j] = fd->params[j].type;
        }
    }
    // Globals and static fields: canonicalize their declared types (the namespace
    // context is the global's own, or its owning class's).
    for (auto &[fq, gi] : globals_) {
        std::string ns = namespaceOf(gi.ownerClass.empty() ? fq : gi.ownerClass);
        gi.type = canonType(gi.type, ns, gi.line, gi.col);
    }
}

void Registry::computeSlots() {
    // Resolve each class's slots, computing bases first via recursion + memo.
    std::unordered_map<std::string, bool> done;
    std::function<void(const std::string &)> compute = [&](const std::string &fq) {
        if (done[fq]) return;
        done[fq] = true;
        ClassInfo *ci = &classes_[fq];
        std::vector<VTableSlot> slots;
        if (!ci->baseClass.empty() && isClass(ci->baseClass)) {
            compute(ci->baseClass);
            slots = classes_[ci->baseClass].slots; // inherit positions
        }
        for (auto &m : ci->methods) {
            if (m.isStatic) continue;
            auto it = std::find_if(slots.begin(), slots.end(),
                                   [&](const VTableSlot &s) { return s.name == m.name; });
            if (it == slots.end()) {
                slots.push_back(VTableSlot{m.name, fq});
            }
            // override: keep existing slot/position, impl resolved at codegen
        }
        ci->slots = std::move(slots);
    };
    for (auto &[fq, ci] : classes_) compute(fq);
}

// A struct stores its struct-typed fields by value, so a struct that contains
// itself (directly or through other structs) would have infinite size. Class
// fields are pointers and never participate in such a cycle.
void Registry::checkStructCycles() {
    enum Mark { Unseen, OnStack, Done };
    for (auto &[fq, si] : structs_) {
        // Walk the value-containment graph from each struct; a back-edge to a
        // struct still on the stack is a cycle. Re-walking per struct lets us
        // pin the diagnostic to the offending declaration.
        std::unordered_map<std::string, int> mark;
        std::function<bool(const std::string &)> walk = [&](const std::string &cur) {
            int &m = mark[cur];
            if (m == Done) return false;
            if (m == OnStack) return true;
            m = OnStack;
            bool cyclic = false;
            if (const StructInfo *s = strct(cur))
                for (const auto &f : s->fields)
                    if (f.type.isNamed() && isStruct(f.type.name) && walk(f.type.name)) {
                        cyclic = true;
                        break;
                    }
            m = Done;
            return cyclic;
        };
        if (walk(fq))
            error(si.decl->line, si.decl->col,
                  "struct '" + fq + "' contains itself by value (infinite size); "
                  "use a class for recursive types");
    }
}

void Registry::checkHierarchies() {
    for (auto &[fq, ci] : classes_) {
        int line = ci.decl->line, col = ci.decl->col;

        // Abstract methods may only appear in abstract classes.
        for (auto &m : ci.methods) {
            if (m.isAbstract && !ci.isAbstract) {
                error(m.decl ? m.decl->line : line, m.decl ? m.decl->col : col,
                      "abstract method '" + m.name +
                          "' is only allowed in an abstract class");
            }
        }

        // A concrete class must implement every inherited abstract method.
        if (!ci.isAbstract) {
            std::vector<std::string> abstractNames;
            std::string cur = fq;
            int guard = 0;
            while (!cur.empty() && guard++ < 100) {
                const ClassInfo *c = cls(cur);
                if (!c) break;
                for (const auto &m : c->methods)
                    if (m.isAbstract) abstractNames.push_back(m.name);
                cur = c->baseClass;
            }
            for (const auto &n : abstractNames) {
                if (findMethodImpl(fq, n).empty())
                    error(line, col, "class '" + fq +
                                         "' must implement abstract method '" + n + "'");
            }
        }

        // Override signatures must match the base method.
        for (auto &m : ci.methods) {
            if (ci.baseClass.empty()) continue;
            const MethodInfo *base = findMethod(ci.baseClass, m.name);
            if (!base) continue;
            bool ok = base->returnType == m.returnType &&
                      base->paramTypes.size() == m.paramTypes.size();
            for (size_t i = 0; ok && i < m.paramTypes.size(); ++i)
                ok = base->paramTypes[i] == m.paramTypes[i];
            if (!ok) {
                error(m.decl->line, m.decl->col,
                      "override of '" + m.name + "' does not match the signature in '" +
                          base->definingClass + "'");
            }
        }
        // Interface conformance.
        for (const auto &ifq : ci.interfaces) {
            if (ifq.empty()) continue;
            const InterfaceInfo *ii = iface(ifq);
            if (!ii) continue;
            for (const auto &want : ii->methods) {
                const MethodInfo *have = findMethod(fq, want.name);
                if (!have || have->isStatic) {
                    error(line, col, "class '" + fq + "' does not implement method '" +
                                         want.name + "' required by interface '" + ifq + "'");
                    continue;
                }
                bool ok = have->returnType == want.returnType &&
                          have->paramTypes.size() == want.paramTypes.size();
                for (size_t i = 0; ok && i < want.paramTypes.size(); ++i)
                    ok = have->paramTypes[i] == want.paramTypes[i];
                if (!ok) {
                    error(line, col, "method '" + want.name + "' in '" + fq +
                                         "' does not match interface '" + ifq + "'");
                }
            }
        }
    }
}

} // namespace grav
