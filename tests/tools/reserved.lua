-- A game (not a tool) must not create a file the kernel would run as one of bm's tools
-- (src/bm/runtime.c reserved_tool_name); other names are fine. tests/tools/check_reserved.py reads the log.
local names = { "pixel.bm", "SDK.BM", "assistan.bm", "code.bm", "Studio.bm", "sdk_b16.bm", "ok.bm", "pixels.bm" }
local done = false
function _draw()
  if done then return end
  done = true
  for _, n in ipairs(names) do
    local ok, msg = cart_save("/bm/" .. n, { title = "x", lua = "function _draw() end" })
    log("reserved: " .. n .. " " .. tostring(ok) .. " " .. tostring(msg))
  end
  log("reserved: end")
end
