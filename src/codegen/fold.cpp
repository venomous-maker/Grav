#include "codegen/fold.h"
#include "codegen/ctext.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace grav {

namespace {

using namespace ctext;

// A function definition in the text: its range, header line and name.
struct Def {
    size_t from, to;          // first line up to the end of its last
    std::string_view header;  // the `... name(...) {` line
    std::string_view name;
};

struct Instance {
    std::vector<std::string> leaves;  // class type arguments, as mangled ("app_User")
    std::vector<std::string> cnames;  // the same as C struct names ("app__User")
};

// `name` with each `$`-segment that is a leaf (alone, or followed by `_m_...` or
// `_VT`) replaced by `@n`; `changed` says whether any was.
std::string canonToken(std::string_view t, const std::vector<std::string> &leaves, bool &changed) {
    changed = false;
    std::string out;
    out.reserve(t.size());
    size_t at = t.find('$');
    if (at == std::string_view::npos) return std::string(t);
    out.append(t.substr(0, at));
    while (at != std::string_view::npos) {
        size_t next = t.find('$', at + 1);
        std::string_view seg = t.substr(at + 1, next == std::string_view::npos ? std::string_view::npos : next - at - 1);
        out += '$';
        bool done = false;
        for (size_t n = 0; n < leaves.size() && !done; ++n) {
            const std::string &lf = leaves[n];
            if (seg.size() < lf.size() || seg.compare(0, lf.size(), lf) != 0) continue;
            std::string_view rest = seg.substr(lf.size());
            if (rest.empty() || rest == "_VT" || rest.compare(0, 3, "_m_") == 0) {
                out += '@';
                out += std::to_string(n);
                out.append(rest);
                changed = done = true;
            }
        }
        if (!done) out.append(seg);
        at = next;
    }
    return out;
}

// Inside a string literal: `$<leaf>` not followed by an identifier character becomes `$<with>(n)`.
template <class With>
std::string mapLeavesInString(std::string_view lit, const std::vector<std::string> &leaves, With with) {
    std::string out;
    for (size_t i = 0; i < lit.size();) {
        if (lit[i] == '$') {
            bool hit = false;
            for (size_t n = 0; n < leaves.size(); ++n) {
                const std::string &lf = leaves[n];
                if (lit.compare(i + 1, lf.size(), lf) == 0 && (i + 1 + lf.size() >= lit.size() || !identChar(lit[i + 1 + lf.size()]) || lit[i + 1 + lf.size()] == '$')) {
                    out += '$';
                    out += with(n);
                    i += 1 + lf.size();
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        out += lit[i++];
    }
    return out;
}

bool pointerFollows(std::string_view text, size_t k) {
    while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
    return k < text.size() && text[k] == '*';
}

struct Text {
    const std::unordered_set<std::string_view> &structs;
    const std::unordered_map<std::string_view, size_t> &defs;
};

// A body with the instance's leaves as placeholders (see foldGenericInstances), and
// the generic functions it calls whose names hold a leaf.
std::string canonBody(std::string_view body, const Instance &in, const Text &tx, std::vector<std::string_view> &callees) {
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size();) {
        char ch = body[i];
        if (ch == '"') {
            size_t j = i + 1;
            while (j < body.size() && body[j] != '"') j += body[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, body.size());
            out += mapLeavesInString(body.substr(i, j - i), in.leaves, [](size_t n) { return "@" + std::to_string(n); });
            i = j;
            continue;
        }
        if (ch == '\'') {
            size_t j = i + 1;
            while (j < body.size() && body[j] != '\'') j += body[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, body.size());
            out.append(body.substr(i, j - i));
            i = j;
            continue;
        }
        if (!identStart(ch) || (i > 0 && identChar(body[i - 1]))) { out += ch; ++i; continue; }
        size_t j = i + 1;
        while (j < body.size() && identChar(body[j])) ++j;
        std::string_view t = body.substr(i, j - i);
        bool ptr = pointerFollows(body, j);
        bool isFn = tx.defs.count(t) > 0;
        bool done = false;
        if (t.find('$') != std::string_view::npos && (isFn || (ptr && (tx.structs.count(t) || (t.size() > 3 && t.compare(t.size() - 3, 3, "_VT") == 0))))) {
            bool changed = false;
            std::string c = canonToken(t, in.leaves, changed);
            if (changed) {
                out += c;
                if (isFn) callees.push_back(t);
                done = true;
            }
        } else if (ptr) {
            for (size_t n = 0; n < in.cnames.size(); ++n)
                if (t == in.cnames[n]) { out += "@C" + std::to_string(n); done = true; break; }
        }
        if (!done) out.append(t);
        i = j;
    }
    return out;
}

// A struct's layout, for comparing instances: its leaves as placeholders and any
// pointer to a struct as `@P` (pointers have one size and alignment).
std::string canonLayout(std::string_view text, const Instance &in, const std::unordered_set<std::string_view> &structs) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (!identStart(text[i]) || (i > 0 && identChar(text[i - 1]))) { out += text[i++]; continue; }
        size_t j = i + 1;
        while (j < text.size() && identChar(text[j])) ++j;
        std::string_view t = text.substr(i, j - i);
        if (pointerFollows(text, j) && structs.count(t)) out += "@P";
        else if (t.find('$') != std::string_view::npos) { bool ch; out += canonToken(t, in.leaves, ch); }
        else out.append(t);
        i = j;
    }
    return out;
}

// `a, b (*f)(int), c` -> its parameters, split at top-level commas.
std::vector<std::string_view> splitParams(std::string_view p) {
    std::vector<std::string_view> out;
    int depth = 0;
    size_t from = 0;
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || (p[i] == ',' && depth == 0)) {
            std::string_view s = p.substr(from, i - from);
            while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
            while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
            if (!s.empty()) out.push_back(s);
            from = i + 1;
        } else if (p[i] == '(') ++depth;
        else if (p[i] == ')') --depth;
    }
    return out;
}

