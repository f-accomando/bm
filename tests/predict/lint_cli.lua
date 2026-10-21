-- The lint of Lua files with the rules of predict.lua, in the output of
-- scripts/api_lint.py (the make check of APP-5b-2 diffs the two):
--   luahost tests/predict/lint_cli.lua build FILE
-- prints FILE:line: unknown API name 'name' for each unknown name called.

local build = arg[1] or "build"
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local P = require "predict"
local path = arg[2]

local f = io.open(path, "rb")
if not f then
  io.stderr:write("Cannot read " .. tostring(path) .. "\n")
  os.exit(2)
end
local text = f:read("a")
f:close()

-- the line breaks of text mode: \r\n and \r become \n
text = text:gsub("\r\n", "\n")
text = text:gsub("\r", "\n")
local lines = {}
for l in (text .. "\n"):gmatch("([^\n]*)\n") do lines[#lines + 1] = l end

for _, issue in ipairs(P.lint(lines)) do
  print(string.format("%s:%d: unknown API name '%s'", path, issue.line, issue.name))
end
os.exit(0)
