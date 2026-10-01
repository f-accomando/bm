-- Every code example of the assistant's knowledge base compiles with the
-- console's Lua 5.4 (syntax only: the examples are pieces of a game).
--   luahost check_snippets.lua build/ai/snippets.txt
local n, bad, id, lines = 0, 0, nil, {}

local function check()
  if not id then return end
  n = n + 1
  local ok, err = load(table.concat(lines, "\n"), "=" .. id, "t", {})
  if not ok then
    bad = bad + 1
    print("FAIL " .. err)
  end
end

for line in io.lines(arg[1]) do
  local new = line:match("^== ([%w_.]+)$")
  if new then
    check()
    id, lines = new, {}
  else
    lines[#lines + 1] = line
  end
end
check()
print(string.format("snippets: %d compiled, %d with errors", n - bad, bad))
os.exit(bad == 0 and n > 0 and 0 or 1)
