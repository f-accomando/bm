-- Vm: the cart running. Its code runs in a coroutine that yields at every
-- flip() (the end of a frame); the 60 Hz loop of the console resumes it
-- every frame for _update60 carts, every other frame for _update ones.

Vm.state = nil            -- "run", "error", "done", or nil
Vm.fps, Vm.t, Vm.cpu = 30, 0, 0
Vm.menu = {}

local FLIP = Env.FLIP

local function short(path)
  return path:match("([^/]+)$") or path
end

local function base_name(path)
  return (short(path):gsub("%.[pP]8%.[pP][nN][gG]$", ""):gsub("%.[pP]8$", ""))
end
Vm.base_name = base_name

function Vm.set_fps(f)
  f = f == 60 and 60 or 30
  if f ~= Vm.fps then
    Vm.fps = f
    n8.setfps(f)
  end
end

local function quiet()
  n8.sfx(-1)
  n8.music(-1)
end

function Vm.fail(title, msg)
  quiet()
  Vm.state = "error"
  Vm.err = { title = title, msg = tostring(msg or "") }
  log("nano8: " .. title .. ": " .. Vm.err.msg)
  return false
end

-- start(path, [param]) -> true, or false (Vm.err says why)
function Vm.start(path, param)
  quiet()
  Vm.state = nil
  local info, err = n8.load(path)
  if not info then return Vm.fail("Cannot load " .. short(path), err) end
  local code, terr = Xl.translate(info.code)
  info.code = nil
  if not code then return Vm.fail("The code cannot be read", terr) end
  local G = Env.make()
  local name = base_name(path)
  local fn, cerr = n8.compile(code, "=" .. name, G)
  code = nil
  if not fn then return Vm.fail("Syntax error", cerr) end
  n8.power()
  Vm.path, Vm.info, Vm.G, Vm.name, Vm.param = path, info, G, name, param
  Vm.fps, Vm.t, Vm.frames, Vm.ticks, Vm.cpu = 30, 0, 0, 0, 0
  n8.setfps(30)
  Vm.cartid, Vm.saved_data, Vm.request, Vm.want_pause = nil, nil, nil, false
  Vm.menu = {}
  Vm.pause_held = true                -- the button that started it is not a pause
  Vm.co = coroutine.create(function()
    local ok, e = xpcall(function()
      fn()
      if G._init then G._init() end
      while true do
        local u60, u = G._update60, G._update
        if u60 then
          Vm.set_fps(60)
          u60()
        elseif u then
          Vm.set_fps(30)
          u()
        end
        if G._draw then G._draw() end
        coroutine.yield(FLIP)
      end
    end, n8.traceback)
    if not ok then error(e, 0) end
  end)
  Vm.state = "run"
  collectgarbage()
  log("nano8: playing " .. path)
  return true
end

function Vm.menuitem(i, label, fn)
  i = floor(tonumber(i) or 0) & 0xff
  if i < 1 or i > 5 then return end
  if label == nil then
    Vm.menu[i] = nil
  else
    Vm.menu[i] = { label = n8.tostr(label), fn = fn }
  end
end

function Vm.extcmd(cmd)
  if cmd == "reset" then Vm.request = { what = "run" }
  elseif cmd == "pause" then Vm.want_pause = true
  elseif cmd == "shutdown" then Vm.request = { what = "stop" }
  end
end

-- a cart asked for another (load): relative to its own folder
local function resolve(path)
  if type(path) ~= "string" or path == "" or path:sub(1, 1) == "#" then return nil end
  local dir = Vm.path:match("^(.*)/[^/]*$") or ""
  local p = path:sub(1, 1) == "/" and path or dir .. "/" .. path
  local tries = { p, p .. ".p8", p .. ".p8.png" }
  for _, t in ipairs(tries) do
    local d, n = t:match("^(.*)/([^/]*)$")
    for _, e in ipairs(ls(d == "" and "/" or d)) do
      if e.name:lower() == n:lower() then return (d == "" and "" or d) .. "/" .. e.name end
    end
  end
end

local function handle_request()
  local r = Vm.request
  Vm.request = nil
  if r.what == "run" then
    Vm.start(Vm.path, Vm.param)
  elseif r.what == "stop" then
    quiet()
    Vm.state = "done"
    Vm.stop_msg = r.msg and n8.tostr(r.msg) or nil
  elseif r.what == "load" then
    local p = resolve(r.path)
    if not p then return Vm.fail("Cannot load " .. tostring(r.path), "the cart asked for a file that is not on the SD card") end
    Vm.start(p, r.param and n8.tostr(r.param))
  end
end

-- the persistent data goes to the SD card when it changes
local function keep_data()
  if not Vm.cartid then return end
  local d = n8.peekstr(0x5e00, 256)
  if d ~= Vm.saved_data then
    Vm.saved_data = d
    Cfg.set_cartdata(Vm.cartid, d)
  end
end

function Vm.save_now()
  keep_data()
  Cfg.flush(true)
end

-- tick() at 60 Hz -> "pause" when the pause button was pressed
function Vm.tick()
  if Vm.state ~= "run" then return end
  Vm.ticks = Vm.ticks + 1
  if Vm.fps == 30 and Vm.ticks % 2 == 1 then return end
  local b = In.read()
  local p = false
  for i = 1, 8 do
    if b[i] & 64 ~= 0 then p = true end
  end
  if p and not Vm.pause_held then
    Vm.pause_held = true
    if n8.peek(0x5f30) == 1 then
      n8.poke(0x5f30, 0)
    else
      return "pause"
    end
  elseif not p then
    Vm.pause_held = false
  end
  n8.buttons(b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8])
  local t0 = time()
  local ok, e = coroutine.resume(Vm.co)
  Vm.cpu = (time() - t0) * Vm.fps
  if not ok then
    return Vm.fail("Runtime error", e)
  end
  if coroutine.status(Vm.co) == "dead" then
    Vm.state = "done"
  end
  Vm.frames = Vm.frames + 1
  Vm.t = Vm.t + 1 / Vm.fps
  if Vm.request then handle_request() end
  if Vm.frames % 30 == 0 then
    keep_data()
    Cfg.flush()
  end
  if Vm.want_pause then
    Vm.want_pause = false
    return "pause"
  end
end

-- the pause menu: the cart's own items run here
function Vm.call_item(i, buttons)
  local it = Vm.menu[i]
  if not it or type(it.fn) ~= "function" then return false end
  local co = coroutine.create(it.fn)
  local ok, keep = coroutine.resume(co, buttons)
  if not ok then
    Vm.fail("Runtime error", keep)
    return false
  end
  return keep == true
end

function Vm.close()
  if Vm.state then Vm.save_now() end
  quiet()
  Vm.state = nil
  Vm.co, Vm.G = nil, nil
  collectgarbage()
end
