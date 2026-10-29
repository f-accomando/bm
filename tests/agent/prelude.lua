-- tests/agent: a fake model in place of the kernel's ai_ask, put before carts/agent/main.lua.
-- Each question is answered on the next poll. "file" asks for an answer with a file block.
local asks, pending = 0, nil

local ready = not FAKE_UNSET          -- FAKE_UNSET = true before this file: the wizard starts

function ai_info()
  if not ready then return { ready = false, why = "no AI server: run /setup in bm Agent (bm/ai.txt)" } end
  return { ready = true, host = "fake.test", model = "fake-1", key = true }
end

-- the kernel's question is answered yes; the fields are logged without the key's text
function ai_setup(t)
  log(string.format("fake: ai_setup url %s model %s key %s", tostring(t.url), tostring(t.model),
                    t.key and ("..." .. t.key:sub(-4)) or "nil"))
  ready = true
  return true
end

function ai_pair(addr, code)
  log("fake: ai_pair " .. addr .. " " .. code)
  pending_pair = true
  return 43
end

function ai_pair_finish(id)
  log("fake: ai_pair_finish " .. id)
  ready = true
  return true
end

function ai_ask(msgs, opts)
  if type(msgs) == "string" then msgs = { { role = "user", content = msgs } } end   -- as the kernel takes it
  opts = opts or {}
  asks = asks + 1
  local last = msgs[#msgs].content
  local raw_back = false                 -- the model's own file block came back in the history
  for _, m in ipairs(msgs) do
    if m.role == "assistant" and m.content:find("<<<file", 1, true) then raw_back = true end
  end
  log(string.format("fake: ask %d, %d messages, system %s, model %s, raw %s", asks, #msgs,
                    opts.system and "yes" or "no", tostring(opts.model), tostring(raw_back)))
  if last:find("single word OK") then
    pending = "OK"
  elseif last:find("file") then
    pending = "Here it is.\n<<<file HELLO.LUA\nfunction _draw()\n  cls()\n  print(\"hi\", 8, 8)\nend\n>>>\nSave it?"
  else
    pending = "echo " .. last
  end
  return 42
end

function web_poll(id)
  if id == 43 then return pending_pair and "done" or "busy" end
  return id == 42 and pending and "done" or "busy"
end
function ai_reply(id) return pending, { finish = "stop", completion_tokens = 5 } end
function web_cancel(id)
  if id == 43 then pending_pair = nil else pending = nil end
  return true
end
