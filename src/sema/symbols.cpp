#include "sema/symbols.h"

#include <cctype>

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
const std::vector<FunctionInfo> &Registry::funcOverloads(const std::string &fq) const {
    static const std::vector<FunctionInfo> empty;
    auto it = funcOverloads_.find(fq);
    return it == funcOverloads_.end() ? empty : it->second;
}
const GlobalInfo *Registry::global(const std::string &fq) const {
    auto it = globals_.find(fq);
    return it == globals_.end() ? nullptr : &it->second;
}

std::string Registry::resolveGlobal(const std::string &name,
                                    const std::string &nsContext) const {
    for (const auto &cand : nameCandidates(name, nsContext, uses_))
        if (globals_.count(cand)) return cand;
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
    for (const auto &cand : nameCandidates(name, nsContext, uses_))
        if (isType(cand)) return cand;
    // `Shape.Circle` names the data enum variant class `Shape$Circle`.
    if (auto dot = name.rfind('.'); dot != std::string::npos)
        return resolveType(name.substr(0, dot) + "$" + name.substr(dot + 1), nsContext);
    return "";
}

std::string Registry::resolveFunc(const std::string &name,
                                  const std::string &nsContext) const {
    for (const auto &cand : nameCandidates(name, nsContext, uses_))
        if (functions_.count(cand)) return cand;
    return "";
}

std::string Registry::resolveNamespace(const std::string &name,
                                       const std::string &nsContext) const {
    for (const auto &cand : nameCandidates(name, nsContext, uses_)) {
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

bool Registry::interfaceDeclares(const std::string &sourceName) const {
    for (const auto &[fq, ii] : interfaces_)
        for (const auto &m : ii.methods)
            if (m.sourceName == sourceName || m.name == sourceName) return true;
    return false;
}

bool Registry::isOverridden(const std::string &classFq, const std::string &name) const {
    std::string key = classFq + ':' + name;
    auto it = overrideCache_.find(key);
    if (it != overrideCache_.end()) return it->second;
    std::string mine = findMethodImpl(classFq, name);
    bool over = mine.empty(); // abstract here: must dispatch to whatever subclass
    if (!over) {
        const MethodInfo *mi = findMethod(classFq, name);
        if (!(mi && mi->isFinal)) {
            for (const auto &[fq, ci] : classes_) {
                if (fq == classFq || !isSubclass(fq, classFq)) continue;
                if (findMethodImpl(fq, name) != mine) { over = true; break; }
            }
        }
    }
    overrideCache_[key] = over;
    return over;
}

std::string Registry::findDestructor(const std::string &classFq) const {
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        if (ci->destructor) return cur;
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
    uses_ = program.uses;
    registerDecls(program);
    canonicalizeAliases();
    canonicalize();
    inheritConstructors();
    assignMethodNames();
    synthesizeAccessors();
    synthesizeDelegates();
    computeSlots();
    checkHierarchies();
    checkStructCycles();
    // Checks walk hash maps: report in source order.
    std::stable_sort(errors_.begin(), errors_.end(), [](const GravError &a, const GravError &b) {
        return a.line() != b.line() ? a.line() < b.line() : a.col() < b.col();
    });
    return errors_;
}

namespace {
// A C-identifier-safe spelling of a parameter list, e.g. (int, string) -> "int_string".
std::string signatureSuffix(const std::vector<TypeRef> &params) {
    if (params.empty()) return "void";
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) out += '_';
        for (char c : typeRefName(params[i]))
            out += std::isalnum(static_cast<unsigned char>(c)) ? c : (c == '*' ? 'P' : '_');
    }
    return out;
}
} // namespace

