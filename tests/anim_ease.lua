-- The kernel's pose_at through animate() (clip modes 0..4, make test-anim-ease):
-- arm.R's head y is 1 + the eased u between the two keys of each clip.
local expect = {
  linear  = { 0, 0.25, 0.5, 1 },
  smooth  = { 0, 0.15625, 0.5, 1 },
  step    = { 0, 0, 0, 1 },
  easein  = { 0, 0.0625, 0.25, 1 },
  easeout = { 0, 0.4375, 0.75, 1 },
}
local times = { 0, 0.25, 0.5, 1 }
local pass, total = 0, 0
local function check(ok, what)
  total = total + 1
  if ok then pass = pass + 1 else log("anim-ease FAIL: " .. what) end
end
function _init()
  local m = model("figure")
  local c = clips(m)
  check(#c == 5, "five clips")
  for name, want in pairs(expect) do
    for i, t in ipairs(times) do
      animate(m, name, t)
      local _, y = bone3d(m, "arm.R")
      check(math.abs((y - 1) - want[i]) < 1e-4, string.format("%s at %.2f: %.5f, wanted %.5f", name, t, y - 1, want[i]))
    end
  end
  log(string.format("anim-ease: %d/%d checks passed", pass, total))
  quit()
end
function _draw() cls(0) end
