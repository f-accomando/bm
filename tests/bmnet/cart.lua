-- bmnet (require "bmnet") on two bmhost (BMHOST_NET_ID 0 and 1: 127.0.0.1
-- hosts, 127.0.0.2 joins), run by tests/bmnet/run.py: the lobby, sure
-- messages with a fifth of the packets lost, a lockstep match of 300
-- frames that must give the same hash on both, then the guest leaves and
-- the host goes on alone. RELAY (a line put in front by run.py): the same
-- through tools/overbit_relay.py.
local net = require "bmnet"
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1 end
  log((ok and "bmnet ok   " or "bmnet FAIL ") .. what)
end

local host = net_ip() == "127.0.0.1"
local N = 10                    -- sure messages each way
local MATCH = 300               -- frames of the match
local got_msgs, lost_msgs = {}, 0
local state = 0                 -- the match: a hash of every input
local done_at, f = nil, 0
local alone = 0                 -- host: frames after the guest left

local function finish()
  log(string.format("bmnet: %d/%d checks passed", checks - fails, checks))
  quit()
end

function _init()
  local ok, err = net.open({ game = "TST1", relay = RELAY, room = "TEST", name = host and "Host" or "Guest",
                             loss = 0.2 })
  check(ok, "open: " .. tostring(err))
  if host then net.host({ max = 2, info = "a test" }) end
  -- net.pad / unpad / held: the buttons and the stick in 32 bits
  local b, x, y = net.unpad(0x11 | (0x40 << 16) | (0xC0 << 24))
  check(b == 0x11 and math.abs(x - 64 / 127) < 1e-6 and math.abs(y + 64 / 127) < 1e-6, "unpad")
  check(net.held(0x11, "a") and net.held(0x11, "left") and not net.held(0x11, "b") and not net.held(false, "a"),
        "held")
end

local function on_event(e)
  if e.type == "join" and host then
    check(e.seat == 2 and e.name == "Guest", "host: the guest joins in seat 2")
    for i = 1, N do net.post("h" .. i) end
  elseif e.type == "joined" then
    check(e.seat == 2, "guest: joined, seat 2")
    for i = 1, N do net.post("g" .. i) end
  elseif e.type == "msg" and e.sure then
    got_msgs[#got_msgs + 1] = e.data
  elseif e.type == "msg" then
    lost_msgs = lost_msgs + 1
  elseif e.type == "start" then
    check(e.seed == 4242 and e.data == "fast" and #e.seats == 2 and e.seat == (host and 1 or 2) and
          e.host == host, "start: the same seed, seats and data")
  elseif e.type == "leave" and host then
    check(e.seat == 2 and net.state == "playing", "host: the guest left the match")
  elseif e.type == "desync" then
    check(false, "desync at frame " .. e.frame)
  elseif e.type == "lost" then
    check(not host and done_at, "lost: " .. tostring(e.why))
  end
end

function _update()
  f = f + 1
  for _, e in ipairs(net.update()) do on_event(e) end
  if net.state == "lobby" and not host then
    local hs = net.hosts()
    if #hs > 0 then
      check(hs[1].name == "Host" and hs[1].info == "a test" and hs[1].max == 2, "guest: the host is in the list")
      net.join(hs[1].id)
    end
  end
  -- the sure messages arrive all, in order, though packets are lost
  if #got_msgs == N and not done_at then
    local want = {}
    for i = 1, N do want[i] = (host and "g" or "h") .. i end
    check(table.concat(got_msgs, ",") == table.concat(want, ","), "post: every message, in order")
    done_at = f
    if not host then net.send("hello") end
  end
  if host and net.state == "hosting" and done_at and f > done_at + 30 then
    check(#net.peers() == 2 and net.peers()[2].name == "Guest", "peers: both")
    net.start({ seed = 4242, data = "fast" })
  end
  if net.state == "playing" then
    net.input((f * 2654435761 + net.seat * 97) & 0xFFFFFFFF)
    for fr, ins in net.frames() do
      if fr < MATCH then
        for _, s in ipairs(net.seats) do state = (state * 31 + (ins[s] or 0) * s) & 0xFFFFFFFF end
        if fr % 30 == 29 then net.check(state) end
      elseif fr == MATCH then
        log(string.format("bmnet hash %d %08x", fr, state))
        if not host then
          check(true, "guest: the match ran " .. MATCH .. " frames")
          net.close()
          finish()
          return
        end
      elseif host then
        if ins[2] == false then alone = alone + 1 end
        if alone == 60 then
          check(ins[1] ~= nil, "host: the match goes on without the guest")
          net.close()
          finish()
          return
        end
      end
    end
  end
  if f > 60 * 25 then
    check(false, "time out in state " .. net.state .. " at frame " .. net.frame .. " stall " .. tostring(net.stall))
    finish()
  end
end

function _draw()
  cls(0x101418)
  print("bmnet " .. net.state .. " frame " .. net.frame, 8, 8, 0xFFFFFF)
end