// Method overloading. Each method gets an internal name that is unique per
// signature within its hierarchy: an override reuses the base method's name (so
// it lands in the same vtable slot); the first method of a given name keeps the
// plain name; any other signature of that name becomes `name__<signature>`.
void Registry::assignMethodNames() {
    std::unordered_map<std::string, bool> done;
    std::function<void(const std::string &)> assign = [&](const std::string &fq) {
        if (done[fq]) return;
        done[fq] = true;
        ClassInfo &ci = classes_[fq];
        if (!ci.baseClass.empty() && isClass(ci.baseClass)) assign(ci.baseClass);
        std::vector<MethodInfo *> seen; // own methods already named
        for (auto &m : ci.methods) {
            m.sourceName = m.name;
            std::string internal;
            bool nameTaken = false;
            // Inherited method with the same source name: same signature = override.
            for (const MethodInfo *b : findMethodsBySource(ci.baseClass, m.sourceName)) {
                nameTaken = true;
                if (b->paramTypes == m.paramTypes && b->isStatic == m.isStatic) {
                    internal = b->name;
                    break;
                }
            }
            for (MethodInfo *o : seen) {
                if (o->sourceName != m.sourceName) continue;
                nameTaken = true;
                if (o->paramTypes == m.paramTypes) {
                    int line = m.decl ? m.decl->line : ci.decl->line;
                    int col = m.decl ? m.decl->col : ci.decl->col;
                    error(line, col, "duplicate method '" + m.sourceName + "' with the same "
                                     "parameter types in class '" + fq + "'");
                }
            }
            if (internal.empty())
                internal = nameTaken ? m.sourceName + "__" + signatureSuffix(m.paramTypes)
                                     : m.sourceName;
            m.name = internal;
            if (m.decl) {
                m.decl->sourceName = m.sourceName;
                m.decl->name = internal;
            }
            seen.push_back(&m);
        }
    };
    for (auto &[fq, ci] : classes_) assign(fq);
    for (auto &[fq, ii] : interfaces_)
        for (auto &m : ii.methods) m.sourceName = m.name;
}

std::vector<const MethodInfo *> Registry::findMethodsBySource(const std::string &classFq,
                                                              const std::string &sourceName) const {
    std::vector<const MethodInfo *> out;
    std::string cur = classFq;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        const ClassInfo *ci = cls(cur);
        if (!ci) break;
        for (const auto &m : ci->methods) {
            const std::string &src = m.sourceName.empty() ? m.name : m.sourceName;
            if (src != sourceName) continue;
            bool shadowed = false; // overridden by a more-derived class already seen
            for (const MethodInfo *o : out)
                if (o->name == m.name) { shadowed = true; break; }
            if (!shadowed) out.push_back(&m);
        }
        cur = ci->baseClass;
    }
    return out;
}

