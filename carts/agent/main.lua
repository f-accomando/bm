-- bm Agent: a chat with a cloud model through ai_ask (docs/api/system.md, "A chat model").
-- The player sets the server with /setup (kept by the kernel in bm/ai.txt, not bm/config.txt): a free API
-- (Gemini, Groq, OpenRouter, Mistral) or tools/bm_agent_relay.py on their own PC, which runs
-- Claude Code, Codex or Gemini CLI under their own subscription. The cartridge never sees the key.
-- The model may propose files; each one is written into bm/web only after the player says yes.
-- /setup (and the first start) asks for the provider and the key, or pairs with the relay by its code;
-- the kernel asks the player before it saves anything (ai_setup, ai_pair_finish).

local pt = require "padtype"

local W, H = SCREEN_W, SCREEN_H
local C_BG, C_BAR, C_PANEL = 0x12141C, 0x262C40, 0x1C2030
local C_TEXT, C_DIM, C_YOU, C_AI, C_NOTE, C_WARN = 0xE0E4F0, 0x8088A0, 0x9FD0FF, 0xE0E4F0, 0xFFC050, 0xFF7060

local SYSTEM = [[You are the assistant of bm, a bare-metal game console (a Raspberry Pi Zero W or a PowKiddy RGB30).
Games and apps for bm are written in Lua 5.4 with bm's API (_init, _update, _draw; cls, print, rectfill, spr, btn, btnp...).
Answer in short plain text: the screen shows about 70 characters a line. No Markdown tables.
To give the player a file, write it as a block on its own lines:
<<<file NAME.EXT
the content
>>>
NAME.EXT is an 8.3 name (1 to 8 of a-z 0-9 _ -, a dot, 1 to 3 letters), never .bm or .bme. The player
decides whether each file is saved (into the folder bm/web on the SD card).]]

local KEEP_MSGS, KEEP_BYTES = 24, 48 * 1024     -- the history sent with each question

local chat = {}          -- { role = "user" / "assistant" / "note", text = }
local input = ""
local req, req_t         -- the running request and when it started
local proposals = {}     -- files waiting for the player's yes or no: { name =, data = }
local writing            -- the name of a file being written (file_state)
local scroll = 0         -- lines up from the bottom
local model              -- /model NAME for this session
local cw, ch = 8, 16
local setup              -- the wizard: { step =, p = the preset, key =, url =, addr = }
local pair_id            -- the relay's CA on its way (ai_pair)
local testing            -- the request is the wizard's test, not a question of the chat

-- the providers with an OpenAI-compatible endpoint and a free tier (docs/design/ai-agent.md)
local PRESETS = {
  { name = "Groq", url = "https://api.groq.com/openai/v1", model = "openai/gpt-oss-20b",
    where = "console.groq.com > API Keys" },
  { name = "Google Gemini", url = "https://generativelanguage.googleapis.com/v1beta/openai", model = "gemini-2.5-flash",
    where = "aistudio.google.com > Get API key" },
  { name = "OpenRouter", url = "https://openrouter.ai/api/v1", model = "openai/gpt-oss-20b:free",
    where = "openrouter.ai > Keys (models ending in :free)" },
  { name = "Mistral", url = "https://api.mistral.ai/v1", model = "mistral-small-latest",
    where = "console.mistral.ai > API Keys" },
  { name = "Relay on my PC (Claude, ChatGPT, Gemini subscription)", relay = true },
  { name = "Another OpenAI-compatible server", other = true },
}

