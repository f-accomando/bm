-- bmweb: text dumps of a tree and of a laid out page. The tests compare them
-- with the files in tests/web/expected/. They are also the "draw" of stage 1.
local M = {}

local function esc(s, max)
  s = s:gsub("\\", "\\\\"):gsub("\n", "\\n"):gsub("\t", "\\t"):gsub('"', '\\"')
  if max and #s > max then s = s:sub(1, max) .. "..." end
  return s
end

-- One line per node, two spaces of indent per level.
function M.tree(root)
  local out = {}
  local function walk(n, depth)
    local pad = string.rep("  ", depth)
    if n.tag == "#text" then
      out[#out + 1] = pad .. '"' .. esc(n.text, 60) .. '"'
      return
    end
    local keys = {}
    for k in pairs(n.attrs or {}) do keys[#keys + 1] = k end
    table.sort(keys)
    local parts = { n.tag }
    for _, k in ipairs(keys) do parts[#parts + 1] = k .. '="' .. esc(n.attrs[k], 40) .. '"' end
    out[#out + 1] = pad .. table.concat(parts, " ")
    for _, k in ipairs(n.kids) do walk(k, depth + 1) end
  end
  walk(root, 0)
  return table.concat(out, "\n") .. "\n"
end

local function box(b)
  if not b then return "-" end
  return string.format("%d,%d,%d,%d", b.x, b.y, b.w, b.h)
end

-- Lines with their runs, then the link, image, control and form tables.
function M.page(page)
  local out = {}
  local function add(fmt, ...) out[#out + 1] = string.format(fmt, ...) end
  add("page w=%d h=%d lines=%d links=%d images=%d controls=%d forms=%d rects=%d cut=%s title=%s",
    page.w, page.h, #page.lines, #page.links, #page.images, #page.controls, #page.forms,
    #page.rects, tostring(page.cut and page.cut_reason or false), page.title and ('"' .. esc(page.title) .. '"') or "-")
  for _, l in ipairs(page.lines) do
    add("line y=%d h=%d", l.y, l.h)
    for _, r in ipairs(l.runs) do
      local f = {}
      if r.bold then f[#f + 1] = "b" end
      if r.italic then f[#f + 1] = "i" end
      if r.ul then f[#f + 1] = "u" end
      if r.color and r.color ~= 0 then f[#f + 1] = string.format("c=%06x", r.color) end
      if r.link then f[#f + 1] = "link=" .. r.link end
      if r.img then f[#f + 1] = "img=" .. r.img end
      if r.ctl then f[#f + 1] = "ctl=" .. r.ctl end
      add("  %s x=%d y=%d w=%d h=%d \"%s\" %s", r.kind or "t", r.x, l.y + l.h - r.h, r.w, r.h,
        esc(r.text or "", 80), table.concat(f, " "))
    end
  end
  for _, r in ipairs(page.rects) do
    add("rect %s %d,%d,%d,%d c=%06x", r.kind, r.x, r.y, r.w, r.h, r.color)
  end
  for i, l in ipairs(page.links) do
    local bs = {}
    for _, b in ipairs(l.boxes) do bs[#bs + 1] = box(b) end
    add("link %d %s \"%s\" boxes=%s", i, l.href, esc(l.text, 40), table.concat(bs, " "))
  end
  for i, im in ipairs(page.images) do
    add("image %d %s alt=\"%s\" %dx%d box=%s", i, im.src, esc(im.alt, 40), im.w, im.h, box(im.box))
  end
  for i, c in ipairs(page.controls) do
    add("control %d %s name=%s value=\"%s\" form=%s box=%s", i, c.kind, tostring(c.name), esc(c.value or "", 40),
      tostring(c.form), box(c.box))
  end
  for i, f in ipairs(page.forms) do
    add("form %d %s %s", i, f.method, tostring(f.action))
  end
  return table.concat(out, "\n") .. "\n"
end

return M
