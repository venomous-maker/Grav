#!/usr/bin/env node
// tools/docs-site/build.js — the documentation website, generated from docs/**/*.md.
//
//   node tools/docs-site/build.js            # writes site/ (open site/index.html)
//   node tools/docs-site/build.js out/dir    # somewhere else
//
// No dependencies: a Markdown renderer for the subset the docs use (headings, paragraphs,
// lists, tables, code fences, quotes, links, emphasis), a Grav highlighter run at build
// time (pages need no script to show colour), a search index, and one page template.
// Every link is relative, so the site works from any path (GitHub Pages serves it under
// /Grav/).
"use strict";
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "../..");
const DOCS = path.join(ROOT, "docs");
const OUT = path.resolve(process.argv[2] || path.join(ROOT, "site"));
const REPO = "https://github.com/venomous-maker/Grav";
const ASSETS = path.join(__dirname, "assets");

// ── reading ────────────────────────────────────────────────────────────────
const read = (p) => fs.readFileSync(p, "utf8");
const version = (() => {
  const m = read(path.join(ROOT, "CHANGELOG.md")).match(/^## (\d+\.\d+\.\d+)/m);
  return m ? m[1] : "";
})();

// ── inline Markdown ───────────────────────────────────────────────────────
const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

function slug(text, used) {
  let s = text.toLowerCase().replace(/<[^>]+>/g, "").replace(/[`*_]/g, "")
    .replace(/[^\p{L}\p{N}\s-]/gu, "").trim().replace(/\s+/g, "-");
  if (!s) s = "section";
  let out = s, n = 1;
  while (used.has(out)) out = `${s}-${n++}`;
  used.add(out);
  return out;
}

// Rewrites a link found in `from` (a docs-relative path) for the site.
function href(url, from) {
  if (/^(https?:|mailto:|#)/.test(url)) return url;
  const [target, frag] = url.split("#");
  if (!target) return url;
  const abs = path.normalize(path.join(path.dirname(from), target));
  if (abs.startsWith("..")) {
    // outside docs/: the file on GitHub
    const repoPath = path.normalize(path.join("docs", path.dirname(from), target)).replace(/\\/g, "/");
    return `${REPO}/${target.endsWith("/") ? "tree" : "blob"}/main/${repoPath.replace(/\/$/, "")}${frag ? "#" + frag : ""}`;
  }
  let page = target.endsWith("/") ? target + "index.html" : target.replace(/README\.md$/, "index.html").replace(/\.md$/, ".html");
  return page + (frag ? "#" + frag : "");
}

function inline(src, from) {
  const codes = [];
  let s = src.replace(/(`+)([\s\S]*?[^`])\1(?!`)/g, (_, __, code) => {
    codes.push(`<code>${esc(code.trim() === "" ? code : code.replace(/^ (.*) $/, "$1"))}</code>`);
    return `\u0000${codes.length - 1}\u0000`;
  });
  s = s.replace(/\\([\\`*_{}\[\]()#+\-.!|<>])/g, (_, c) => `\u0001${c.charCodeAt(0)}\u0001`);
  s = esc(s);
  s = s.replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, (_, alt, url) => `<img src="${href(url, from)}" alt="${alt}">`);
  s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (_, text, url) => {
    const h = href(url.replace(/&amp;/g, "&"), from);
    const ext = /^https?:/.test(h) ? ' rel="noopener"' : "";
    return `<a href="${esc(h)}"${ext}>${text}</a>`;
  });
  s = s.replace(/&lt;(https?:\/\/[^\s&]+)&gt;/g, '<a href="$1" rel="noopener">$1</a>');
  s = s.replace(/\*\*([^*]+?)\*\*/g, "<strong>$1</strong>");
  s = s.replace(/(^|[\s(])\*([^*\s][^*]*?)\*(?=[\s).,;:!?]|$)/g, "$1<em>$2</em>");
  s = s.replace(/(^|[\s(])_([^_\s][^_]*?)_(?=[\s).,;:!?]|$)/g, "$1<em>$2</em>");
  s = s.replace(/\u0001(\d+)\u0001/g, (_, c) => esc(String.fromCharCode(+c)));
  s = s.replace(/\u0000(\d+)\u0000/g, (_, i) => codes[+i]);
  return s;
}

// ── highlighting ──────────────────────────────────────────────────────────
const GRAV_KEYWORDS = new Set(("if else while for in step do switch match case default break continue return try catch " +
  "finally throw defer yield async await public private protected static readonly abstract final export weak operator " +
  "new free is as fn let const var class struct interface trait enum extends implements namespace import from use " +
  "test bench constructor destructor self super typeof sizeof spawn").split(" "));
const GRAV_TYPES = new Set("int long uint ulong byte bool float double float32 string binary any void char".split(" "));
const GRAV_CONST = new Set("true false null".split(" "));
const C_KEYWORDS = new Set(("auto break case char const continue default do double else enum extern float for goto if inline int " +
  "long register return short signed sizeof static struct switch typedef union unsigned void volatile while include define").split(" "));
const SH_KEYWORDS = new Set("if then else fi for do done case esac while in function export local return".split(" "));

function highlight(code, lang) {
  const kw = lang === "grav" || lang === "typescript" || lang === "ts" ? GRAV_KEYWORDS : lang === "c" ? C_KEYWORDS
    : (lang === "bash" || lang === "sh" || lang === "shell") ? SH_KEYWORDS : null;
  if (!kw && lang !== "toml" && lang !== "json" && lang !== "yaml") return esc(code);
  const hashComments = lang === "bash" || lang === "sh" || lang === "shell" || lang === "toml" || lang === "yaml";
  let out = "", i = 0;
  const n = code.length;
  const span = (cls, text) => `<span class="t-${cls}">${esc(text)}</span>`;
  while (i < n) {
    const c = code[i], rest = code.slice(i);
    let m;
    if (!hashComments && rest.startsWith("//")) { const e = code.indexOf("\n", i); const j = e < 0 ? n : e; out += span("c", code.slice(i, j)); i = j; continue; }
    if (!hashComments && rest.startsWith("/*")) { const e = code.indexOf("*/", i + 2); const j = e < 0 ? n : e + 2; out += span("c", code.slice(i, j)); i = j; continue; }
    if (hashComments && c === "#" && (i === 0 || /\s/.test(code[i - 1]))) { const e = code.indexOf("\n", i); const j = e < 0 ? n : e; out += span("c", code.slice(i, j)); i = j; continue; }
    if (lang === "grav" && rest.startsWith("%{")) { const e = code.indexOf("%}", i + 2); const j = e < 0 ? n : e + 2; out += span("x", code.slice(i, j)); i = j; continue; }
    if (rest.startsWith('"""') || rest.startsWith('r"""')) {
      const start = rest.startsWith("r") ? i + 4 : i + 3;
      const e = code.indexOf('"""', start); const j = e < 0 ? n : e + 3;
      out += span("s", code.slice(i, j)); i = j; continue;
    }
    if (c === '"' || c === "'" || (c === "r" && code[i + 1] === '"' && lang === "grav")) {
      const q = c === "r" ? '"' : c;
      let j = c === "r" ? i + 2 : i + 1;
      while (j < n && code[j] !== q && code[j] !== "\n") { if (code[j] === "\\" && c !== "r") j++; j++; }
      j = Math.min(n, j + 1);
      const text = code.slice(i, j);
      // ${…} inside Grav strings stays readable as code
      if (lang === "grav" && text.includes("${")) {
        out += `<span class="t-s">${esc(text).replace(/\$\{([^}]*)\}/g, '<span class="t-i">${$1}</span>')}</span>`;
      } else out += span("s", text);
      i = j; continue;
    }
    if ((m = /^(0x[0-9a-fA-F_]+|0b[01_]+|\d[\d_]*(\.\d+)?([eE][+-]?\d+)?)/.exec(rest)) && !/[\w]/.test(code[i - 1] || "")) {
      out += span("n", m[0]); i += m[0].length; continue;
    }
    if ((m = /^[A-Za-z_][\w]*/.exec(rest))) {
      const w = m[0];
      const next = code.slice(i + w.length).match(/^\s*(\(|<)/);
      if (kw && kw.has(w)) out += span("k", w);
      else if (lang === "grav" && GRAV_TYPES.has(w)) out += span("t", w);
      else if (GRAV_CONST.has(w)) out += span("n", w);
      else if (lang === "grav" && /^[A-Z]/.test(w)) out += span("t", w);
      else if (lang === "grav" && next && next[1] === "(") out += span("f", w);
      else if ((lang === "toml" || lang === "yaml") && /^\s*[=:]/.test(code.slice(i + w.length)) && /(^|\n)\s*$/.test(code.slice(0, i))) out += span("f", w);
      else out += esc(w);
      i += w.length; continue;
    }
    if (lang === "c" && c === "#") { const e = code.indexOf("\n", i); const j = e < 0 ? n : e; out += span("k", code.slice(i, j)); i = j; continue; }
    out += esc(c); i++;
  }
  return out;
}

// ── block Markdown ────────────────────────────────────────────────────────
// Returns { html, title, headings: [{level, text, id, body}] }.
function render(md, from) {
  const lines = md.replace(/\r\n/g, "\n").replace(/<!--[\s\S]*?-->/g, "").split("\n");
  const used = new Set();
  const headings = [];
  let title = "";

  function blocks(ls) {
    let html = "", i = 0;
    const isBlank = (l) => /^\s*$/.test(l);
    while (i < ls.length) {
      const l = ls[i];
      if (isBlank(l)) { i++; continue; }
      let m;
      if ((m = /^(\s*)(```+|~~~+)\s*([\w+-]*)/.exec(l))) {
        const fence = m[2], lang = m[3] || "", ind = m[1].length;
        const body = [];
        i++;
        while (i < ls.length && !ls[i].trim().startsWith(fence)) { body.push(ls[i].slice(Math.min(ind, ls[i].length - ls[i].trimStart().length))); i++; }
        i++;
        const code = body.join("\n");
        html += codeBlock(code, lang);
        continue;
      }
      if ((m = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(l))) {
        const level = m[1].length, raw = m[2];
        const text = inline(raw, from);
        const plain = text.replace(/<[^>]+>/g, "").replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"');
        const id = slug(raw, used);
        if (level === 1 && !title) { title = plain; html += `<h1 id="${id}">${text}</h1>\n`; }
        else html += `<h${level} id="${id}"><a class="anchor" href="#${id}" aria-hidden="true" tabindex="-1">#</a>${text}</h${level}>\n`;
        headings.push({ level, text: plain, id, body: "" });
        i++;
        continue;
      }
      if (/^\s*(---+|\*\*\*+)\s*$/.test(l)) { html += "<hr>\n"; i++; continue; }
      if (/^\s*>/.test(l)) {
        const q = [];
        while (i < ls.length && /^\s*>/.test(ls[i])) { q.push(ls[i].replace(/^\s*> ?/, "")); i++; }
        html += `<blockquote>${blocks(q)}</blockquote>\n`;
        continue;
      }
      if (/^\s*\|/.test(l) && i + 1 < ls.length && /^\s*\|?\s*:?-{3,}/.test(ls[i + 1])) {
        const cells = (row) => row.trim().replace(/^\|/, "").replace(/\|$/, "").split(/(?<!\\)\|/).map((c) => c.trim().replace(/\\\|/g, "|"));
        const head = cells(l);
        const aligns = cells(ls[i + 1]).map((c) => c.startsWith(":") && c.endsWith(":") ? "center" : c.endsWith(":") ? "right" : "");
        i += 2;
        let t = "<div class=\"table\"><table>\n<thead><tr>" + head.map((h, k) => `<th${aligns[k] ? ` style="text-align:${aligns[k]}"` : ""}>${inline(h, from)}</th>`).join("") + "</tr></thead>\n<tbody>\n";
        while (i < ls.length && /^\s*\|/.test(ls[i])) {
          t += "<tr>" + cells(ls[i]).map((c, k) => `<td${aligns[k] ? ` style="text-align:${aligns[k]}"` : ""}>${inline(c, from)}</td>`).join("") + "</tr>\n";
          i++;
        }
        html += t + "</tbody></table></div>\n";
        continue;
      }
      if ((m = /^(\s*)([-*+]|\d+[.)])\s+/.exec(l))) {
        const ordered = /\d/.test(m[2]);
        const base = m[1].length;
        const items = [];
        let start = ordered ? parseInt(m[2], 10) : 1;
        while (i < ls.length) {
          const mm = /^(\s*)([-*+]|\d+[.)])\s+(.*)$/.exec(ls[i]);
          if (!mm || mm[1].length !== base || /\d/.test(mm[2]) !== ordered) break;
          const contentIndent = mm[1].length + mm[2].length + 1;
          const body = [mm[3]];
          i++;
          while (i < ls.length) {
            const nl = ls[i];
            if (isBlank(nl)) {
              // a blank line continues the item only when indented text follows
              if (i + 1 < ls.length && (ls[i + 1].length - ls[i + 1].trimStart().length) > base && !isBlank(ls[i + 1])) { body.push(""); i++; continue; }
              break;
            }
            const ind = nl.length - nl.trimStart().length;
            if (ind <= base && /^(\s*)([-*+]|\d+[.)])\s+/.test(nl)) break;
            if (ind <= base && !/^\s*$/.test(nl) && body.length && isBlank(body[body.length - 1])) break;
            body.push(nl.slice(Math.min(ind, contentIndent)));
            i++;
          }
          items.push(body);
        }
        const tag = ordered ? "ol" : "ul";
        html += `<${tag}${ordered && start !== 1 ? ` start="${start}"` : ""}>\n`;
        for (const b of items) {
          const inner = blocks(b);
          const tight = (inner.match(/<p>/g) || []).length === 1 && inner.startsWith("<p>");
          html += `<li>${tight ? inner.replace(/^<p>([\s\S]*?)<\/p>\n?/, "$1") : inner}</li>\n`;
        }
        html += `</${tag}>\n`;
        continue;
      }
      // a paragraph: until a blank line or another block
      const para = [l.trim()];
      i++;
      while (i < ls.length && !isBlank(ls[i]) && !/^\s*(```|~~~|#{1,6}\s|>|\||([-*+]|\d+[.)])\s)/.test(ls[i])) { para.push(ls[i].trim()); i++; }
      const text = para.join("\n");
      if (/^Output:$/.test(text)) { html += '<p class="output-label">Output:</p>\n'; continue; }
      html += `<p>${inline(text, from).replace(/\n/g, " ")}</p>\n`;
    }
    return html;
  }

  function codeBlock(code, lang) {
    const cls = lang === "text" ? "output" : "code";
    const label = lang && lang !== "text" ? `<span class="lang">${esc(lang)}</span>` : "";
    return `<div class="${cls}">${label}<pre><code${lang ? ` class="language-${esc(lang)}"` : ""}>${highlight(code, lang)}</code></pre></div>\n`;
  }

  let html = blocks(lines);
  // "Output:" joins the code above it with the output below into one unit
  html = html.replace(/<div class="code">([\s\S]*?)<\/div>\n<p class="output-label">Output:<\/p>\n<div class="output">([\s\S]*?)<\/div>\n/g,
    '<figure class="run"><div class="code">$1</div><figcaption>Output</figcaption><div class="output">$2</div></figure>\n');
  html = html.replace(/<p class="output-label">Output:<\/p>\n/g, '<p class="output-label">Output:</p>\n');

  // section text for search: what follows each heading up to the next
  const parts = html.split(/<h[1-6] id="[^"]+">/);
  for (let k = 0; k < headings.length; k++) {
    const body = (parts[k + 1] || "").replace(/^[\s\S]*?<\/h[1-6]>/, "");
    headings[k].body = body.replace(/<pre>[\s\S]*?<\/pre>/g, " ").replace(/<[^>]+>/g, " ")
      .replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/\s+/g, " ").trim();
  }
  return { html, title, headings };
}

// ── the site's structure ──────────────────────────────────────────────────
function pageList() {
  const pages = [];
  const add = (file, section, group) => { if (fs.existsSync(path.join(DOCS, file))) pages.push({ file, section, group }); };
  add("README.md", "Overview");
  add("learn/README.md", "Learn");
  for (const f of fs.readdirSync(path.join(DOCS, "learn")).filter((f) => /^\d+-.*\.md$/.test(f)).sort()) add(`learn/${f}`, "Learn");
  add("reference/README.md", "Reference");
  const refOrder = ["syntax", "types", "operators", "statements", "builtins", "runtime", "cli"];
  for (const r of refOrder) add(`reference/${r}.md`, "Reference");
  for (const f of fs.readdirSync(path.join(DOCS, "reference")).filter((f) => f.endsWith(".md") && f !== "README.md" && !refOrder.includes(f.slice(0, -3)))) add(`reference/${f}`, "Reference");
  add("std/README.md", "Standard library");
  // categories and modules in the order the library's index lists them
  const index = read(path.join(DOCS, "std/README.md"));
  let cat = null;
  const seen = new Set();
  for (const line of index.split("\n")) {
    const h = /^###\s+(\w+)/.exec(line);
    if (h) { cat = h[1]; continue; }
    const m = /^\|\s*\[([\w_]+)\]\((\w+\/[\w_]+\.md)\)/.exec(line);
    if (m && cat) { add(`std/${m[2]}`, "Standard library", cat); seen.add(`std/${m[2]}`); }
  }
  for (const c of fs.readdirSync(path.join(DOCS, "std")).filter((d) => fs.statSync(path.join(DOCS, "std", d)).isDirectory())) {
    for (const f of fs.readdirSync(path.join(DOCS, "std", c)).filter((f) => f.endsWith(".md")).sort()) {
      if (!seen.has(`std/${c}/${f}`)) add(`std/${c}/${f}`, "Standard library", c);
    }
  }
  for (const t of ["install", "editors", "mcp", "releasing"]) add(`tools/${t}.md`, "Tools");
  return pages;
}

function navTitle(p) {
  if (p.section === "Standard library" && p.group) return path.basename(p.file, ".md");
  if (p.file.endsWith("README.md")) return p.file === "README.md" ? "Introduction" : "Overview";
  const m = /^(\d+)-/.exec(path.basename(p.file));
  return (m ? `${+m[1]}. ` : "") + p.title.replace(/^\d+\.\s*/, "");
}

function sidebar(pages, current, prefix) {
  let html = "";
  const sections = [...new Set(pages.map((p) => p.section))];
  for (const s of sections) {
    const inSection = pages.filter((p) => p.section === s);
    const open = current.section === s;
    html += `<details class="nav-section"${open ? " open" : ""}><summary>${esc(s)}</summary>\n<ul>\n`;
    let group = null;
    for (const p of inSection) {
      if (p.group !== group) {
        if (group) html += "</ul></details></li>\n";
        group = p.group;
        if (group) {
          const groupOpen = current.group === group;
          html += `<li class="nav-group"><details${groupOpen ? " open" : ""}><summary>${esc(group)}</summary><ul>\n`;
        }
      }
      const here = p === current;
      html += `<li><a href="${prefix}${p.url}"${here ? ' aria-current="page"' : ""}>${esc(navTitle(p))}</a></li>\n`;
    }
    if (group) html += "</ul></details></li>\n";
    html += "</ul></details>\n";
  }
  return html;
}

function toc(headings) {
  const hs = headings.filter((h) => h.level === 2 || h.level === 3);
  if (hs.length < 2) return "";
  return `<nav class="toc" aria-label="On this page"><p>On this page</p><ul>` +
    hs.map((h) => `<li class="l${h.level}"><a href="#${h.id}">${esc(h.text)}</a></li>`).join("") + "</ul></nav>";
}

const LOGO = read(path.join(DOCS, "assets/grav-logo.svg")).replace(/<title>[\s\S]*?<\/title>/, "").replace(/width="120" height="120"/, 'width="32" height="32"').replace(/role="img" aria-label="Grav"/, 'aria-hidden="true"');

function pageHtml(p, pages, body) {
  const depth = p.url.split("/").length - 1;
  const prefix = "../".repeat(depth);
  const i = pages.indexOf(p);
  const prev = pages[i - 1], next = pages[i + 1];
  const pager = `<nav class="pager" aria-label="Previous and next">` +
    (prev ? `<a class="prev" href="${prefix}${prev.url}"><span>Previous</span>${esc(navTitle(prev))}</a>` : "<span></span>") +
    (next ? `<a class="next" href="${prefix}${next.url}"><span>Next</span>${esc(navTitle(next))}</a>` : "<span></span>") + "</nav>";
  const title = p.file === "README.md" ? "Grav documentation" : `${p.title} · Grav`;
  const editUrl = `${REPO}/edit/main/docs/${p.file}`;
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>
<meta name="description" content="${esc((p.headings[0] && p.headings[0].body || "Grav, a statically typed language that compiles to C.").slice(0, 160))}">
<link rel="icon" href="${prefix}assets/grav-logo.svg" type="image/svg+xml">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Familjen+Grotesk:wght@500;600;700&family=JetBrains+Mono:wght@400;600&family=Source+Serif+4:ital,opsz,wght@0,8..60,400;0,8..60,600;1,8..60,400&display=swap" rel="stylesheet">
<link rel="stylesheet" href="${prefix}assets/site.css">
<script>try{var t=localStorage.getItem("grav-theme");if(t)document.documentElement.dataset.theme=t}catch(e){}</script>
</head>
<body data-root="${prefix}">
<a class="skip" href="#content">Skip to the content</a>
<header class="top">
  <button class="menu" type="button" aria-label="Show the contents" aria-expanded="false" aria-controls="sidebar"><span></span></button>
  <a class="brand" href="${prefix}index.html">${LOGO}<span>Grav</span></a>
  <span class="version">${esc(version ? "v" + version : "")}</span>
  <div class="search" role="search">
    <input id="q" type="search" placeholder="Search the docs" aria-label="Search the docs" autocomplete="off" spellcheck="false">
    <kbd>/</kbd>
    <ol id="results" role="listbox" hidden></ol>
  </div>
  <button class="theme" type="button" aria-label="Switch between light and dark"></button>
  <a class="gh" href="${REPO}" rel="noopener" aria-label="Grav on GitHub"><svg viewBox="0 0 16 16" width="20" height="20" aria-hidden="true"><path fill="currentColor" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z"/></svg></a>
</header>
<div class="layout">
<nav id="sidebar" class="sidebar" aria-label="Contents">
${sidebar(pages, p, prefix)}
</nav>
<main id="content" class="content${p.file === "README.md" ? " home" : ""}">
<article>
${body}
</article>
<p class="edit"><a href="${editUrl}" rel="noopener">Edit this page on GitHub</a></p>
${pager}
</main>
${toc(p.headings)}
</div>
<script src="${prefix}assets/site.js" defer></script>
</body>
</html>
`;
}

// ── build ─────────────────────────────────────────────────────────────────
function build() {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(path.join(OUT, "assets"), { recursive: true });
  const pages = pageList();
  for (const p of pages) {
    p.url = p.file.replace(/README\.md$/, "index.html").replace(/\.md$/, ".html");
    const r = render(read(path.join(DOCS, p.file)), p.file);
    Object.assign(p, r);
    if (!p.title) p.title = path.basename(p.file, ".md");
  }
  const index = [];
  for (const p of pages) {
    const out = path.join(OUT, p.url);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, pageHtml(p, pages, p.html));
    const where = p.section + (p.group ? " / " + p.group : "");
    for (const h of p.headings) {
      // the function names a section lists (API sections run long) are searchable too
      const names = [...new Set((h.body.match(/\bfn ([A-Za-z_]\w*)/g) || []).map((x) => x.slice(3)))];
      const text = h.body.slice(0, h.level === 1 ? 240 : 400);
      index.push([h.level === 1 ? p.title : h.text, h.level === 1 ? p.url : `${p.url}#${h.id}`, h.level === 1 ? where : p.title, text, names.join(" ")]);
    }
  }
  fs.writeFileSync(path.join(OUT, "assets/search.json"), JSON.stringify(index));
  for (const f of fs.readdirSync(ASSETS)) fs.copyFileSync(path.join(ASSETS, f), path.join(OUT, "assets", f));
  fs.copyFileSync(path.join(DOCS, "assets/grav-logo.svg"), path.join(OUT, "assets/grav-logo.svg"));
  fs.writeFileSync(path.join(OUT, ".nojekyll"), "");
  fs.writeFileSync(path.join(OUT, "404.html"), pageHtml({ file: "404.md", url: "404.html", title: "Page not found", section: "", headings: [] }, pages,
    '<h1>Page not found</h1><p>That page is not in the documentation. Try the search above, or start from the <a href="index.html">introduction</a>.</p>').replace("<head>", '<head>\n<base href="/Grav/">').replace(/data-root=""/, 'data-root="/Grav/"'));
  console.log(`wrote ${pages.length} pages to ${path.relative(process.cwd(), OUT) || "."}`);
}

if (require.main === module) build();
module.exports = { render, highlight, inline };