local function note(text) chat[#chat + 1] = { role = "note", text = text } end

-- ---- the history and the request ----

local function history()
  local msgs, bytes = {}, 0
  for i = #chat, 1, -1 do
    local m = chat[i]
    if m.role ~= "note" then
      bytes = bytes + #m.text
      if #msgs >= KEEP_MSGS or (bytes > KEEP_BYTES and #msgs > 0) then break end
      table.insert(msgs, 1, { role = m.role, content = m.raw or m.text })
    end
  end
  return msgs
end

local function ask()
  local id, why = ai_ask(history(), { system = SYSTEM, model = model, max_tokens = 2048, timeout = 120000 })
  if not id then note("not sent: " .. why) return end
  req, req_t = id, time()
end

-- ---- the setup wizard ----

local function prompt_step()
  local st = setup.step
  if st == "provider" then
    note("Setup. Which AI? Type the number and press enter (esc cancels):")
    for i, p in ipairs(PRESETS) do note("  " .. i .. "  " .. p.name) end
  elseif st == "url" then note("The server's address (https://.../v1):")
  elseif st == "key" then note("Your key from " .. (setup.p.where or "the provider") .. ". Type it and press enter:")
  elseif st == "model" then
    note("The model" .. (setup.p.model and (" (enter keeps " .. setup.p.model .. ")") or "") .. ":")
  elseif st == "addr" then
    note("Start on your PC: python3 tools/bm_agent_relay.py serve --ip <its address>")
    note("The PC's address, as the relay shows it (192.168.1.20):")
  elseif st == "code" then note("The pairing code the relay shows (XXXX-XXXX-XXXX-XXXX-XXXX):")
  end
end

local function start_setup()
  setup = { step = "provider" }
  input = ""
  prompt_step()
end

local function test_setup()
  local id, why = ai_ask("Reply with the single word OK.", { max_tokens = 64, timeout = 120000 })
  if not id then note("saved, but the test was not sent: " .. why) return end
  req, req_t, testing = id, time(), true
  note("saved. Testing the connection...")
end

local function setup_answer(line)
  local st, p = setup.step, setup.p
  if st == "provider" then
    p = PRESETS[tonumber(line) or 0]
    if not p then note("type a number from 1 to " .. #PRESETS) return end
    setup.p = p
    setup.step = p.relay and "addr" or p.other and "url" or "key"
  elseif st == "url" then
    if not line:match("^https://") then note("the address starts with https://") return end
    setup.url = line
    setup.step = "key"
  elseif st == "key" then
    if line == "" or line:find("%s") then note("the key has no spaces") return end
    setup.key = line
    setup.step = "model"
  elseif st == "model" then
    local m = line ~= "" and line or p.model
    if not m then note("type the model's name") return end
    local key, url = setup.key, p.url or setup.url
    setup = nil
    local ok, why = ai_setup({ url = url, model = m, key = key })
    if ok then model = nil test_setup() else note("not saved: " .. why .. ". /setup to try again") end
    return
  elseif st == "addr" then
    if not line:match("^%d+%.%d+%.%d+%.%d+") then note("four numbers, as 192.168.1.20") return end
    setup.addr = line
    setup.step = "code"
  elseif st == "code" then
    local id, why = ai_pair(setup.addr, line)
    if not id then note("not paired: " .. why) return end
    pair_id = id
    setup = nil
    note("asking the relay for its certificate...")
    return
  end
  prompt_step()
end

-- the file blocks of an answer: kept as proposals, shown in the chat as one line each
local function take_files(text)
  local shown = text:gsub("<<<file%s+([%w_%-]+%.%w+)[ \t]*\n(.-)\n>>>", function(name, data)
    proposals[#proposals + 1] = { name = name, data = data .. "\n" }
    return "[file " .. name .. ": " .. #data + 1 .. " bytes]"
  end)
  return shown
end

local function finish()
  local text, info = ai_reply(req)
  local took = time() - req_t
  web_cancel(req)
  req = nil
  if testing then
    testing = nil
    if text then note("it works: the model answered \"" .. text:sub(1, 40) .. "\". Ask your question.")
    else note("the test failed: " .. info .. ". /setup to try again") end
    return
  end
  if not text then note("no answer: " .. info) return end
  chat[#chat + 1] = { role = "assistant", text = take_files(text) }
  chat[#chat].raw = text                  -- the model gets back what it wrote, file blocks too
  local tok = info.completion_tokens and (", " .. info.completion_tokens .. " tokens") or ""
  if info.finish == "length" then note("the answer was cut: it reached max_tokens") end
  log(string.format("agent: answer in %.1f s%s", took, tok))
end

-- ---- commands ----

local function transcript()
  local out = {}
  for _, m in ipairs(chat) do out[#out + 1] = "[" .. m.role .. "]\n" .. (m.raw or m.text) .. "\n" end
  return table.concat(out, "\n")
end

local function command(line)
  local cmd, arg = line:match("^/(%S+)%s*(.*)$")
  if cmd == "new" then
    chat, proposals, scroll = {}, {}, 0
    note("a new conversation")
  elseif cmd == "save" then
    local ok, why = file_write("chat.txt", transcript())
    note(ok and ("saved " .. why) or ("not saved: " .. why))
  elseif cmd == "setup" then
    start_setup()
  elseif cmd == "model" then
    model = arg ~= "" and arg or nil
    note("model: " .. (model or ((ai_info().model or "?") .. " (bm/config.txt)")))
  else
    note("commands: /setup  /new  /save (bm/web/chat.txt)  /model NAME")
  end
end

local function send()
  local line = (input:gsub("^%s+", ""):gsub("%s+$", ""))
  input = ""
  scroll = 0
  if req or pair_id then return end
  if setup then return setup_answer(line) end
  if line == "" then return end
  if line:sub(1, 1) == "/" then return command(line) end
  chat[#chat + 1] = { role = "user", text = line }
  ask()
end

-- the input line as padtype's host: the pad's newline sends
local host = {
  lang = "en",
  prose = true,
  before = function() return input end,
  insert = function(s) input = input .. s end,
  erase = function(n)
    for _ = 1, n do
      local cut = utf8.offset(input, -1)
      if not cut then break end
      input = input:sub(1, cut - 1)
    end
  end,
  newline = function() send() end,
  move = function() end,
}

-- ---- the frame ----

function _init()
  cw, ch = font("8x16")
  pt.set({ delay = 0.25, fallback = "keyboard" })    -- on with the first pad button, off with a key
  keyhelp({ { "enter", "send" }, { "esc", "stop the request / say no to a file" },
            { "pgup pgdn", "scroll" }, { "y n", "save or skip a proposed file" } }, "bm Agent")
  local i = ai_info()
  if i.ready then
    note("server " .. i.host .. ", model " .. i.model .. (i.key and "" or " (no ai_key)") .. ". /help for commands.")
  else
    note("bm Agent is not set up yet.")
    start_setup()
  end
end

local function answer_file(yes)
  local p = table.remove(proposals, 1)
  if not yes then note("skipped " .. p.name) return end
  local ok, why = file_write(p.name, p.data, true)
  if ok then writing = p.name else note("not saved: " .. why) end
end

function _update()
  if writing then
    local st, info = file_state(writing)
    if st == "done" then note("saved bm/web/" .. writing) writing = nil
    elseif st == "error" or not st then note("not saved: " .. tostring(info)) writing = nil end
  end
  if req then
    local st = web_poll(req)
    if st == "done" or st == "error" then finish() end
  end
  if pair_id then
    local st = web_poll(pair_id)
    if st == "done" or st == "error" then
      local ok, why = ai_pair_finish(pair_id)
      web_cancel(pair_id)
      pair_id = nil
      if ok then model = nil test_setup() else note("not paired: " .. why .. ". /setup to try again") end
    end
  end
  local k = keyp()
  if k and pt.is_on() then pt.on(nil) end
  if not k and not pt.is_on() and pad() ~= 0 and lastinput() ~= "keyboard" and not proposals[1] then
    pt.on("compose")
  end
  while k do
    if proposals[1] and not writing then
      if k == "y" or k == "\n" then answer_file(true)
      elseif k == "n" or k == "esc" then answer_file(false) end
    elseif k == "esc" and (req or pair_id) then
      web_cancel(req or pair_id)
      req, pair_id, testing = nil, nil, nil
      note("stopped")
    elseif k == "esc" and setup then
      setup = nil
      input = ""
      note("setup cancelled: /setup starts it again")
    elseif k == "\n" then send()
    elseif k == "\b" then host.erase(1)
    elseif k == "pgup" then scroll = scroll + 5
    elseif k == "pgdn" then scroll = math.max(0, scroll - 5)
    elseif #k == 1 or (#k > 1 and k:byte(1) >= 0x80) then input = input .. k
    end
    k = keyp()
  end
  if proposals[1] and not writing then
    if btnp("ok") then answer_file(true) elseif btnp("back") then answer_file(false) end
  elseif not pt.update(host) then
    if btnp("up") then scroll = scroll + 1 end
    if btnp("down") then scroll = math.max(0, scroll - 1) end
    if btnp("ok") then send() end
  end
end

-- the lines of a text at cols characters, cut at spaces
local function wrap(text, cols, out, col)
  for para in (text .. "\n"):gmatch("(.-)\n") do
    local line = ""
    for word in para:gmatch("%S+%s*") do
      if utf8.len(line .. word) and utf8.len(line .. word) > cols and line ~= "" then
        out[#out + 1] = { line, col }
        line = ""
      end
      while utf8.len(word) and utf8.len(word) > cols do
        out[#out + 1] = { word:sub(1, utf8.offset(word, cols + 1) - 1), col }
        word = word:sub(utf8.offset(word, cols + 1))
      end
      line = line .. word
    end
    out[#out + 1] = { line, col }
  end
end

local COLS = { user = C_YOU, assistant = C_AI, note = C_NOTE }

function _draw()
  cls(C_BG)
  local cols = (W - 16) // cw
  local pw, ph = pt.size()
  local pad_h = pt.is_on() and ph + 8 or 0
  local top, bottom = ch + 6, H - 2 * ch - 10 - pad_h
  local lines = {}
  for _, m in ipairs(chat) do
    wrap((m.role == "user" and "> " or "") .. m.text, cols, lines, COLS[m.role])
    lines[#lines + 1] = { "", C_DIM }
  end
  local rows = (bottom - top) // ch
  scroll = math.min(scroll, math.max(0, #lines - rows))
  local first = math.max(1, #lines - rows - scroll + 1)
  for i = first, math.min(#lines, first + rows - 1) do
    print(lines[i][1], 8, top + (i - first) * ch, lines[i][2])
  end
  rectfill(0, 0, W, ch + 2, C_BAR)
  print("bm Agent", 8, 1, C_TEXT)
  local i = ai_info()
  local right = i.ready and ((model or i.model) .. " @ " .. i.host) or "not set up"
  print(right, W - 8 - utf8.len(right) * cw, 1, C_DIM)
  local y = H - 2 * ch - 6 - pad_h
  rectfill(0, y - 2, W, 2 * ch + 8, C_PANEL)
  if proposals[1] then
    local p = proposals[1]
    print("save " .. p.name .. " (" .. #p.data .. " bytes) into bm/web?  y / n", 8, y + 2, C_NOTE)
    print(p.data:match("^[^\n]*"):sub(1, cols), 8, y + 2 + ch, C_DIM)
  elseif req or pair_id then
    local dots = string.rep(".", math.floor(time() * 3) % 4)
    local what = pair_id and "pairing" or testing and "testing" or "thinking"
    print(string.format("%s%s %d s  (esc stops)", what, dots, math.floor(time() - (req_t or time()))), 8, y + 2, C_DIM)
  else
    local shown = input
    if setup and setup.step == "key" and #input > 4 then
      shown = string.rep("*", #input - 4) .. input:sub(-4)           -- the key is not shown whole
    end
    if utf8.len(shown) and utf8.len(shown) > cols - 2 then shown = shown:sub(utf8.offset(shown, -(cols - 2))) end

    print("> " .. shown .. ((time() * 2) % 2 < 1 and "_" or ""), 8, y + 2, C_TEXT)
    print(setup and "setup: enter answers   esc cancels" or "enter send   /help commands", 8, y + 2 + ch, C_DIM)
  end
  if pad_h > 0 then pt.draw((W - pw) // 2, H - ph - 4) end
end
