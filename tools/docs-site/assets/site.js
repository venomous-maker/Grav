// The Grav documentation site: theme, contents drawer, search, copy buttons, and the
// "on this page" marker. Pages read fine without it.
(function () {
  "use strict";
  var root = document.body.dataset.root || "";
  var html = document.documentElement;

  // ── theme ──
  var themeBtn = document.querySelector(".theme");
  function dark() {
    var t = html.dataset.theme;
    return t ? t === "dark" : window.matchMedia("(prefers-color-scheme: dark)").matches;
  }
  if (themeBtn) themeBtn.addEventListener("click", function () {
    var next = dark() ? "light" : "dark";
    html.dataset.theme = next;
    try { localStorage.setItem("grav-theme", next); } catch (e) {}
  });

  // ── contents drawer (narrow screens) ──
  var menu = document.querySelector(".menu");
  if (menu) menu.addEventListener("click", function () {
    var open = document.body.classList.toggle("nav-open");
    menu.setAttribute("aria-expanded", open ? "true" : "false");
  });
  document.addEventListener("click", function (e) {
    if (document.body.classList.contains("nav-open") && !e.target.closest(".sidebar") && !e.target.closest(".menu")) {
      document.body.classList.remove("nav-open");
      menu.setAttribute("aria-expanded", "false");
    }
  });
  var current = document.querySelector('.sidebar a[aria-current="page"]');
  var side = document.querySelector(".sidebar");
  if (current && side) side.scrollTop = current.offsetTop - side.clientHeight / 2;   // the sidebar only, not the page

  // ── copy buttons ──
  document.querySelectorAll(".code").forEach(function (block) {
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "copy";
    btn.textContent = "Copy";
    btn.addEventListener("click", function () {
      var text = block.querySelector("pre").innerText;
      navigator.clipboard.writeText(text).then(function () {
        btn.textContent = "Copied";
        setTimeout(function () { btn.textContent = "Copy"; }, 1500);
      });
    });
    block.appendChild(btn);
  });

  // ── on this page ──
  var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".toc a"));
  if (tocLinks.length && "IntersectionObserver" in window) {
    var byId = {};
    tocLinks.forEach(function (a) { byId[a.getAttribute("href").slice(1)] = a; });
    var visible = {};
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) { visible[e.target.id] = e.isIntersecting; });
      var heads = document.querySelectorAll("article h2[id], article h3[id]");
      var on = null;
      for (var i = 0; i < heads.length; i++) { if (visible[heads[i].id]) { on = heads[i].id; break; } }
      if (!on) return;
      tocLinks.forEach(function (a) { a.classList.toggle("on", a === byId[on]); });
    }, { rootMargin: "-10% 0px -70% 0px" });
    document.querySelectorAll("article h2[id], article h3[id]").forEach(function (h) { io.observe(h); });
  }

  // ── search ──
  var input = document.getElementById("q");
  var list = document.getElementById("results");
  if (!input || !list) return;
  var index = null, loading = null, picked = -1;
  function load() {
    if (index || loading) return loading;
    loading = fetch(root + "assets/search.json").then(function (r) { return r.json(); }).then(function (d) {
      index = d.map(function (e) { return { title: e[0], url: e[1], where: e[2], text: e[3], names: e[4] || "", lt: e[0].toLowerCase(), lx: (e[3] + " " + (e[4] || "")).toLowerCase() }; });
    });
    return loading;
  }
  function escapeHtml(s) { return s.replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; }); }
  function mark(s, words) {
    var out = escapeHtml(s);
    words.forEach(function (w) {
      if (w.length < 2) return;
      out = out.replace(new RegExp("(" + w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")", "ig"), "<mark>$1</mark>");
    });
    return out;
  }
  function snippet(text, words) {
    var lx = text.toLowerCase(), at = -1;
    for (var i = 0; i < words.length && at < 0; i++) at = lx.indexOf(words[i]);
    if (at < 0) return text.slice(0, 130);
    if (at >= text.length) return text.slice(0, 130);
    var start = Math.max(0, at - 50);
    return (start ? "…" : "") + text.slice(start, start + 140) + (start + 140 < text.length ? "…" : "");
  }
  // the listed functions a query matched, shown after the snippet
  function nameHits(names, words) {
    if (!names) return "";
    var hits = names.split(" ").filter(function (n) { var l = n.toLowerCase(); return words.some(function (w) { return l.indexOf(w) >= 0; }); });
    return hits.length ? " · " + hits.slice(0, 6).join(", ") : "";
  }
  function search(q) {
    var words = q.toLowerCase().split(/\s+/).filter(Boolean);
    if (!words.length) return [];
    var scored = [];
    for (var i = 0; i < index.length; i++) {
      var e = index[i], score = 0, ok = true;
      for (var k = 0; k < words.length; k++) {
        var w = words[k], inTitle = e.lt.indexOf(w), inText = e.lx.indexOf(w);
        if (inTitle < 0 && inText < 0) { ok = false; break; }
        if (inTitle === 0) score += 12; else if (inTitle > 0) score += 7;
        if (e.lt === w) score += 10;
        if (inText >= 0) score += 2;
      }
      if (!ok) continue;
      if (e.url.indexOf("#") < 0) score += 3;          // a page itself before its sections
      scored.push([score, e]);
    }
    scored.sort(function (a, b) { return b[0] - a[0]; });
    return scored.slice(0, 12).map(function (s) { return s[1]; });
  }
  function show() {
    var q = input.value.trim();
    if (!q) { list.hidden = true; return; }
    load().then(function () {
      var hits = search(q), words = q.toLowerCase().split(/\s+/).filter(Boolean);
      picked = hits.length ? 0 : -1;
      list.innerHTML = hits.length ? hits.map(function (h, i) {
        return '<li><a role="option" href="' + root + h.url + '"' + (i === 0 ? ' aria-selected="true"' : "") + '>' +
          '<span class="r-title">' + mark(h.title, words) + '</span><span class="r-where">' + escapeHtml(h.where) + "</span>" +
          '<span class="r-text">' + mark(snippet(h.text, words) + nameHits(h.names, words), words) + "</span></a></li>";
      }).join("") : '<li class="empty">Nothing matches “' + escapeHtml(q) + '”. Try a class name such as Pool, or a word from the text.</li>';
      list.hidden = false;
    });
  }
  function move(d) {
    var links = list.querySelectorAll("a");
    if (!links.length) return;
    if (picked >= 0) links[picked].removeAttribute("aria-selected");
    picked = (picked + d + links.length) % links.length;
    links[picked].setAttribute("aria-selected", "true");
    links[picked].scrollIntoView({ block: "nearest" });
  }
  input.addEventListener("focus", load);
  input.addEventListener("input", show);
  input.addEventListener("keydown", function (e) {
    if (e.key === "ArrowDown") { e.preventDefault(); move(1); }
    else if (e.key === "ArrowUp") { e.preventDefault(); move(-1); }
    else if (e.key === "Enter") { var a = list.querySelectorAll("a")[picked]; if (a) location.href = a.href; }
    else if (e.key === "Escape") { list.hidden = true; input.blur(); }
  });
  document.addEventListener("keydown", function (e) {
    if (e.key === "/" && document.activeElement !== input && !/input|textarea/i.test(document.activeElement.tagName)) {
      e.preventDefault();
      input.focus();
    }
  });
  document.addEventListener("click", function (e) { if (!e.target.closest(".search")) list.hidden = true; });
})();
