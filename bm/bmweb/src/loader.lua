-- bmweb: the request manager. One request at a time, at human pace:
--   * a gap between two requests to the same host (GAP seconds, or the
--     Crawl-delay of its robots.txt if that is longer; the kernel adds its own
--     minimum of 1 s),
--   * robots.txt is read once per host, before the first other request to it,
--     and a path it disallows is not fetched (the job ends with `blocked`),
--   * no prefetch: a job starts only when the cart asks for it.
-- It talks to the kernel only through `api` (web_get, web_poll, web_info,
-- web_body, web_cancel, time), so the host tests can give it a mock.
local url = require("url")
local robots = require("robots")

local M = {}

M.GAP = 3          -- seconds between two requests to one host
M.MAX_HOSTS = 32   -- robots.txt results kept
M.ROBOTS_MAX = 65536

local L = {}
L.__index = L

function M.new(api, opts)
  opts = opts or {}
  return setmetatable({
    api = api, gap = opts.gap or M.GAP, allow_http = opts.allow_http,
    next_at = {},    -- host -> earliest time of the next request
    last_at = {},    -- host -> time of the last request
    robots = {},     -- host -> parsed robots group
    order = {},      -- hosts in the order robots were read (for the cap)
    job = nil, queue_robots = nil, requests = 0,
  }, L)
end

local function hostpath(u)
  local r = url.parse(u)
  if not r or not r.host then return nil end
  local path = (r.path == "" and "/" or r.path) .. (r.query and ("?" .. r.query) or "")
  return r.host:lower(), path, r
end

-- Start a job. kind: "page", "image" or "file". opts: max (body cap), timeout.
-- Returns the job (read job.phase each frame) or nil, reason.
function L:get(u, kind, opts)
  if self.job then return nil, "a request is under way" end
  local ok, why = url.check(u, { allow_http = self.allow_http })
  if not ok then return nil, why end
  local host = hostpath(ok)
  if not host then return nil, "bad address" end
  local job = { url = ok, kind = kind or "page", host = host, opts = opts or {}, phase = "start", t0 = self.api.time() }
  self.job = job
  return job
end

function L:cancel()
  local j = self.job
  if not j then return end
  if j.id then self.api.web_cancel(j.id) end
  if j.rid then self.api.web_cancel(j.rid) end
  self.job = nil
end

-- Free the result of a finished job (the kernel keeps the body until then).
function L:release(job)
  if job and job.res and job.res.id then
    self.api.web_cancel(job.res.id)
    job.res.id = nil
  end
  if self.job == job then self.job = nil end
end

local function remember(self, host, group)
  if not self.robots[host] then
    self.order[#self.order + 1] = host
    if #self.order > M.MAX_HOSTS then
      local old = table.remove(self.order, 1)
      self.robots[old] = nil
    end
  end
  self.robots[host] = group
  if group.delay and self.last_at[host] then       -- the Crawl-delay counts from the robots.txt request
    local t = self.last_at[host] + group.delay
    if t > (self.next_at[host] or 0) then self.next_at[host] = t end
  end
end

local function pause_left(self, host)
  local t = self.next_at[host]
  if not t then return 0 end
  local d = t - self.api.time()
  return d > 0 and d or 0
end

local function start_fetch(self, job, u, max, kind_robots)
  local api = self.api
  local opts = { max = max, timeout = job.opts.timeout }
  local id, err = api.web_get(u, opts)
  if not id then
    job.phase, job.err = "error", err or "cannot start"
    return false
  end
  self.requests = self.requests + 1
  local delay = self.gap
  local g = self.robots[job.host]
  if g and g.delay and g.delay > delay then delay = g.delay end
  self.last_at[job.host] = api.time()
  self.next_at[job.host] = api.time() + delay
  if kind_robots then job.rid = id else job.id = id end
  return true
end

local function finish(self, job, id)
  local info = self.api.web_info(id) or {}
  job.res = { id = id, status = info.status, type = info.type, url = info.url or job.url,
              size = info.size, length = info.length, location = info.location }
  job.id = nil
  job.phase = "done"
end

-- Work the current job a little. Call once a frame. Returns the job when it
-- reaches a final phase ("done", "error", "blocked"), else nil.
function L:step()
  local job = self.job
  if not job then return nil end
  local api = self.api
  local ph = job.phase

  if ph == "start" then
    if self.robots[job.host] == nil then
      job.phase = "robots-pause"
    else
      job.phase = "pause"
    end
    ph = job.phase
  end

  if ph == "robots-pause" then
    job.left = pause_left(self, job.host)
    if job.left > 0 then return nil end
    local ru = "https://" .. job.host .. "/robots.txt"
    local r = select(3, hostpath(job.url))
    if r and r.port then ru = "https://" .. job.host .. ":" .. r.port .. "/robots.txt" end
    if r and r.scheme == "http" then ru = ru:gsub("^https", "http") end
    job.phase = "robots"
    job.rid = nil
    if not start_fetch(self, job, ru, M.ROBOTS_MAX, true) then
      -- robots.txt could not be asked: the job ends in error
      return job
    end
    return nil
  end

  if ph == "robots" then
    local state = api.web_poll(job.rid)
    if state == "done" then
      local info = api.web_info(job.rid) or {}
      local group
      if info.status == 200 then
        group = robots.parse(api.web_body(job.rid) or "")
      elseif info.status and info.status >= 400 and info.status < 500 then
        group = { rules = {} }              -- no robots.txt: everything is allowed
      else
        group = { rules = {} }
      end
      api.web_cancel(job.rid)
      job.rid = nil
      remember(self, job.host, group)
      job.phase = "pause"
    elseif state == "error" or state == nil then
      -- An unreachable host will fail the page too; a server error on robots.txt is read as "no rules".
      api.web_cancel(job.rid)
      job.rid = nil
      remember(self, job.host, { rules = {} })
      job.phase = "pause"
    end
    ph = job.phase
    if ph ~= "pause" then return nil end
  end

  if ph == "pause" then
    local _, path = hostpath(job.url)
    local g = self.robots[job.host]
    if g and job.kind ~= "robots" then
      local okp, rule = robots.allowed(g, path)
      if not okp then
        job.phase, job.blocked = "blocked", rule
        return job
      end
    end
    job.left = pause_left(self, job.host)
    if job.left > 0 then return nil end
    if not start_fetch(self, job, job.url, job.opts.max, false) then return job end
    job.phase = "connect"                    -- one frame to draw "connecting" before the first poll
    return nil
  end

  if ph == "connect" or ph == "busy" or ph == "wait" then
    local state, size, length = api.web_poll(job.id)
    job.size, job.length = size, length
    if state == "done" then
      finish(self, job, job.id)
      return job
    elseif state == "error" or state == nil then
      local info = state and api.web_info(job.id) or {}
      job.err = info.error or size or "request failed"
      api.web_cancel(job.id)
      job.id = nil
      job.phase = "error"
      return job
    else
      job.phase = state == "wait" and "wait" or "busy"
    end
    return nil
  end

  return job
end

return M