// A header's parts: its return type (without static/inline/GRAV_FN) and its parameters.
bool headerParts(std::string_view header, std::string_view name, std::string &ret, std::vector<std::string_view> &params) {
    size_t at = header.find(name);
    if (at == std::string_view::npos) return false;
    std::string_view r = header.substr(0, at);
    std::string cleaned;
    size_t i = 0;
    while (i < r.size()) {
        size_t j = i;
        while (j < r.size() && identChar(r[j])) ++j;
        std::string_view w = r.substr(i, j - i);
        if (w == "static" || w == "inline" || w == "GRAV_FN") { i = j; while (i < r.size() && r[i] == ' ') ++i; continue; }
        if (j == i) { cleaned += r[i++]; continue; }
        cleaned.append(w);
        i = j;
    }
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    ret = cleaned;
    size_t open = at + name.size();
    if (open >= header.size() || header[open] != '(') return false;
    size_t close = header.rfind(')');
    if (close == std::string_view::npos || close < open) return false;
    params = splitParams(header.substr(open + 1, close - open - 1));
    for (auto p : params) if (p.find('(') != std::string_view::npos) return false;  // (a function-pointer parameter: not wrapped)
    return true;
}

std::string_view lastIdent(std::string_view p) {
    size_t e = p.size();
    while (e > 0 && !identChar(p[e - 1])) --e;
    size_t s = e;
    while (s > 0 && identChar(p[s - 1])) --s;
    return p.substr(s, e - s);
}

} // namespace

