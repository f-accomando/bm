-- Host tests of the bmweb cart, stage 1 (pure Lua: URL, HTML, CSS, layout).
-- Run from the repository root through tests/web/run.sh.
--   run.sh            check everything
--   run.sh --update   rewrite tests/web/expected/*.txt (then read the diff)
package.path = "carts/bmweb/src/?.lua;" .. package.path

local url = require("url")
local html = require("html")
local css = require("css")
local layout = require("layout")
local page = require("page")
local dump = require("dump")

local UPDATE = arg and arg[1] == "--update"
local checks, fails = 0, 0

local function fail(msg)
  fails = fails + 1
  print("FAIL: " .. msg)
end
local function check(cond, msg)
  checks = checks + 1
  if not cond then fail(msg) end
end
local function eq(got, want, msg)
  checks = checks + 1
  if got ~= want then fail(string.format("%s\n    got:  %s\n    want: %s", msg, tostring(got), tostring(want))) end
end

local function readfile(p)
  local f = io.open(p, "rb")
  if not f then return nil end
  local s = f:read("a")
  f:close()
  return s
end
local function writefile(p, s)
  local f = assert(io.open(p, "wb"))
  f:write(s)
  f:close()
end

local BASE = "https://example.test/"
local function load(src, o)
  o = o or {}
  o.url = o.url or BASE
  o.width = o.width or 240
  return page.load(src, o)
end

-- text of one line: runs joined, a space where there is a gap
local function linetext(line, cw)
  local parts, prev = {}, nil
  for _, r in ipairs(line.runs) do
    if prev and r.x - (prev.x + prev.w) >= (cw or 6) then parts[#parts + 1] = " " end
    parts[#parts + 1] = r.text
    prev = r
  end
  return table.concat(parts)
end
local function linetexts(p)
  local t = {}
  for i, l in ipairs(p.lines) do t[i] = linetext(l, p.cw) end
  return t
end
local function find_run(p, text)
  for _, l in ipairs(p.lines) do
    for _, r in ipairs(l.runs) do
      if r.text and r.text:find(text, 1, true) then return r, l end
    end
  end
end
-- y of the line that holds a run
local function ly(p, text)
  local _, l = find_run(p, text)
  return l and l.y
end
local function walk(node, fn)
  fn(node)
  for _, k in ipairs(node.kids or {}) do walk(k, fn) end
end
local function find_node(root, pred)
  local found
  walk(root, function(n) if not found and pred(n) then found = n end end)
  return found
end
local function text_of(node)
  local t = {}
  walk(node, function(n) if n.tag == "#text" then t[#t + 1] = n.text end end)
  return table.concat(t)
end
local function count_nodes(root)
  local n = 0
  walk(root, function() n = n + 1 end)
  return n
end
local function depth_of(node)
  local d = 0
  for _, k in ipairs(node.kids or {}) do d = math.max(d, depth_of(k)) end
  return d + 1
end
local function tags(node)
  local t = {}
  for i, k in ipairs(node.kids) do t[i] = k.tag end
  return table.concat(t, ",")
end
local function inside(p)
  for _, l in ipairs(p.lines) do
    for _, r in ipairs(l.runs) do
      if r.x < 0 or r.x + r.w > p.w then return false, r end
    end
  end
  return true
end

---------------------------------------------------------------------------
print("-- url")
---------------------------------------------------------------------------
local B = "https://example.test/a/b/c.html?x=1#f"
local resolve_cases = {
  { "d.html", "https://example.test/a/b/d.html" },
  { "../d", "https://example.test/a/d" },
  { "../../../d", "https://example.test/d" },
  { "../../..", "https://example.test/" },
  { "/d", "https://example.test/d" },
  { "//other.test/p", "https://other.test/p" },
  { "\\\\other.test/p", "https://other.test/p" },
  { "?q=2", "https://example.test/a/b/c.html?q=2" },
  { "#z", "https://example.test/a/b/c.html?x=1#z" },
  { "", "https://example.test/a/b/c.html?x=1" },
  { "./", "https://example.test/a/b/" },
  { ".", "https://example.test/a/b/" },
  { "x/./y/../z", "https://example.test/a/b/x/z" },
  { "d?q=1#h", "https://example.test/a/b/d?q=1#h" },
  { "HTTPS://Example.TEST:443/p", "https://example.test/p" },
  { "https://example.test:8443/p", "https://example.test:8443/p" },
  { "https://[::1]:8443/x", "https://[::1]:8443/x" },
  { "https://a.test", "https://a.test/" },
  { "/a b", "https://example.test/a%20b" },
  { "  \t/trim\n  ", "https://example.test/trim" },
  { "https://a.test/x/../../y", "https://a.test/y" },
}
for _, c in ipairs(resolve_cases) do
  eq(url.resolve(B, c[1]), c[2], "resolve " .. string.format("%q", c[1]))
end
local refused = {
  "javascript:alert(1)", "JaVaScRiPt:alert(1)", "java\nscript:alert(1)", " javascript:alert(1)",
  "file:///etc/passwd", "data:text/html,hi", "ftp://x.test/f", "mailto:a@b.test", "http://plain.test/",
  "https://user:pw@host.test/", "https://host with space/", "https://ho_st.test/", "https://:80/",
  "https://a.test:0/", "https://a.test:70000/", "blob:https://a.test/x", "view-source:https://a.test/",
}
for _, r in ipairs(refused) do
  local u, why = url.resolve(B, r)
  check(u == nil and why, "refused " .. string.format("%q", r))
end
eq(url.resolve(B, "http://plain.test/x", { allow_http = true }), "http://plain.test/x", "http with allow_http")
check(url.resolve(B, "file:///x", { allow_http = true }) == nil, "file refused even with allow_http")
check(url.resolve(B, "/" .. string.rep("a", 2100)) == nil, "URL over 2048 bytes refused")
check(url.resolve("/relative", "x") == nil, "relative base refused")
eq(url.check("https://a.test/b/../c"), "https://a.test/c", "check normalizes")
check(url.check("//a.test/") == nil, "check needs a scheme")
eq(url.strip_fragment("https://a.test/p?q#f"), "https://a.test/p?q", "strip_fragment")
eq(url.from_input("example.test/path"), "https://example.test/path", "bar: no scheme is https")
eq(url.from_input("localhost:8080/x"), "https://localhost:8080/x", "bar: host:port")
check(url.from_input("javascript:alert(1)") == nil, "bar: javascript refused")
check(url.from_input("http://a.test") == nil, "bar: http refused by default")
eq(url.from_input("http://a.test", { allow_http = true }), "http://a.test/", "bar: http allowed")
check(url.from_input("") == nil, "bar: empty")
eq(url.form_get("https://a.test/s?old=1#x", { { "q", "a&b=c/\u{E9}" }, { "n", "two words" } }),
  "https://a.test/s?q=a%26b%3Dc%2F%C3%A9&n=two+words", "form_get encodes and replaces the query")

---------------------------------------------------------------------------
print("-- tokenizer and tree builder")
---------------------------------------------------------------------------
local function tokens(src, chunk)
  local out = {}
  local tk = html.tokenizer(function(t)
    local last = out[#out]
    if t.t == "text" and last and last.t == "text" then last.s = last.s .. t.s
    else out[#out + 1] = t end
  end)
  if chunk then for i = 1, #src, chunk do tk:feed(src:sub(i, i + chunk - 1)) end else tk:feed(src) end
  tk:finish()
  local s = {}
  for _, t in ipairs(out) do
    if t.t == "text" then s[#s + 1] = "T(" .. t.s .. ")"
    else
      local keys = {}
      for k in pairs(t.attrs or {}) do keys[#keys + 1] = k end
      table.sort(keys)
      for i, k in ipairs(keys) do keys[i] = k .. "=" .. t.attrs[k] end
      s[#s + 1] = (t.t == "end" and "/" or "<") .. t.name .. (#keys > 0 and (" " .. table.concat(keys, ",")) or "") .. (t.self and "/" or "")
    end
  end
  return table.concat(s, " ")
end
local tsrc = [[<!DOCTYPE html><P class=a id="b" data-x='1>2'>Hi &amp; <b>bye</b><br/><!-- c -->x<script>if(a<b){"</p>"}</script><style>p{}</style><textarea><b>&lt;</textarea><?xml?></P>]]
local whole = tokens(tsrc)
eq(whole, "<p class=a,data-x=1>2,id=b T(Hi & ) <b T(bye) /b <br/ T(x) <script /script <style T(p{}) /style <textarea T(<b><) /textarea /p",
  "tokens of a mixed snippet")
for _, n in ipairs({ 1, 2, 3, 5, 7, 16 }) do
  eq(tokens(tsrc, n), whole, "tokens are the same with chunks of " .. n)
end
eq(tokens("a < b <3 </> c"), "T(a < b <3  c)", "stray < and bogus end tag")
eq(tokens("<a href=x/>t"), "<a href=x/ T(t)", "a bare value keeps its slash")
eq(tokens("x &am"), "T(x &am)", "partial entity at end stays text")

local function parse(src, o) return html.parse(src, o) end
local b = parse("<p>a<p>b")
eq(tags(b.body), "p,p", "p closes p")
b = parse("<ul><li>a<li>b</ul><p>x")
eq(tags(b.body), "ul,p", "ul then p")
eq(tags(b.body.kids[1]), "li,li", "li closes li")
b = parse("<table><tr><td>a<td>b<tr><td>c</table>")
local tbl = b.body.kids[1]
eq(tags(tbl), "tr,tr", "tr closes tr")
eq(tags(tbl.kids[1]), "td,td", "td closes td")
b = parse("<dl><dt>a<dd>b<dt>c<dd>d</dl>")
eq(tags(b.body.kids[1]), "dt,dd,dt,dd", "dt and dd close each other")
b = parse("<select><option>a<option selected>b</select>")
eq(tags(b.body.kids[1]), "option,option", "option closes option")
b = parse("<b><i>x</b>y</i>z")
eq(tags(b.body), "b,#text", "bad nesting: </b> pops <i>, stray </i> ignored")
eq(text_of(b.body), "xyz", "bad nesting keeps the text")
b = parse("<div>x</p>y</span>z</div>")
eq(tags(b.body.kids[1]), "#text", "stray end tags are ignored")
eq(text_of(b.body), "xyz", "stray end tags keep the text")
b = parse("<td>cell</td><table><tr><td><div>in</span></div>after</td></tr></table>")
check(find_node(b.body, function(n) return n.tag == "table" end) ~= nil, "table survives a stray end tag in a cell")
b = parse("<foo>a<bar>b</bar></foo>")
eq(tags(b.body), "span", "unknown tag becomes span")
eq(tags(b.body.kids[1]), "#text,span", "unknown nested tag becomes span")
b = parse("<p>a</p><script>var x = '<div>';</script><p>b</p><noscript><p>n</p></noscript><iframe><p>i</p></iframe>")
eq(tags(b.body), "p,p,iframe", "script and noscript are dropped, the iframe stays as an empty element")
eq(b.scripts, 1, "script is counted")
b = parse('<svg aria-label="logo"><g><path d="x"/></g></svg><p>after</p>')
eq(tags(b.body), "svg,p", "svg keeps no children")
eq(b.body.kids[1].attrs["aria-label"], "logo", "svg keeps its label")
b = parse("<title>t &amp; u</title><meta charset=UTF-8><base href='/b/'><p>x")
eq(b.title, "t & u", "title")
eq(b.charset, "utf-8", "charset")
eq(b.base_href, "/b/", "base href")
b = parse("text before any tag <b>bold</b>")
eq(tags(b.body), "#text,b", "text without html and body")
b = parse("<html><head><title>T</title></head>  <body bgcolor=x class=k>hi</body></html> trailing")
eq(b.body.attrs.class, "k", "body attributes")
eq(text_of(b.body), "hi trailing", "content after </html> is kept")
b = parse("<P>one</P><p>two")
eq(tags(b.body), "p,p", "tag names are case-insensitive")

---------------------------------------------------------------------------
print("-- size caps")
---------------------------------------------------------------------------
do
  local deep = ("<div>"):rep(200) .. "deep" .. ("</div>"):rep(200) .. "<p>after</p>"
  local bb = parse(deep)
  check(depth_of(bb.root) <= 64 + 1, "depth is capped at 64 (got " .. depth_of(bb.root) .. ")")
  eq(bb.depth_cut, 200 - 62, "depth_cut counts the flattened divs")
  eq(text_of(bb.body), "deepafter", "text below the depth cap is kept")
  eq(tags(bb.body), "div,p", "end tags past the cap do not close outer elements")

  local many = ("<p>x</p>"):rep(25000)
  bb = parse(many)
  check(bb.nodes <= 20000, "node cap: nodes <= 20000 (got " .. bb.nodes .. ")")
  check(bb.cut and bb.cut_reason == "nodes", "node cap sets cut")
  check(count_nodes(bb.root) <= 20000 + 1, "node cap: tree size")
  bb = parse(many, { max_nodes = 100 })
  eq(bb.nodes, 100, "max_nodes option")

  local big = "<p>" .. ("word "):rep(250000) .. "</p>"
  bb = parse(big)
  eq(bb.bytes, 1048576, "byte cap reads exactly 1 MiB")
  check(bb.cut and bb.cut_reason == "bytes", "byte cap sets cut")
  bb = parse(big, { max_bytes = 1000 })
  eq(bb.bytes, 1000, "max_bytes option")
  -- fed in chunks the cap is the same
  local p2 = page.new({ url = BASE, max_bytes = 5000 })
  for i = 1, 20000, 1000 do p2:feed(("<p>word word</p>"):rep(100):sub(1, 1000)) end
  local pg = p2:finish()
  check(pg.cut and pg.cut_reason == "bytes" and pg.stats.bytes == 5000, "byte cap with chunks")

  local longattr = ("X"):rep(5000)
  local src = '<p>a</p><div title="' .. longattr .. '">b</div><p>c</p>'
  for _, chunk in ipairs({ false, 1000, 100 }) do
    local o = {}
    local pp = page.new({ url = BASE })
    if chunk then for i = 1, #src, chunk do pp:feed(src:sub(i, i + chunk - 1)) end else pp:feed(src) end
    local res = pp:finish()
    eq(res.stats.tags_dropped, 1, "a 5 KB tag is dropped (chunk " .. tostring(chunk) .. ")")
    check(find_run(res, "a") and find_run(res, "b") and find_run(res, "c"), "text around a dropped tag is kept (chunk " .. tostring(chunk) .. ")")
  end
  src = "<p>a</p><!--" .. ("-"):rep(10000) .. "x--><p>c</p>"
  local pp = page.new({ url = BASE })
  for i = 1, #src, 700 do pp:feed(src:sub(i, i + 699)) end
  local res = pp:finish()
  check(find_run(res, "a") and find_run(res, "c") and not find_run(res, "x"), "a 10 KB comment is dropped")

  local r = css.rules()
  r:add((".a{color:red}"):rep(6000))
  check(r.cut and r.bytes == 65536, "css cap: 64 KiB of CSS (got " .. r.bytes .. ")")
  check(r.n <= 4000, "css rule cap")
  r = css.rules({ max_rules = 3 })
  r:add(".a{color:red}.b{color:red}.c{color:red}.d{color:red}")
  check(r.n == 3 and r.cut, "max_rules option")

  local paras = ("<p>line</p>"):rep(100)
  res = load(paras, { max_lines = 50 })
  eq(res.nlines, 50, "line cap")
  check(res.cut and res.cut_reason == "lines", "line cap sets cut")
  res = load(("<img src=a.png width=1 height=1>"):rep(40))
  eq(#res.images, 16, "image cap: 16 images get an id")
end

---------------------------------------------------------------------------
print("-- css")
---------------------------------------------------------------------------
do
  local function style_of(src, finder, sheet)
    local bb = parse(src)
    local rules = css.rules()
    if sheet then rules:add(sheet) end
    local node = find_node(bb.body, finder)
    local chain, n = {}, node
    while n and n ~= bb.root do table.insert(chain, 1, n); n = n.parent end
    local st
    for _, c in ipairs(chain) do st = css.compute(c, rules, st) end
    return st, rules
  end
  local function tag(t) return function(n) return n.tag == t end end
  local st = style_of('<p id=a class="c d">x</p>', tag("p"), "p{color:red} .c{color:#00ff00} #a{color:blue} p{color:black}")
  eq(st.color, 0x0000FF, "id beats class beats tag")
  st = style_of('<p class="c">x</p>', tag("p"), "p{color:red} .c{color:#00ff00}")
  eq(st.color, 0x00FF00, "class beats tag")
  st = style_of("<p>x</p>", tag("p"), "p{color:red} p{color:#112233}")
  eq(st.color, 0x112233, "later rule wins at equal specificity")
  st = style_of('<p style="color:rgb(1,2,3)">x</p>', tag("p"), "#a, p{color:red!important}")
  eq(st.color, 0x010203, "inline style wins")
  st = style_of("<div class=x><p>y</p></div>", tag("p"), ".x p{color:green} p{margin:5px 6px 7px 8px}")
  eq(st.color, 0x008000, "descendant selector")
  eq(st.mt .. "," .. st.mr .. "," .. st.mb .. "," .. st.ml, "5,6,7,8", "margin shorthand with 4 values")
  st = style_of("<p>y</p>", tag("p"), "p{padding:2px 4px;margin:1em;margin-left:50%}")
  eq(st.pt .. "," .. st.pr .. "," .. st.pl, "2,4,4", "padding with 2 values")
  eq(st.mt, 12, "1em is 12 px")
  eq(st.ml, "50%", "percent stays a percent")
  eq(css.px(st.ml, 200), 100, "percent resolves against the width")
  st = style_of("<div><p>y</p></div>", tag("p"), "div{color:red;font-weight:700;text-align:center;text-decoration:underline;font-style:italic}")
  check(st.color == 0xFF0000 and st.bold and st.align == "center" and st.ul and st.italic, "inherited properties")
  st = style_of("<div><p>y</p></div>", tag("p"), "div{background-color:red;padding:4px}")
  check(st.bg == nil and st.pt == 0, "background and padding are not inherited")
  st = style_of("<p class=x>y</p>", tag("p"), ".x{color:var(--c);margin:calc(1px + 2px);display:flex;float:left;position:absolute}")
  eq(st.display, "block", "flex is block")
  eq(st.color, 0, "var() without a value is ignored")
  eq(st.mt, 3, "calc() of px")
  st = style_of("<span>y</span>", tag("span"), "span{display:none}")
  eq(st.display, "none", "display none")
  st = style_of("<span class='hidden'>y</span>", tag("span"), nil)
  eq(st.display, "none", "class hidden hides without a rule")
  st = style_of("<span hidden>y</span>", tag("span"), nil)
  eq(st.display, "none", "hidden attribute hides")
  local _, rules = style_of("<p>x</p>", tag("p"), [[
    /* comment */ @import url(x.css); @media (max-width:1px) { p { color: red } } @font-face { font-family: x }
    a:hover { color: red } a > b { color: red } [x] { color: red } p::before { color: red } ul li { color: blue }
    @keyframes k { from { top: 0 } to { top: 1 } } em { color: red }]])
  eq(rules.dropped, 2, "pseudo-classes without a meaning here and pseudo-elements are dropped")
  eq(rules.n, 4, "rules kept: a > b, [x], ul li and em")
  -- selectors, variables and media queries
  local function pick(html, finder, sheet, field)
    local st = style_of(html, finder, sheet)
    return st[field]
  end
  eq(pick("<div><p class=a>1</p></div>", tag("p"), "div > p{color:#010101}", "color"), 0x010101, "child combinator")
  eq(pick("<div><span><p>1</p></span></div>", tag("p"), "div > p{color:#010101}", "color"), 0, "child combinator is not descendant")
  eq(pick("<p a=1 b=\"x y\">1</p>", tag("p"), "p[a]{color:#020202}", "color"), 0x020202, "attribute exists")
  eq(pick("<p a=1 b=\"x y\">1</p>", tag("p"), "p[b~=y]{color:#030303}", "color"), 0x030303, "attribute word")
  eq(pick("<a href=\"https://x/y.pdf\">1</a>", tag("a"), "a[href$='.pdf']{color:#040404}", "color"), 0x040404, "attribute ends with")
  eq(pick("<a href=\"https://x/y.pdf\">1</a>", tag("a"), "a[href^=https]{color:#050505}", "color"), 0x050505, "attribute starts with")
  eq(pick("<ul><li>1</li><li id=t>2</li><li>3</li></ul>", function(n) return n.attrs and n.attrs.id == "t" end,
    "li:nth-child(2){color:#060606}", "color"), 0x060606, "nth-child(2)")
  eq(pick("<ul><li>1</li><li id=t>2</li><li>3</li></ul>", function(n) return n.attrs and n.attrs.id == "t" end,
    "li:nth-child(odd){color:#060606}", "color"), 0, "nth-child(odd) skips the second")
  eq(pick("<ul><li>1</li><li>2</li><li id=t>3</li></ul>", function(n) return n.attrs and n.attrs.id == "t" end,
    "li:last-child{color:#070707}", "color"), 0, "last-child needs the whole tree")
  eq(pick("<ul><li id=t>1</li><li>2</li></ul>", function(n) return n.attrs and n.attrs.id == "t" end,
    "li:first-child{color:#080808}", "color"), 0x080808, "first-child")
  eq(pick("<p>1</p><p id=t>2</p>", function(n) return n.attrs and n.attrs.id == "t" end,
    "p + p{color:#090909}", "color"), 0x090909, "adjacent sibling")
  eq(pick("<p class=a>1</p>", tag("p"), "p:not(.b){color:#0a0a0a}", "color"), 0x0a0a0a, "not()")
  eq(pick("<p class=a>1</p>", tag("p"), "p:not(.a){color:#0a0a0a}", "color"), 0, "not() excludes")
  eq(pick("<p>1</p>", tag("p"), ":root{--c:#0b0b0b} p{color:var(--c)}", "color"), 0, "root is the html element, not seen by the test")
  eq(pick("<div><p>1</p></div>", tag("p"), "div{--c:#0c0c0c} p{color:var(--c)}", "color"), 0x0c0c0c, "custom property inherited")
  eq(pick("<p>1</p>", tag("p"), "p{color:var(--nope, #0d0d0d)}", "color"), 0x0d0d0d, "var() fallback")
  eq(pick("<p>1</p>", tag("p"), "p{margin-top:calc(2em + 4px)}", "mt"), 28, "calc with em")
  eq(pick("<p>1</p>", tag("p"), "p{color:hsl(120,100%,50%)}", "color"), 0x00ff00, "hsl")
  eq(pick("<p>1</p>", tag("p"), "p{color:rgb(1 2 3 / 50%)}", "color"), 0x010203, "rgb space syntax")
  eq(pick("<p>1</p>", tag("p"), "p{color:#11223344}", "color"), 0x112233, "#rrggbbaa")
  eq(pick("<p>1</p>", tag("p"), "@media (max-width: 700px){p{color:#0e0e0e}}", "color"), 0x0e0e0e, "media max-width true")
  eq(pick("<p>1</p>", tag("p"), "@media (min-width: 900px){p{color:#0e0e0e}}", "color"), 0, "media min-width false")
  eq(pick("<p>1</p>", tag("p"), "@media print{p{color:#0e0e0e}}", "color"), 0, "media print false")
  eq(pick("<p>1</p>", tag("p"), "@media screen and (min-width:600px){p{color:#0f0f0f}}", "color"), 0x0f0f0f, "media screen and min-width")
  eq(pick("<p>1</p>", tag("p"), "p{text-transform:uppercase;white-space:pre}", "tt"), "upper", "text-transform")
  eq(pick("<p>1</p>", tag("p"), "p{border:1px solid #ccc}", "bt"), true, "border sets sides")
  eq(pick("<p>1</p>", tag("p"), "p{border:1px solid #ccc;border-bottom:none}", "bb"), false, "border-bottom none")
  eq(pick("<p>1</p>", tag("p"), "p{width:calc(100% - 4px)}", "w"), nil, "mixed calc with % is ignored")
  eq(pick("<p>1</p>", tag("p"), "p{width:200px;max-width:100px}", "maxw"), 100, "max-width")
  eq(pick("<p>1</p>", tag("p"), "p{background: url(x.png) #123456 no-repeat}", "bg"), 0x123456, "background shorthand colour")
