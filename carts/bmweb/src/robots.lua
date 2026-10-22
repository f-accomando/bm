-- bmweb: robots.txt. One parse per host. The cart honours the group "bm" (the
-- first token of the kernel User-Agent "bm/<version>") and, if there is none,
-- the group "*". Longest rule wins, Allow wins a tie (RFC 9309). Crawl-delay
-- is read in seconds and capped.
local M = {}

M.MAX_DELAY = 30

function M.parse(text)
  local groups, cur, last_was_agent = {}, nil, false
  for raw in (text or ""):gmatch("[^\r\n]+") do
    local line = raw:gsub("#.*$", ""):gsub("^%s+", ""):gsub("%s+$", "")
    local key, val = line:match("^([%a%-]+)%s*:%s*(.-)$")
    if key then
      key = key:lower()
      if key == "user-agent" then
        if not last_was_agent then cur = nil end
        cur = cur or { agents = {}, rules = {} }
        if not last_was_agent then groups[#groups + 1] = cur end
        cur.agents[#cur.agents + 1] = val:lower()
        last_was_agent = true
      else
        last_was_agent = false
        if cur then
          if key == "allow" or key == "disallow" then
            if val ~= "" then cur.rules[#cur.rules + 1] = { allow = key == "allow", path = val } end
          elseif key == "crawl-delay" then
            local n = tonumber(val)
            if n and n >= 0 then cur.delay = math.min(n, M.MAX_DELAY) end
          end
        end
      end
    end
  end
  local pick
  for _, g in ipairs(groups) do
    for _, a in ipairs(g.agents) do
      if a == "bm" then pick = g end
    end
  end
  if not pick then
    for _, g in ipairs(groups) do
      for _, a in ipairs(g.agents) do
        if a == "*" and not pick then pick = g end
      end
    end
  end
  return pick or { rules = {} }
end

-- A rule path: "*" matches any run, "$" at the end anchors.
local function match(rule, path)
  local anchored = rule:sub(-1) == "$"
  if anchored then rule = rule:sub(1, -2) end
  local pat = "^" .. rule:gsub("[%^%$%(%)%%%.%[%]%+%-%?]", "%%%0"):gsub("%*", ".*")
  if anchored then pat = pat .. "$" end
  return path:find(pat) ~= nil
end

-- May the path (with its query) be fetched? Returns true, or false and the rule.
function M.allowed(robots, path)
  local best, blen, ballow = nil, -1, true
  for _, r in ipairs(robots.rules) do
    if match(r.path, path) then
      local len = #r.path
      if len > blen or (len == blen and r.allow and not ballow) then
        best, blen, ballow = r, len, r.allow
      end
    end
  end
  if best and not best.allow then return false, best.path end
  return true
end

return M