std::string foldGenericInstances(const std::string &c) {
    // 1) Lines, struct names, function definitions.
    std::unordered_set<std::string_view> structs;
    std::vector<std::pair<size_t, size_t>> structBlocks;  // `struct X {` .. `}` ranges
    std::vector<Def> defs;
    std::unordered_map<std::string_view, size_t> byName;
    for (size_t pos = 0; pos < c.size();) {
        size_t eol = c.find('\n', pos);
        size_t next = eol == std::string::npos ? c.size() : eol + 1;
        std::string_view line(c.data() + pos, (eol == std::string::npos ? c.size() : eol) - pos);
        std::string_view rest = line;
        if (rest.compare(0, 8, "typedef ") == 0) rest.remove_prefix(8);
        if (rest.compare(0, 7, "struct ") == 0) {
            rest.remove_prefix(7);
            size_t e = 0;
            while (e < rest.size() && identChar(rest[e])) ++e;
            if (e > 0) {
                structs.insert(rest.substr(0, e));
                if (trimRight(line).back() == '{') {
                    size_t end = blockEnd(c, pos + trimRight(line).size() - 1);
                    if (end != std::string::npos) structBlocks.push_back({pos, end});
                }
            }
        }
        std::string_view name;
        if (classify(line, name) == LineKind::Definition && !byName.count(name)) {
            size_t end = blockEnd(c, pos + trimRight(line).size() - 1);
            if (end != std::string::npos) {
                size_t lineEnd = c.find('\n', end);
                lineEnd = lineEnd == std::string::npos ? c.size() : lineEnd + 1;
                byName.emplace(name, defs.size());
                defs.push_back({pos, lineEnd, line, name});
                pos = lineEnd;
                continue;
            }
        }
        pos = next;
    }
    if (defs.empty()) return c;

    // 2) Generic instances and their class leaves.
    std::unordered_map<std::string, std::string_view> classByMangle;  // "app_User" -> "app__User"
    for (auto s : structs) {
        if (s.find('$') != std::string_view::npos || (s.size() > 3 && s.compare(s.size() - 3, 3, "_VT") == 0)) continue;
        std::string m;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '_' && i + 1 < s.size() && s[i + 1] == '_') { m += '_'; ++i; }
            else m += s[i];
        }
        classByMangle.emplace(m, s);
    }
    std::unordered_map<std::string_view, Instance> instances;
    for (auto s : structs) {
        size_t d = s.find('$');
        if (d == std::string_view::npos || (s.size() > 3 && s.compare(s.size() - 3, 3, "_VT") == 0)) continue;
        Instance in;
        while (d != std::string_view::npos) {
            size_t next = s.find('$', d + 1);
            std::string seg(s.substr(d + 1, next == std::string_view::npos ? std::string_view::npos : next - d - 1));
            auto it = classByMangle.find(seg);
            if (it != classByMangle.end() && std::find(in.leaves.begin(), in.leaves.end(), seg) == in.leaves.end()) {
                in.leaves.push_back(seg);
                in.cnames.emplace_back(it->second);
            }
            d = next;
        }
        if (!in.leaves.empty()) instances.emplace(s, std::move(in));
    }
    if (instances.empty()) return c;

    // 3) Layouts of the instances and their method tables.
    std::unordered_map<std::string_view, std::string> layout;
    for (auto [from, to] : structBlocks) {
        std::string_view text(c.data() + from, to - from);
        std::string_view rest = text;
        if (rest.compare(0, 8, "typedef ") == 0) rest.remove_prefix(8);
        rest.remove_prefix(7);
        size_t e = 0;
        while (e < rest.size() && identChar(rest[e])) ++e;
        std::string_view name = rest.substr(0, e);
        std::string_view owner = name;
        if (owner.size() > 3 && owner.compare(owner.size() - 3, 3, "_VT") == 0) owner.remove_suffix(3);
        auto it = instances.find(owner);
        if (it != instances.end()) layout[name] = canonLayout(text, it->second, structs);
    }

    // 4) The candidates: methods of instances, keyed by what must match.
    struct Member {
        size_t def;
        std::string_view instance;
        std::vector<std::string_view> callees;
    };
    std::vector<Member> members;
    std::unordered_map<size_t, size_t> memberOfDef;
    std::vector<int> group;
    std::unordered_map<std::string, int> keys;
    Text tx{structs, byName};
    for (size_t k = 0; k < defs.size(); ++k) {
        std::string_view name = defs[k].name;
        // The longest instance name that, with `_m_`, starts it.
        std::string_view owner;
        for (size_t at = name.rfind("_m_"); at != std::string_view::npos && at > 0; at = at == 0 ? std::string_view::npos : name.rfind("_m_", at - 1)) {
            std::string_view cand = name.substr(0, at);
            if (instances.count(cand)) { owner = cand; break; }
            if (at == 0) break;
        }
        if (owner.empty()) continue;
        const Instance &in = instances.at(owner);
        Member m{k, owner, {}};
        std::string body = canonBody(std::string_view(c.data() + defs[k].from, defs[k].to - defs[k].from), in, tx, m.callees);
        bool ch;
        std::string key = canonToken(owner, in.leaves, ch);
        key += '\x1f';
        key.append(name.substr(owner.size()));
        key += '\x1f';
        key += body;
        key += '\x1f';
        auto l1 = layout.find(owner);
        if (l1 != layout.end()) key += l1->second;
        key += '\x1f';
        std::string vt(owner);
        vt += "_VT";
        auto l2 = layout.find(std::string_view(vt));
        if (l2 != layout.end()) key += l2->second;
        memberOfDef[k] = members.size();
        group.push_back(keys.emplace(std::move(key), (int)keys.size()).first->second);
        members.push_back(std::move(m));
    }
    if (members.size() < 2) return c;

    // 5) Refine: members of a group fold only when what they call folds together too.
    for (size_t round = 0; round < 64; ++round) {
        std::map<std::vector<long>, int> sig;
        std::vector<int> next(members.size());
        for (size_t i = 0; i < members.size(); ++i) {
            std::vector<long> s{group[i]};
            const Instance &in = instances.at(members[i].instance);
            for (auto callee : members[i].callees) {
                auto d = byName.find(callee);
                auto mm = d == byName.end() ? memberOfDef.end() : memberOfDef.find(d->second);
                if (mm != memberOfDef.end()) s.push_back(group[mm->second]);
                else {
                    bool ch;
                    s.push_back(-1 - (long)std::hash<std::string>{}(canonToken(callee, in.leaves, ch)));
                }
            }
            next[i] = sig.emplace(std::move(s), (int)sig.size()).first->second;
        }
        size_t before = std::set<int>(group.begin(), group.end()).size();
        group.swap(next);
        if (std::set<int>(group.begin(), group.end()).size() == before) break;
    }

    // 6) Each group's first member keeps its body; the others become wrappers.
    std::unordered_map<int, std::vector<size_t>> groups;
    for (size_t i = 0; i < members.size(); ++i) groups[group[i]].push_back(i);
    std::unordered_map<size_t, std::string> replace;  // def -> its new text
    std::unordered_map<size_t, const Instance *> relabel;
    for (auto &[g, ms] : groups) {
        if (ms.size() < 2) continue;
        std::sort(ms.begin(), ms.end(), [&](size_t a, size_t b) { return members[a].def < members[b].def; });
        const Def &rep = defs[members[ms[0]].def];
        std::string rret;
        std::vector<std::string_view> rparams;
        if (!headerParts(rep.header, rep.name, rret, rparams)) continue;
        bool any = false;
        for (size_t x = 1; x < ms.size(); ++x) {
            const Def &d = defs[members[ms[x]].def];
            std::string ret;
            std::vector<std::string_view> params;
            if (!headerParts(d.header, d.name, ret, params) || params.size() != rparams.size()) continue;
            std::string call(rep.name);
            call += '(';
            bool first = true;
            for (size_t p = 0; p < params.size(); ++p) {
                if (params[p] == "void") continue;
                std::string_view pn = lastIdent(params[p]), rpn = lastIdent(rparams[p]);
                std::string_view rtype = rparams[p].substr(0, rparams[p].size() - rpn.size());
                while (!rtype.empty() && rtype.back() == ' ') rtype.remove_suffix(1);
                if (!first) call += ", ";
                first = false;
                if (rtype.find('*') != std::string_view::npos) { call += '('; call.append(rtype); call += ')'; }
                call.append(pn);
            }
            call += ')';
            std::string text(d.header);
            text += '\n';
            if (ret == "void") text += "    " + call + ";\n";
            else if (ret.find('*') != std::string::npos) text += "    return (" + ret + ")" + call + ";\n";
            else text += "    return " + call + ";\n";
            text += "}\n";
            replace[members[ms[x]].def] = std::move(text);
            any = true;
        }
        if (any) relabel[members[ms[0]].def] = &instances.at(members[ms[0]].instance);
    }
    if (replace.empty()) return c;

    // 7) The C with the wrappers, and the kept bodies' trace labels made generic.
    std::string out;
    out.reserve(c.size());
    size_t at = 0;
    for (size_t k = 0; k < defs.size(); ++k) {
        auto r = replace.find(k);
        auto l = relabel.find(k);
        if (r == replace.end() && l == relabel.end()) continue;
        out.append(c, at, defs[k].from - at);
        if (r != replace.end()) {
            out += r->second;
        } else {
            std::string_view body(c.data() + defs[k].from, defs[k].to - defs[k].from);
            for (size_t pos = 0; pos < body.size();) {
                size_t eol = body.find('\n', pos);
                size_t next = eol == std::string_view::npos ? body.size() : eol + 1;
                std::string_view line = body.substr(pos, next - pos);
                size_t q = line.find("grav_enter(\"");
                if (q != std::string_view::npos) {
                    size_t e = line.find('"', q + 12);
                    if (e != std::string_view::npos) {
                        out.append(line.substr(0, q + 11));
                        out += mapLeavesInString(line.substr(q + 11, e + 1 - (q + 11)), l->second->leaves, [](size_t) { return std::string("*"); });
                        out.append(line.substr(e + 1));
                        pos = next;
                        continue;
                    }
                }
                out.append(line);
                pos = next;
            }
        }
        at = defs[k].to;
    }
    out.append(c, at, std::string::npos);
    return out;
}

std::string compactLineDirectives(const std::string &c) {
    std::string out;
    out.reserve(c.size());
    std::string_view current;
    for (size_t pos = 0; pos < c.size();) {
        size_t eol = c.find('\n', pos);
        size_t next = eol == std::string::npos ? c.size() : eol + 1;
        std::string_view line(c.data() + pos, next - pos);
        if (line.compare(0, 6, "#line ") == 0) {
            size_t q = line.find('"');
            size_t qe = q == std::string_view::npos ? q : line.rfind('"');
            if (q != std::string_view::npos && qe > q) {
                std::string_view file = line.substr(q + 1, qe - q - 1);
                if (file == current) {
                    std::string_view num = line.substr(0, q);
                    while (!num.empty() && num.back() == ' ') num.remove_suffix(1);
                    out.append(num);
                    out += '\n';
                    pos = next;
                    continue;
                }
                current = file;
            }
        }
        out.append(line);
        pos = next;
    }
    return out;
}

} // namespace grav
