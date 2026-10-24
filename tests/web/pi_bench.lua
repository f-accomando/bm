-- Web bench: the numbers for multitasking stages 3-5 on the Pi Zero W (and the RGB30).
-- Not a game. Build and run it as a cartridge (see docs/progress/multitasking.md):
--   python scripts/mkbm.py -o build/webbench.bm --lua tests/web/pi_bench.lua --title "Web bench" --author bm --res 640x360
-- Copy build/webbench.bm to bm/ on the SD card by hand. Press A to start. The result is on the screen and in
-- /docs/WEBBENCH.TXT (doc_write). It writes bm/web/BENCH1.BIN, BENCH2.BIN and, with a URL, BENCH.BIN / BENCH.PRT.
--
-- 1. sync:   file_write of SIZE bytes: the call time in ms is one frame stopped.
-- 2. async:  the same with async = true: the worst frame while it is written, and the time to done.
-- 3. save:   a save() while an async write is half way: the write must pause and finish (state done).
-- 4. resume: needs URL (an https address of a file of at least 2 MB that the server serves with Range):
--            web_get with resume, cancelled after 1 MB, then asked again; prints "resumed" and the final size.
local SIZE = 2 * 1024 * 1024
local URL = ""   -- put an https address of a big file here to run test 4 (leave empty to skip)

local lines = {}
local function say(s)
  lines[#lines + 1] = s
  log("webbench: " .. s)
end

local state, t0, worst, step = "idle", 0, 0, 0
local last, id, data

local function frame_dt()
  local now = time()
  local dt = (now - last) * 1000
  last = now
  return dt
end

function _init()
  last = time()
  data = string.rep("0123456789abcdef", SIZE // 16)
end

local function finish()
  doc_write("WEBBENCH.TXT", table.concat(lines, "\n") .. "\n")
  state = "done"
end

function _update()
  local dt = frame_dt()
  if state == "idle" then
    if btnp(4) then state = "sync" end
  elseif state == "sync" then
    local a = time()
    local ok, why = file_write("BENCH1.BIN", data)
    say(string.format("sync   %d KB: the call took %.0f ms (%s)", SIZE // 1024, (time() - a) * 1000, tostring(ok or why)))
    state, t0, worst = "async", time(), 0
    local ok2, why2 = file_write("BENCH2.BIN", data, true)
    say("async  queued: " .. tostring(ok2 or why2))
  elseif state == "async" then
    worst = math.max(worst, dt)
    local st, info = file_state("BENCH2.BIN")
    if st == "done" or st == "error" then
      say(string.format("async  %s after %.0f ms, worst frame %.1f ms (%s)", st, (time() - t0) * 1000, worst, tostring(info)))
      state, step = "save", 0
    end
  elseif state == "save" then
    if step == 0 then
      file_write("BENCH2.BIN", data, true)
      step = 1
    elseif step < 6 then
      file_state("BENCH2.BIN")
      step = step + 1
    elseif step == 6 then
      local ok = save({ bench = 1 })
      local st = file_state("BENCH2.BIN")
      say(string.format("save   during a queued write: save %s, write is %s", tostring(ok), tostring(st)))
      step = 7
    else
      local st, info = file_state("BENCH2.BIN")
      if st == "done" or st == "error" then
        say(string.format("save   the queued write ended: %s (%s)", st, tostring(info)))
        state = URL ~= "" and "resume1" or "end"
      end
    end
  elseif state == "resume1" then
    if not id then
      id = web_get(URL, { resume = "BENCH.BIN", max = 4 * 1024 * 1024 })
      if not id then say("resume could not start"); state = "end"; return end
      t0 = time()
    end
    local st, size = web_poll(id)
    if st == "error" or st == "done" or (size or 0) >= 1024 * 1024 then
      local info = web_info(id)
      say(string.format("resume 1: %s, %d bytes, resumed %s, %.0f s", tostring(info.state), info.size, tostring(info.resumed), time() - t0))
      web_cancel(id)
      id, step, state = nil, 0, "resume2"
    end
  elseif state == "resume2" then
    step = step + 1
    file_state("BENCH.PRT")
    if step > 120 then
      id = web_get(URL, { resume = "BENCH.BIN", max = 4 * 1024 * 1024 })
      t0, state = time(), "resume3"
    end
  elseif state == "resume3" then
    local st = web_poll(id)
    if st == "error" or st == "done" then
      local info = web_info(id)
      say(string.format("resume 2: %s, %d bytes, resumed %s, status %s, %.0f s", tostring(info.state), info.size, tostring(info.resumed),
        tostring(info.status), time() - t0))
      web_cancel(id)
      state = "end"
    end
  elseif state == "end" then
    finish()
  end
end

function _draw()
  cls(0x101020)
  print("Web bench (A = start)  " .. state, 4, 4, 0xFFFFFF)
  for i, l in ipairs(lines) do print(l, 4, 24 + i * 18, 0xC0C0C0, 1) end
end
