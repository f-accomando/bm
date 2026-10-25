-- file_write of a model (docs/api/system.md): a name that ends in .bmm goes to /bmm/NAME.bmm after the
-- kernel's own check of the bytes (bmhost: the host folder is the card). The files come from /docs
-- (tests/web/run_bmm_test.py copies them there): the committed models of web/models, and broken ones.
-- The Python driver checks the folders after the run (what is on the card, and what is not).
local passed, total, frame = 0, 0, 0

local function check(ok, what)
  total = total + 1
  if ok then passed = passed + 1 else log("web_bmm: FAIL " .. what) end
end

local function doc(name)
  local b, why = doc_read(name)
  check(b ~= nil, "doc_read " .. name .. ": " .. tostring(why))
  return b or ""
end

local function run()
  -- 1. the four committed models go to /bmm and read back as a model
  for _, n in ipairs({ "house", "tree", "crate", "walker" }) do
    local ok, path = file_write(n .. ".bmm", doc(n .. ".bmm"))
    check(ok == true and path == "bmm/" .. n .. ".bmm", n .. " saved: " .. tostring(path))
    local r, why = res_read("/bmm/" .. n .. ".bmm")
    check(r ~= nil and r.kind == 1 and r.sections[8] ~= nil, n .. " reads back as a model: " .. tostring(why))
  end
  local r = res_read("/bmm/house.bmm")
  check(r and r.name == "House" and r.author == "bm tests", "the name and author of house")

  -- 2. a model of that name is there: it is not replaced
  local ok, why = file_write("house.bmm", doc("tree.bmm"))
  check(ok == nil and tostring(why):find("there already", 1, true), "no silent replace: " .. tostring(why))
  check(res_read("/bmm/house.bmm").name == "House", "house is unchanged")

  -- 3. broken bytes with a .bmm name: refused, nothing written
  for _, n in ipairs({ "trunc", "junk", "flip", "empty", "image" }) do
    ok, why = file_write(n .. ".bmm", doc(n .. ".dat"))
    check(ok == nil and why ~= nil, n .. " refused: " .. tostring(why))
    check(res_read("/bmm/" .. n .. ".bmm") == nil, n .. " is not in /bmm")
  end
  ok, why = file_write("cart.bmm", doc("cart.dat"))
  check(ok == nil, "a cartridge named .bmm is refused: " .. tostring(why))

  -- 4. a name of the model rules: upper case is written lower case, a folder name is refused
  ok, why = file_write("UP.BMM", doc("tree.bmm"))
  check(ok == true and why == "bmm/up.bmm", "upper case name: " .. tostring(why))
  ok, why = file_write("bm/web/dir.bmm", doc("tree.bmm"))
  check(ok == true and why == "bmm/dir.bmm", "bm/web/ prefix: " .. tostring(why))
  for _, n in ipairs({ "toolongname.bmm", "a.b.bmm", "../x.bmm", "sub/x.bmm", "/bmm/x.bmm", "x y.bmm", ".bmm", "x.bmmm" }) do
    ok, why = file_write(n, doc("tree.bmm"))
    check(ok == nil, "name refused: " .. n .. " " .. tostring(why))
  end

  -- 5. the other kinds of file keep their rules
  for _, n in ipairs({ "x.bm", "x.bme", "x.b16", "X.BM" }) do
    ok, why = file_write(n, doc("tree.bmm"))
    check(ok == nil, n .. " refused: " .. tostring(why))
  end
  ok, why = file_write("model.glb", "glTF....")
  check(ok == true and why == "bm/web/model.glb", "another file goes to bm/web: " .. tostring(why))
  -- async: a model is written at once, the other kinds queue as before
  ok, why = file_write("note.txt", "hello", true)
  check(ok == true and why == "bm/web/note.txt", "async text: " .. tostring(why))
end

function _update()
  frame = frame + 1
end

function _draw()
  cls(0x102030)
  if frame == 2 then
    run()
    log(string.format("web_bmm: %d/%d checks passed", passed, total))
  end
end