end

---------------------------------------------------------------------------
print("-- layout, exact positions")
---------------------------------------------------------------------------
do
  local p = load("<p>aaa bbb ccc ddd</p>", { width = 56 })
  eq(table.concat(linetexts(p), "|"), "aaa bbb|ccc ddd", "wrap at 8 columns")
  eq(p.lines[1].y, 12, "first line y: the p margin (12) wins over the body margin (4)")
  eq(p.lines[2].y, 24, "second line y")
  eq(p.lines[1].runs[1].x, 4, "body margin gives x = 4")
  eq(p.lines[1].runs[1].w, 42, "run width = 7 chars * 6")
  eq(p.h, 36, "page height")
  p = load("<p>one</p><p>two</p>", { width = 56 })
  eq(p.lines[2].y, 36, "margins of two paragraphs collapse (12, not 24)")
  p = load("<p>abcd</p>", { width = 56 })
  eq(p.lines[1].runs[1].x, 4, "left aligned")
  p = load('<p style="text-align:center">abcd</p><p style="text-align:right">abcd</p>', { width = 56 })
  eq(p.lines[1].runs[1].x, 4 + 12, "centered")
  eq(p.lines[2].runs[1].x, 4 + 24, "right aligned")
  p = load("<p>a   \n  b\t\tc</p>", { width = 200 })
  eq(table.concat(linetexts(p), "|"), "a b c", "white space is collapsed")
  p = load("<p>x<b>y</b> <i>z</i></p>", { width = 200 })
  eq(table.concat(linetexts(p), "|"), "xy z", "no space between x and y, one space before z")
  eq(#p.lines[1].runs, 3, "three style runs")
  check(p.lines[1].runs[2].bold and p.lines[1].runs[3].italic, "bold and italic flags")
  p = load("<p>abcdefghijklmnopqrstuvwxyz</p>", { width = 56 })
  eq(table.concat(linetexts(p), "|"), "abcdefgh|ijklmnop|qrstuvwx|yz", "a long word is cut into lines")
  p = load("<p>a<br>b<br><br>c</p>", { width = 200 })
  eq(table.concat(linetexts(p), "|"), "a|b||c", "br and a blank line")
  p = load("<pre>\nab  cd\n  x\n\nz</pre>", { width = 200 })
  eq(table.concat(linetexts(p), "|"), "ab  cd|  x||z", "pre keeps spaces and blank lines, drops the first newline")
  p = load("<p>No&nbsp;break here</p>", { width = 200 })
  eq(table.concat(linetexts(p), "|"), "No break here", "nbsp shown as a space")
  p = load("<p>caf\u{E9} na\u{EF}ve</p>", { width = 200 })
  eq(p.lines[1].runs[1].w, 10 * 6, "width counts UTF-8 characters, not bytes")
  -- background and padding
  p = load('<div style="background-color:#eeeeee;padding:4px">box</div>', { width = 100 })
  eq(#p.rects, 1, "one background rect")
  local r = p.rects[1]
  eq(r.x .. "," .. r.y .. "," .. r.w .. "," .. r.h .. "," .. r.color, "4,4,92,20," .. 0xeeeeee, "background rect")
  eq(p.lines[1].runs[1].x, 8, "padding moves the text")
  eq(p.lines[1].y, 8, "padding moves the text down")
  -- hr and headings
  p = load("<h1>Title</h1><hr><p>t</p>", { width = 200 })
  eq(p.rects[1].kind, "hr", "hr rect")
  check(p.lines[1].runs[1].bold, "heading is bold")
  -- lists
  p = load("<ul><li>one</li><li>two</li></ul>", { width = 200 })
  eq(p.lines[1].runs[1].text, "*", "bullet")
  eq(p.lines[1].runs[1].x, 22 - 6 - 6, "bullet x")
  eq(p.lines[1].runs[2].x, 22, "item text x (body 4 + list padding 18)")
  eq(p.lines[2].y, p.lines[1].y + 12, "items are on consecutive lines")
  p = load('<ol start="3"><li>a<li>b<ul><li>c</ul></ol>', { width = 200 })
  eq(p.lines[1].runs[1].text, "3.", "ol start")
  eq(p.lines[2].runs[1].text, "4.", "ol next number")
  eq(p.lines[3].runs[2].x, 22 + 18, "nested list is indented")
  p = load("<ul><li><p>para</p></li></ul>", { width = 200 })
  eq(p.lines[1].runs[1].text, "*", "marker goes with the first line of a block child")
  check(#p.lines == 1, "marker does not make its own line")
  -- links
  p = load('<p><a href="/x">hello</a> world</p>', { width = 200 })
  eq(#p.links, 1, "one link")
  eq(p.links[1].href, "https://example.test/x", "link href is resolved")
  eq(p.links[1].text, "hello", "link text")
  local bx = p.links[1].boxes[1]
  eq(bx.x .. "," .. bx.y .. "," .. bx.w .. "," .. bx.h, "4,12,30,12", "link box")
  check(p.lines[1].runs[1].ul and p.lines[1].runs[1].color == 0x0000EE, "link is blue and underlined")
  local kind, id = layout.hit(p, 10, 15)
  eq(kind .. id, "link1", "hit inside the link")
  check(layout.hit(p, 34, 15) == nil, "no hit just right of the box")
  check(layout.hit(p, 10, 24) == nil, "no hit below the box")
  p = load('<p>aaa <a href="/l">bbb ccc ddd</a></p>', { width = 56 })
  eq(#p.links[1].boxes, 2, "a wrapped link has one box per line")
  local b1, b2 = p.links[1].boxes[1], p.links[1].boxes[2]
  eq(b1.x .. "," .. b1.y .. "," .. b1.w, "28,12,18", "first box")
  eq(b2.x .. "," .. b2.y .. "," .. b2.w, "4,24,42", "second box")
  kind, id = layout.hit(p, 10, 30)
  eq(kind .. id, "link1", "hit on the second line of the link")
  p = load('<a href="/a">one</a> <a href="/b">two</a>', { width = 200 })
  eq(#p.links, 2, "two links in reading order")
  eq(p.links[2].href, "https://example.test/b", "second link")
  p = load('<a href="http://x.test/">x</a>', { allow_http = true })
  eq(#p.links, 1, "allow_http lets an http link through")
  p = load('<base href="/sub/"><a href="p.html">x</a>')
  eq(p.links[1].href, "https://example.test/sub/p.html", "base href")
  -- images
  p = load('<p>x <img src="a.png" width="100" height="50" alt="A"> y</p>', { width = 240 })
  eq(p.lines[1].h, 50, "line is as high as the image")
  local im = p.images[1]
  eq(im.box.w .. "x" .. im.box.h, "100x50", "image box size")
  eq(im.box.y, p.lines[1].y, "image sits at the bottom of the line")
  eq(layout.run_y(p.lines[1], p.lines[1].runs[1]), p.lines[1].y + 50 - 12, "text sits at the bottom of the line")
  eq(im.src, "https://example.test/a.png", "image src")
  p = load('<img src="w.webp" width="1220" height="720">', { width = 240 })
  eq(p.images[1].w .. "x" .. p.images[1].h, "232x136", "a wide image is scaled to the line")
  p = load('<img src="a.png" alt="Some alt text">', { width = 240 })
  eq(p.images[1].w, (13 + 2) * 6, "alt-only box width")
  eq(p.lines[1].runs[1].text, "Some alt text", "alt text in the box")
  p = load('<img src="data:image/png;base64,AA" alt="d"><img src="javascript:x"><img src="file:///x">')
  eq(#p.images, 0, "unsafe image sources get no id")
  eq(p.stats.refused, 3, "refused sources are counted")
  p = load('<a href="/m"><img src="a.png" width="10" height="10"></a>')
  kind, id = layout.hit(p, 6, p.lines[1].y + 5)
  eq(kind .. id, "link1", "an image inside a link is a link hit")
  p = load('<svg width="30" height="20" aria-label="logo"></svg>')
  eq(p.lines[1].runs[1].kind, "svg", "svg box")
  eq(p.lines[1].runs[1].text, "logo", "svg label")
  -- tables
  p = load("<table><tr><td>one<td>two<tr><td>three<td>four</table>", { width = 240 })
  local one, two = find_run(p, "one"), find_run(p, "two")
  local three, four = find_run(p, "three"), find_run(p, "four")
  check(ly(p, "one") == ly(p, "two") and ly(p, "three") == ly(p, "four") and ly(p, "three") > ly(p, "one"), "implied cells share a row")
  check(one.x == three.x and two.x == four.x and two.x > one.x, "columns line up")
  p = load("<table><tr><td>aaaa bbbb cccc dddd eeee ffff<td>gggg hhhh iiii jjjj kkkk llll</table>", { width = 100 })
  check(inside(p), "a narrow table stays inside the width")
  local c1, c2 = find_run(p, "aaaa"), find_run(p, "gggg")
  check(ly(p, "aaaa") == ly(p, "gggg") and c2.x > c1.x, "narrow table keeps two columns when the min widths fit")
  p = load("<table><tr><td>aaaaaaaaaaaaaaaaaaaa<td>bbbbbbbbbbbbbbbbbbbb</table>", { width = 60 })
  check(inside(p), "stacked cells stay inside the width")
  c1, c2 = find_run(p, "aaaa"), find_run(p, "bbbb")
  check(ly(p, "bbbb") > ly(p, "aaaa"), "too narrow: cells of a row stack")
  p = load('<table border=1><tr><th>A</th><td>b</td></tr></table>', { width = 240 })
  eq(#p.rects, 2, "border rects: one per cell")
  check(find_run(p, "A").bold, "th is bold")
  -- hidden content
  p = load('<p>a<span style="display:none">SECRET</span><span class="hidden">SECRET</span>b</p>', { width = 200 })
  eq(table.concat(linetexts(p), "|"), "ab", "display:none is not laid out")
  p = load("<p>a</p><title>T</title><style>p{color:red}</style>")
  eq(#p.lines, 1, "title and style text is not shown")
  -- control and form
  p = load('<form action="/search?old=1" method="get"><input type="hidden" name="src" value="web">'
    .. '<input type="text" name="q" value="old tree"><select name="s"><option value="a">A<option value="b" selected>B</select>'
    .. '<input type="checkbox" name="c" value="1" checked><input type="checkbox" name="d" value="2">'
    .. '<input type="password" name="pw" value="secret"><input type="submit" name="go" value="Go"></form>')
  eq(#p.forms, 1, "one form")
  eq(p.forms[1].action, "https://example.test/search?old=1", "form action")
  local full = layout.form_url(p, 1, { q = "low poly" })
  eq(full, "https://example.test/search?src=web&q=low+poly&s=b&c=1", "GET form URL")
  eq(layout.form_url(p, 1, nil, 6), "https://example.test/search?src=web&q=old+tree&s=b&c=1&go=Go", "pressed submit control")
  check(not full:find("pw", 1, true) and not full:find("secret", 1, true), "password field is never sent")
  check(find_run(p, "password refused") ~= nil, "password input shows a refusal")
  local ctl = p.controls[2]
  kind, id = layout.hit(p, ctl.box.x + 2, ctl.box.y + 2)
  eq(kind .. id, "ctl2", "hit on a text field")
  p = load('<form action="/p" method="POST"><input name="a"></form>')
  local u, why = layout.form_url(p, 1)
  check(u == nil and why == "POST not supported", "POST forms are refused")
  p = load('<form action="javascript:x()"><input name="a"></form>')
  check(layout.form_url(p, 1) == nil, "javascript: form action is refused")
  -- visible range
  p = load(("<p>line</p>"):rep(100), { width = 100 })
  local f, l = layout.visible(p, 600, 700)
  local lf, ll
  for i, ln in ipairs(p.lines) do
    if ln.y < 700 and ln.y + ln.h > 600 then lf = lf or i; ll = i end
  end
  check(f <= lf and l >= ll and p.lines[f].y + p.maxh > 600 - 1 - p.maxh, "visible range covers the screen")
  check(f > 1 and l < #p.lines, "visible range is a slice")
  f, l = layout.visible(p, 100000, 100100)
  check(l < f, "empty range below the page")
end

---------------------------------------------------------------------------
print("-- fixtures")
---------------------------------------------------------------------------
local FIXTURES = { "headings", "paragraphs", "links", "lists", "images", "tables", "entities", "malformed", "search_page", "html5" }
local P = {}
for _, name in ipairs(FIXTURES) do
  local src = assert(readfile("tests/web/fixtures/" .. name .. ".html"), name)
  local lay = load(src)
  local tree = load(src, { stream = false })
  P[name] = lay
  local d1, d2 = dump.page(lay), dump.page(tree)
  eq(d1, d2, name .. ": streaming and full tree give the same page")
  for _, chunk in ipairs({ 1, 3, 17, 512 }) do
    eq(dump.page(load(src, { chunk = chunk })), d1, name .. ": same page with chunks of " .. chunk)
  end
  check(inside(lay), name .. ": all runs inside the page width")
  -- golden files
  local tree_dump = dump.tree(tree.tree)
  for _, g in ipairs({ { "tree", tree_dump }, { "lay", d1 } }) do
    local path = "tests/web/expected/" .. name .. "." .. g[1] .. ".txt"
    if UPDATE then writefile(path, g[2])
    else
      local want = readfile(path)
      if not want then fail(path .. " is missing (run with --update)")
      elseif want ~= g[2] then
        local i = 1
        local a, b2 = {}, {}
        for line in (g[2] .. "\n"):gmatch("([^\n]*)\n") do a[#a + 1] = line end
        for line in (want .. "\n"):gmatch("([^\n]*)\n") do b2[#b2 + 1] = line end
        while a[i] == b2[i] do i = i + 1 end
        fail(string.format("%s differs at line %d\n    got:  %s\n    want: %s", path, i, tostring(a[i]), tostring(b2[i])))
      else checks = checks + 1 end
    end
  end
end

-- Facts about each fixture, written from the HTML (not from the dump).
do
  local p = P.headings
  eq(p.title, "Headings test", "headings: title is trimmed and collapsed")
  eq(p.charset, "utf-8", "headings: charset")
  eq(#p.lines, 7, "headings: six headings and a note")
  for i = 1, 6 do check(p.lines[i].runs[1].bold, "headings: h" .. i .. " is bold") end
  eq(find_run(p, "Second").color, 0x336699, "headings: h2 colour from <style>")
  eq(find_run(p, "Note text").color, 0xFF0000, "headings: class colour")
  eq(find_run(p, "Note text").x, 4 + 12, "headings: class margin-left")
  eq(#p.rects, 1, "headings: hr")

  p = P.paragraphs
  check(find_run(p, "bold").bold and find_run(p, "italic").italic and find_run(p, "underline").ul, "paragraphs: inline styles")
  local lt = linetexts(p)
  eq(lt[1], "This is the first paragraph. It has", "paragraphs: first line wraps at 38 columns (232 px)")
  local pre_at
  for i, t in ipairs(lt) do if t == "line one" then pre_at = i end end
  check(pre_at, "paragraphs: pre line kept")
  eq(table.concat(lt, "|", pre_at, pre_at + 3), "line one|  indented line two||after blank", "paragraphs: pre keeps indent and blank line")
  check(find_run(p, "Centered line").x > 4, "paragraphs: centered")
  check(#p.rects == 1 and p.rects[1].color == 0xeeeeee, "paragraphs: background box")
  check(find_run(p, "verylong") ~= nil, "paragraphs: the long word is placed")

  p = P.links
  local hrefs = {}
  for i, l in ipairs(p.links) do hrefs[i] = l.href end
  eq(table.concat(hrefs, " "), table.concat({
    "https://example.test/dir/page2.html", "https://example.test/up.html?x=1#top",
    "https://example.test/root/abs.html", "https://cdn.example.test/lib",
    "https://example.test/dir/base.html?q=tree", "https://example.test/dir/base.html#frag",
    "https://other.test:8443/a/c", "https://example.test/dir/long.html" }, " "), "links: resolved targets in reading order")
  eq(p.stats.refused, 6, "links: javascript, mailto, file, data, http and ftp are refused")
  eq(#p.links[8].boxes, 3, "links: the long anchor wraps over three lines")
  check(find_run(p, "anchor without href") and not find_run(p, "anchor without href").ul, "links: <a> without href is plain text")
  check(find_run(p, "script") ~= nil, "links: refused link text is still shown")

  p = P.lists
  local marks = {}
  for _, l in ipairs(p.lines) do
    local r = l.runs[1]
    if r.text == "*" or r.text:match("^%d+%.$") then marks[#marks + 1] = r.text end
  end
  eq(table.concat(marks, " "), "* * * 3. 4. * * 5. *", "lists: markers")
  check(find_run(p, "Definition one").x > find_run(p, "Term one").x, "lists: dd is indented")
  eq(p.links[1].href, "https://example.test/three", "lists: link in an item")

  p = P.images
  eq(#p.images, 16, "images: capped at 16")
  eq(p.images[1].w .. "x" .. p.images[1].h, "100x50", "images: sized")
  eq(p.images[4].src, "https://static.example.test/wide.webp", "images: absolute src")
  check(p.images[4].w <= 232, "images: wide image scaled")
  for _, im in ipairs(p.images) do check(not im.src:find("hidden") and not im.src:find("data:"), "images: no hidden or data image in the table") end
  eq(p.stats.refused, 2, "images: data: and javascript: refused")
  eq(p.images[5].src, "https://example.test/linked.png", "images: linked")
  check(find_run(p, "logo") and find_run(p, "logo").kind == "svg", "images: svg box with label")
  check(not find_run(p, "never shown"), "images: hidden image not laid out")
  eq(p.images[16].src, "https://example.test/i11.png", "images: the 16th is i11")

  p = P.tables
  check(#p.rects >= 9, "tables: border rects for the first table")
  local name, apple, water = find_run(p, "Name"), find_run(p, "Apple"), find_run(p, "Watermelon")
  check(apple.x == water.x and name.x > apple.x, "tables: first column lines up, the th is centered")
  check(find_run(p, "Qty").x == find_run(p, "12").x, "tables: second column lines up")
  check(find_run(p, "Prices") ~= nil, "tables: caption")
  check(find_run(p, "buy").link ~= nil, "tables: link in a cell")
  local sp, x = find_run(p, "Spans two"), find_run(p, "x")
  check(ly(p, "Spans two") == ly(p, "x") and x.x > sp.x, "tables: colspan row")
  eq(find_run(p, "short").x, find_run(p, "This cell").x, "tables: second table first column")
  check(ly(p, "short") > ly(p, "This cell"), "tables: second row below the first")

  p = P.entities
  eq(p.title, "Fish & Chips \u{A9}", "entities: title")
  local wide = load(assert(readfile("tests/web/fixtures/entities.html")), { width = 2000 })
  local lt2 = table.concat(linetexts(wide), "\n")
  check(lt2:find('<tag> & "quotes" \'apos\' \u{A9} \u{AE} \u{2122} \u{2026} \u{2014} \u{2013}', 1, true), "entities: named")
  check(lt2:find("ABCD \u{20AC} \u{1F600} bad: \u{FFFD} \u{FFFD} \u{FFFD}", 1, true), "entities: numeric and invalid")
  check(lt2:find("&nosuchthing; and bare &amp and ampersand & alone, AT&T and a&b;c.", 1, true), "entities: unknown stay as typed")
  check(lt2:find("No break space and \u{E9}\u{FC}\u{DF}.", 1, true), "entities: nbsp and latin letters")
  check(lt2:find("Double &lt; stays as &lt; text.", 1, true), "entities: decoded once")
  eq(p.links[1].href, "https://example.test/search?a=1&b=2", "entities: in an attribute")

  p = P.malformed
  local tr = load(assert(readfile("tests/web/fixtures/malformed.html")), { stream = false }).tree
  eq(tr.kids[1].tag, "body", "malformed: body is created")
  eq(tr.kids[1].attrs.class, "main", "malformed: bare attribute and upper case names")
  local alltext = text_of(tr)
  check(not alltext:find("not html", 1, true) and not alltext:find("needs js", 1, true)
    and not alltext:find("a comment", 1, true) and not alltext:find("unterminated", 1, true), "malformed: script, noscript, comments dropped")
  check(alltext:find("after comment", 1, true), "malformed: text after a closed comment")
  check(alltext:find("3 < 4 and a <3 heart", 1, true), "malformed: stray < kept as text")
  check(alltext:find("Last paragraph ", 1, true), "malformed: text before an unterminated comment")
  check(find_node(tr, function(n) return n.tag == "span" and n.kids[2] and n.kids[2].tag == "b" end), "malformed: custom element is a span")
  eq(find_run(p, "unquoted attr").color, 0x008000, "malformed: .x rule applies, @media ignored")
  check(p.stats.css_dropped == 1, "malformed: a:hover dropped")
  eq(p.links[1].href, "https://example.test/unquoted.html", "malformed: unquoted href")
  eq(p.links[2].href, "https://example.test/single.html", "malformed: single quoted href")
  eq(#p.images, 1, "malformed: image with unquoted attributes")
  check(find_run(p, "needs js") == nil, "malformed: noscript not shown")

  p = P.search_page
  eq(p.title, "Tree - Low poly models", "search: title")
  eq(p.stats.scripts, 2, "search: two scripts counted, not shown")
  check(not find_run(p, "SERVER_APP_STATE") and not find_run(p, "inside a string"), "search: script text is not shown")
  eq(#p.images, 3, "search: three card images (hidden placeholders skipped)")
  eq(p.images[2].alt, "Pine Tree", "search: card image alt")
  local want_cards = { "/m/aaaa1111", "/m/bbbb2222", "/m/cccc3333" }
  local count = {}
  for _, l in ipairs(p.links) do count[l.href] = (count[l.href] or 0) + 1 end
  for _, c in ipairs(want_cards) do eq(count["https://example.test" .. c], 2, "search: card link and title link for " .. c) end
  eq(count["https://example.test/u/Author-B"], 1, "search: author link")
  check(find_run(p, "800 tris") and find_run(p, "Results for") and find_run(p, "Pine Tree"), "search: texts")
  check(find_run(p, "site") and find_run(p, "site").kind == "svg", "search: svg box (label cut to the 24 px box)")
  check(p.controls[1].kind == "text" and p.controls[1].value == "Tree" and p.controls[2].kind == "submit", "search: input and button")
  eq(p.controls[1].name, nil, "search: input without a form has no name")
  check(#p.forms == 0, "search: no form (the cart builds /search/<text> itself)")
  eq(p.rects[1] and p.rects[1].kind, nil, "search: no stray rects")
end

---------------------------------------------------------------------------
print("-- streaming")
---------------------------------------------------------------------------
do
  local parts = { "<html><head><title>S</title></head><body><div id=root>" }
  for i = 1, 3000 do parts[#parts + 1] = "<div class=card><p>Item " .. i .. " <a href=/m/" .. i .. ">link</a></p></div>" end
  parts[#parts + 1] = "</div></body></html>"
  local src = table.concat(parts)
  local p1 = page.new({ url = BASE, width = 240 })
  p1:feed(src)
  local lay1 = p1:finish()
  eq(#p1.builder.body.kids, 0, "stream: the tree under <body> is dropped")
  local p2 = page.new({ url = BASE, width = 240, stream = false })
  p2:feed(src)
  local lay2 = p2:finish()
  check(#p2.builder.body.kids > 0, "tree mode keeps the tree")
  eq(dump.page(lay1), dump.page(lay2), "stream: 3000 cards give the same page in both modes")
  eq(#lay1.links, 3000, "stream: all links")
  eq(lay1.stats.nodes, lay2.stats.nodes, "stream: same node count")
  collectgarbage(); collectgarbage()
  local kb_stream = collectgarbage("count")
  local keep = lay2.tree
  lay1 = nil
  collectgarbage(); collectgarbage()
  print(string.format("   (info) live Lua KiB with the tree kept: %.0f; nodes: %d", collectgarbage("count"), lay2.stats.nodes))
  keep = nil
  -- A <style> that comes after some content applies to the later content only
  -- when streaming (the earlier lines are laid out already). Styles in <head>
  -- and before the content work the same in both modes.
  local late = '<a class=x href=/a>early</a><style>.x{color:red}</style><a class=x href=/b>late</a>'
  local ls, lt = load(late), load(late, { stream = false })
  eq(find_run(ls, "early").color, 0x0000EE, "stream: a late <style> does not restyle laid out lines")
  eq(find_run(ls, "late").color, 0xFF0000, "stream: a late <style> styles later content")
  eq(find_run(lt, "early").color, 0xFF0000, "tree: a late <style> styles everything")
  -- hidden container is not laid out and is dropped
  local hp = load('<div style="display:none"><p>hidden</p></div><div><p>shown</p></div>')
  eq(table.concat(linetexts(hp), "|"), "shown", "stream: display:none container")
end

print(string.format("%d checks, %d failed", checks, fails))
if fails > 0 then os.exit(1) end
