-- bmweb: URL helpers (RFC 3986 subset). Pure Lua, no kernel calls.
-- Safety: https only by default. http needs opts.allow_http. Every other
-- scheme (file, data, javascript, ftp, mailto ...) is refused. A URL with
-- user:password@ is refused (the cart sends no credentials).
local M = {}

M.MAX_URL = 2048
local DEFAULT_PORT = { https = 443, http = 80 }

-- Clean a raw reference like an HTML attribute value: strip outer control
-- characters and spaces, drop tab and newline, encode the unsafe bytes.
local function clean(s)
  s = s:gsub("^[%c ]+", ""):gsub("[%c ]+$", ""):gsub("[\t\r\n]", "")
  s = s:gsub('[%c "<>`\128-\255]', function(c) return string.format("%%%02X", c:byte()) end)
  return s
end

-- Split a reference into parts. Returns a table or nil, reason.
-- Fields: scheme, host, port, auth (true when "//" was present), path,
-- query, fragment. It does not check which schemes are allowed.
function M.parse(ref)
  if type(ref) ~= "string" then return nil, "not a string" end
  local s = clean(ref)
  if #s > M.MAX_URL then return nil, "URL too long" end
  local r = { path = "" }
  local frag = s:find("#", 1, true)
  if frag then r.fragment = s:sub(frag + 1); s = s:sub(1, frag - 1) end
  local q = s:find("?", 1, true)
  if q then r.query = s:sub(q + 1); s = s:sub(1, q - 1) end
  -- A backslash is a slash in the part before "?" (browsers do this).
  s = s:gsub("\\", "/")
  local scheme, rest = s:match("^([A-Za-z][A-Za-z0-9+.%-]*):(.*)$")
  if scheme then r.scheme = scheme:lower(); s = rest end
  if s:sub(1, 2) == "//" then
    local auth, path = s:match("^//([^/]*)(.*)$")
    if auth:find("@", 1, true) then return nil, "credentials in URL" end
    local host, port = auth, nil
    local v6, after = auth:match("^(%[[%x:.]+%])(.*)$")
    if v6 then host = v6; port = after:match("^:(%d*)$"); if after ~= "" and not port then return nil, "bad host" end
    else
      host, port = auth:match("^([^:]*):?(%d*)$")
      if not host then return nil, "bad host" end
    end
    if host == "" or not (v6 or host:match("^[A-Za-z0-9.%-]+$")) then return nil, "bad host" end
    if port and port ~= "" then
      port = tonumber(port)
      if port < 1 or port > 65535 then return nil, "bad port" end
      r.port = port
    end
    r.host = host:lower(); r.auth = true; r.path = path
  else
    r.path = s
  end
  return r
end

local function remove_dots(path)
  if path == "" then return "" end
  local out, n = {}, 0
  local last_dir = false
  for seg in (path .. "/"):gmatch("([^/]*)/") do
    last_dir = false
    if seg == "." then last_dir = true
    elseif seg == ".." then
      if n > 1 then out[n] = nil; n = n - 1 end
      last_dir = true
    else n = n + 1; out[n] = seg end
  end
  -- out[1] is "" for an absolute path (the part before the first slash).
  local p = table.concat(out, "/", 1, n)
  if last_dir and p:sub(-1) ~= "/" then p = p .. "/" end
  if p:sub(1, 1) ~= "/" then p = "/" .. p end
  return p
end

local function serialize(t)
  local s = t.scheme .. "://" .. t.host
  if t.port and t.port ~= DEFAULT_PORT[t.scheme] then s = s .. ":" .. t.port end
  s = s .. (t.path == "" and "/" or t.path)
  if t.query then s = s .. "?" .. t.query end
  if t.fragment then s = s .. "#" .. t.fragment end
  return s
end
M.serialize = serialize

-- Is this scheme allowed? Returns true or nil, reason.
function M.scheme_ok(scheme, opts)
  if scheme == "https" then return true end
  if scheme == "http" and opts and opts.allow_http then return true end
  return nil, "scheme not allowed: " .. tostring(scheme)
end

-- Resolve ref against base (an absolute URL string). Returns the absolute
-- URL string or nil, reason. A result with a refused scheme is nil.
function M.resolve(base, ref, opts)
  local b, e = M.parse(base)
  if not b then return nil, "bad base: " .. e end
  if not (b.scheme and b.auth) then return nil, "base is not absolute" end
  local r, e2 = M.parse(ref)
  if not r then return nil, e2 end
  local t = {}
  if r.scheme then
    if not r.auth then return nil, "scheme not allowed: " .. r.scheme end
    t.scheme, t.host, t.port, t.path, t.query = r.scheme, r.host, r.port, remove_dots(r.path), r.query
  elseif r.auth then
    t.scheme, t.host, t.port, t.path, t.query = b.scheme, r.host, r.port, remove_dots(r.path), r.query
  else
    t.scheme, t.host, t.port = b.scheme, b.host, b.port
    if r.path == "" then
      t.path = b.path
      t.query = r.query or b.query
    else
      if r.path:sub(1, 1) == "/" then t.path = remove_dots(r.path)
      else
        local dir = b.path:match("^(.*/)") or "/"
        t.path = remove_dots(dir .. r.path)
      end
      t.query = r.query
    end
  end
  t.fragment = r.fragment
  local ok, why = M.scheme_ok(t.scheme, opts)
  if not ok then return nil, why end
  return serialize(t)
end

-- Check an absolute URL string and return its normal form, or nil, reason.
function M.check(url, opts)
  local r, e = M.parse(url)
  if not r then return nil, e end
  if not (r.scheme and r.auth) then return nil, "not an absolute URL" end
  local ok, why = M.scheme_ok(r.scheme, opts)
  if not ok then return nil, why end
  r.path = remove_dots(r.path)
  return serialize(r)
end

function M.strip_fragment(url) return (url:gsub("#.*$", "")) end

-- Text typed in the URL bar. No scheme means https://.
function M.from_input(text, opts)
  local s = clean(text or "")
  if s == "" then return nil, "empty" end
  if s:find("^[A-Za-z][A-Za-z0-9+.%-]*://") then return M.check(s, opts) end
  -- "host:port/path" has a colon but is not a scheme.
  if s:find("^[A-Za-z][A-Za-z0-9+.%-]*:") and not s:find("^[%w.%-]+:%d+") then
    return nil, "scheme not allowed: " .. s:match("^([^:]+)"):lower()
  end
  return M.check("https://" .. s, opts)
end

local function pct(s)
  s = s:gsub("[^%w%-_.~ ]", function(c) return string.format("%%%02X", c:byte()) end)
  return (s:gsub(" ", "+"))
end
M.encode_component = pct

-- GET form: action URL plus a list of {name, value} pairs. The action
-- query is replaced (as a browser does) and the fragment is dropped.
function M.form_get(action, fields, opts)
  local a, e = M.parse(action)
  if not a then return nil, e end
  local parts = {}
  for i = 1, #fields do parts[i] = pct(fields[i][1]) .. "=" .. pct(fields[i][2] or "") end
  a.query = table.concat(parts, "&")
  a.fragment = nil
  a.path = remove_dots(a.path)
  local ok, why = M.scheme_ok(a.scheme, opts)
  if not ok then return nil, why end
  return serialize(a)
end

return M
