-- tests/agent: a fake model in place of the kernel's ai_ask, put before carts/agent/main.lua.
-- Each question is answered on the next poll. "file" asks for an answer with a file block.
local asks, pending = 0, nil

function ai_info() return { ready = true, host = "fake.test", model = "fake-1", key = true } end

function ai_ask(msgs, opts)
  asks = asks + 1
  local last = msgs[#msgs].content
  local raw_back = false                 -- the model's own file block came back in the history
  for _, m in ipairs(msgs) do
    if m.role == "assistant" and m.content:find("<<<file", 1, true) then raw_back = true end
  end
  log(string.format("fake: ask %d, %d messages, system %s, model %s, raw %s", asks, #msgs,
                    opts.system and "yes" or "no", tostring(opts.model), tostring(raw_back)))
  if last:find("file") then
    pending = "Here it is.\n<<<file HELLO.LUA\nfunction _draw()\n  cls()\n  print(\"hi\", 8, 8)\nend\n>>>\nSave it?"
  else
    pending = "echo " .. last
  end
  return 42
end

function web_poll(id) return id == 42 and pending and "done" or "busy" end
function ai_reply(id) return pending, { finish = "stop", completion_tokens = 5 } end
function web_cancel(id) pending = nil return true end