std::string Registry::implementingMethod(const std::string &classFq,
                                         const MethodInfo &want) const {
    auto matches = [&](const TypeRef &h, TypeRef w) {
        if (w.isNamed() && w.name == "Self") w = TypeRef::named(classFq);
        return h == w;
    };
    for (const MethodInfo *m : findMethodsBySource(classFq, want.name)) {
        if (m->isStatic || m->paramTypes.size() != want.paramTypes.size()) continue;
        bool ok = matches(m->returnType, want.returnType);
        for (size_t i = 0; ok && i < want.paramTypes.size(); ++i)
            ok = matches(m->paramTypes[i], want.paramTypes[i]);
        if (ok) return m->name;
    }
    return "";
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
                g.sourceName = getter;
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
                s.sourceName = setter;
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

// For each `uses field: A` delegate, forward A's public, non-static methods that
// the class does not already define to `self.field.method(...)`.
void Registry::synthesizeDelegates() {
    for (auto &[fq, ci] : classes_) {
        if (!ci.decl) continue;
        std::string ns = namespaceOf(fq);
        std::vector<MethodInfo> added;
        auto hasMethod = [&](const std::string &n) {
            for (const auto &m : ci.methods) if (m.name == n) return true;
            for (const auto &m : added) if (m.name == n) return true;
            return findMethod(ci.baseClass, n) != nullptr;
        };
        for (const auto &d : ci.decl->delegates) {
            std::string aFq = resolveType(d.type.name, ns);
            if (aFq.empty() || !isClass(aFq)) {
                error(ci.decl->line, ci.decl->col,
                      "'uses " + d.name + ": " + d.type.name + "' requires a class type");
                continue;
            }
            // Walk the delegate class hierarchy collecting forwardable methods.
            std::string cur = aFq;
            int guard = 0;
            while (!cur.empty() && guard++ < 100) {
                const ClassInfo *ac = cls(cur);
                if (!ac) break;
                for (const auto &m : ac->methods) {
                    if (m.isStatic || m.access != Access::Public) continue;
                    if (hasMethod(m.name)) continue;
                    MethodInfo fwd;
                    fwd.name = m.name;
                    fwd.sourceName = m.sourceName.empty() ? m.name : m.sourceName;
                    fwd.access = Access::Public;
                    fwd.returnType = m.returnType;
                    fwd.paramTypes = m.paramTypes;
                    fwd.paramNames = m.paramNames;
                    fwd.definingClass = fq;
                    fwd.hasBody = true;
                    fwd.accessor = AccessorKind::Delegate;
                    fwd.accessorField = d.name;
                    fwd.delegateClass = aFq;
                    added.push_back(std::move(fwd));
                }
                cur = ac->baseClass;
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
            info.extraBases = c->extraBases;     // raw, canonicalized later
            info.interfaces = c->interfaceNames; // raw
            info.decl = c;
            info.hasConstructor = c->constructor.present;
            info.constructor = c->constructor.present ? &c->constructor : nullptr;
            if (c->constructor.present) info.ctors.push_back(&c->constructor);
            for (auto &x : c->extraCtors) info.ctors.push_back(&x);
            info.destructor = c->destructor.present ? &c->destructor : nullptr;
            info.isFinal = c->isFinal;
            for (auto &f : c->fields) {
                FieldInfo fi{f.access, f.isReadonly, f.name, f.type, c->fqName};
                fi.isWeak = f.isWeak;
                info.fields.push_back(std::move(fi));
            }
            for (auto &m : c->methods) {
                MethodInfo mi;
                mi.name = m.name;
                mi.access = m.access;
                mi.isStatic = m.isStatic;
                mi.isAbstract = m.isAbstract;
                mi.isFinal = m.isFinal;
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
                mi.decl = &m; // for its parameters' default values
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
            // A task can outlive the caller's stack, where variadic arguments live.
            if (info.isAsync && info.isVariadic)
                error(fn->line, fn->col, "an async function cannot have a variadic parameter");
            // Overloading: a repeated name is allowed as long as the parameter
            // signature differs; an identical signature is a true duplicate.
            auto &set = funcOverloads_[fn->fqName];
            bool dup = false;
            for (const auto &prev : set)
                if (prev.paramTypes == info.paramTypes) { dup = true; break; }
            if (dup) {
                error(fn->line, fn->col, "duplicate function '" + fn->fqName +
                                             "' with the same parameter types");
                continue;
            }
            // First overload keeps index 0 (plain name); later ones get an index
            // so codegen can give each a distinct C symbol.
            info.overloadIndex = static_cast<int>(set.size());
            fn->overloadIndex = info.overloadIndex;
            if (set.empty()) functions_[fn->fqName] = info; // back-compat single lookup
            set.push_back(std::move(info));
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
    for (const auto &cand : nameCandidates(name, nsContext, uses_))
        if (isType(cand) || isAlias(cand)) return cand;
    if (auto dot = name.rfind('.'); dot != std::string::npos) // `Shape.Circle` -> `Shape$Circle`
        return resolveTypeOrAlias(name.substr(0, dot) + "$" + name.substr(dot + 1), nsContext);
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
    if (t.isNullable()) {
        // References can already be null: `Dog?` is `Dog`. A struct holding
        // references cannot be made nullable (yet).
        TypeRef e = resolveCanonical(*t.elem, nsContext, ok);
        if (e.isError() || e.isNullable() || e.isPointer() || e.isFunction() || e.isAny() ||
            (e.isNamed() && (isClass(e.name) || isInterface(e.name))))
            return e;
        bool fine = e.isNumeric() || e.kind == TypeRef::Kind::Bool || e.kind == TypeRef::Kind::String ||
                    (e.isNamed() && (isEnum(e.name) || (isStruct(e.name) && !structHoldsRefs(e.name))));
        if (!fine) {
            ok = false;
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return TypeRef::nullable(std::move(e));
    }
    if (t.isFunction()) {
        std::vector<TypeRef> ps;
        for (const auto &p : t.fnParams) ps.push_back(resolveCanonical(p, nsContext, ok));
        return TypeRef::function(std::move(ps), resolveCanonical(*t.elem, nsContext, ok));
    }
    if (!t.isNamed()) return t;
    // `Self` is a placeholder for the implementing type inside a trait; it stays
    // a sentinel here and is matched/substituted where the concrete class is known.
    if (t.name == "Self") return t;
    std::string fq = resolveTypeOrAlias(t.name, nsContext);
    if (fq.empty()) {
        ok = false;
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    auto it = aliases_.find(fq);
    if (it != aliases_.end()) return it->second.target; // already canonical
    return TypeRef::named(fq);
}

// Why a written type does not resolve: an unknown name, or a `T?` that cannot be.
std::string Registry::typeProblem(const TypeRef &t, const std::string &nsContext) const {
    bool ok = true;
    if (t.isNullable() && t.elem) {
        TypeRef e = resolveCanonical(*t.elem, nsContext, ok);
        if (ok)
            return "'" + typeRefName(t) + "' cannot be nullable: only numbers, bools, strings, enums and "
                   "structs without strings or objects inside can (classes can already be null)";
    }
    for (const TypeRef *x = &t; x; x = x->elem ? x->elem.get() : nullptr) {
        ok = true;
        if (x->isNamed() && resolveCanonical(*x, nsContext, ok).isError()) return "unknown type '" + typeRefName(*x) + "'";
    }
    return "unknown type '" + typeRefName(t) + "'";
}

// Does a struct (directly or in nested structs/arrays) hold strings, objects,
// closures or `any` values? Such a struct cannot be nullable.
bool Registry::structHoldsRefs(const std::string &fq) const {
    const StructInfo *si = strct(fq);
    if (!si) return false;
    std::function<bool(const TypeRef &)> refs = [&](const TypeRef &t) -> bool {
        if (t.kind == TypeRef::Kind::String || t.isFunction() || t.isAny() || t.isNullable()) return true;
        if (t.isArray() && t.elem) return refs(*t.elem);
        if (t.isNamed()) {
            if (isClass(t.name) || isInterface(t.name)) return true;
            if (isStruct(t.name)) return structHoldsRefs(t.name);
        }
        return false;
    };
    for (const auto &f : si->fields) if (refs(f.type)) return true;
    return false;
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
    if (!ok) error(line, col, typeProblem(t, nsContext));
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
        for (auto &eb : ci.extraBases) {
            std::string b = resolveType(eb, ns);
            if (b.empty() || !isClass(b)) {
                error(line, col, "unknown base class '" + eb + "'");
                eb.clear();
            } else {
                eb = b;
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
            if (cd->fields[i].isWeak) {
                const TypeRef &wt = cd->fields[i].type;
                if (!wt.isNamed() || !classes_.count(wt.name))
                    error(cd->fields[i].line, cd->fields[i].col,
                          "'weak' needs a class type: field '" + cd->fields[i].name + "' is " + typeRefName(wt));
            }
        }
        for (auto &p : cd->constructor.params) p.type = canonType(p.type, ns, line, col);
        for (auto &x : cd->extraCtors)
            for (auto &p : x.params) p.type = canonType(p.type, ns, line, col);
        // Constructor overloads must differ in their parameter types.
        for (size_t a = 0; a < ci.ctors.size(); ++a)
            for (size_t b = a + 1; b < ci.ctors.size(); ++b) {
                bool same = ci.ctors[a]->params.size() == ci.ctors[b]->params.size();
                for (size_t k = 0; same && k < ci.ctors[a]->params.size(); ++k)
                    same = ci.ctors[a]->params[k].type == ci.ctors[b]->params[k].type;
                if (same)
                    error(ci.ctors[b]->line, ci.ctors[b]->col,
                          "duplicate constructor with the same parameter types in class '" + fq + "'");
            }
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
    for (auto &[fq, set] : funcOverloads_) {
        std::string ns = namespaceOf(fq);
        for (auto &fi : set) {
            FunctionDecl *fd = fi.decl;
            fd->returnType = canonType(fd->returnType, ns, fd->line, fd->col);
            fi.returnType = fd->returnType;
            for (size_t j = 0; j < fd->params.size(); ++j) {
                fd->params[j].type = canonType(fd->params[j].type, ns, fd->line, fd->col);
                fi.paramTypes[j] = fd->params[j].type;
            }
        }
        functions_[fq] = set[0]; // keep the single-lookup copy in sync
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
    // A field name is declared once per struct, and once along a class's chain
    // of bases (base fields are part of every subclass instance).
    for (auto &[fq, si] : structs_)
        for (size_t i = 0; i < si.decl->fields.size(); ++i)
            for (size_t j = 0; j < i; ++j)
                if (si.decl->fields[j].name == si.decl->fields[i].name) {
                    error(si.decl->fields[i].line, si.decl->fields[i].col,
                          "duplicate field '" + si.decl->fields[i].name + "' in struct '" + fq + "'");
                    break;
                }
    for (auto &[fq, ci] : classes_) {
        const auto &fields = ci.decl->fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            bool dup = false;
            for (size_t j = 0; j < i && !dup; ++j) dup = fields[j].name == fields[i].name;
            const FieldInfo *inherited = ci.baseClass.empty() ? nullptr : findField(ci.baseClass, fields[i].name);
            if (dup)
                error(fields[i].line, fields[i].col, "duplicate field '" + fields[i].name + "' in class '" + fq + "'");
            else if (inherited)
                error(fields[i].line, fields[i].col, "field '" + fields[i].name + "' is already declared in base class '" +
                                                         inherited->definingClass + "'");
        }
    }
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

        // `final` classes cannot be extended (by a primary or secondary base).
        auto checkFinalBase = [&](const std::string &b) {
            if (const ClassInfo *bc = cls(b); bc && bc->isFinal)
                error(line, col, "class '" + fq + "' cannot extend final class '" + b + "'");
        };
        if (!ci.baseClass.empty()) checkFinalBase(ci.baseClass);
        for (const auto &eb : ci.extraBases) if (!eb.empty()) checkFinalBase(eb);

        // Override signatures must match the base method.
        for (auto &m : ci.methods) {
            if (ci.baseClass.empty()) continue;
            const MethodInfo *base = findMethod(ci.baseClass, m.name);
            if (!base) continue;
            if (base->isFinal && m.decl)
                error(m.decl->line, m.decl->col,
                      "method '" + m.name + "' overrides final method in '" +
                          base->definingClass + "'");
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
                // Overloads: prefer the method whose signature matches exactly.
                const MethodInfo *have = nullptr;
                if (std::string impl = implementingMethod(fq, want); !impl.empty())
                    have = findMethod(fq, impl);
                if (!have) {
                    auto all = findMethodsBySource(fq, want.name);
                    have = all.empty() ? nullptr : all.front();
                }
                if (!have || have->isStatic) {
                    error(line, col, "class '" + fq + "' does not implement method '" +
                                         want.name + "' required by interface '" + ifq + "'");
                    continue;
                }
                // An interface type of `Self` is satisfied by the class's own type.
                auto matches = [&](const TypeRef &h, TypeRef w) {
                    if (w.isNamed() && w.name == "Self") w = TypeRef::named(fq);
                    return h == w;
                };
                bool ok = matches(have->returnType, want.returnType) &&
                          have->paramTypes.size() == want.paramTypes.size();
                for (size_t i = 0; ok && i < want.paramTypes.size(); ++i)
                    ok = matches(have->paramTypes[i], want.paramTypes[i]);
                if (!ok) {
                    error(line, col, "method '" + want.name + "' in '" + fq +
                                         "' does not match interface '" + ifq + "'");
                }
            }
        }
    }
}

} // namespace grav
