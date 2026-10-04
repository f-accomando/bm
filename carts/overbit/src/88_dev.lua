-- Dev kit overlay (Select / Tab / F1): the cost of the frame on the console,
-- split in its phases, what was drawn, the quality and the governor.
-- F2 the next quality level (manual), F3 the governor on or off, F4 a full
-- ultimate, F5 lose this life (a mech breaks and its pilot ejects), F6 the
-- free camera (fly with the movement keys, look with the arrows or the right
-- stick, jump up, crouch down; the hero waits).

Dev = { cam = { on = false, x = 0, y = 2, z = 0, yaw = 0, pitch = 0 } }

-- every frame after the input: the free camera takes the commands
function Dev.update()
  local c = Input.cmd
  local fc = Dev.cam
  if G.dev and c.f6_p then
    fc.on = not fc.on
    fc.x, fc.y, fc.z, fc.yaw, fc.pitch = Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch
  end
  if not fc.on then return end
  fc.yaw = wrap_angle(fc.yaw + c.look_x)
  fc.pitch = clamp(fc.pitch + c.look_y, -1.5, 1.5)
  local sp = 8 * DT
  local cp = cos(fc.pitch)
  local fx, fy, fz = sin(fc.yaw) * cp, sin(fc.pitch), cos(fc.yaw) * cp
  local rx, rz = cos(fc.yaw), -sin(fc.yaw)
  fc.x = fc.x + (fx * c.mz + rx * c.mx) * sp
  fc.y = fc.y + fy * c.mz * sp + ((c.jump and 1 or 0) - (c.crouch and 1 or 0)) * sp
  fc.z = fc.z + (fz * c.mz + rz * c.mx) * sp
  Input.blank(c)
end

-- the scene from the free camera, if it is on (true: drawn)
function Dev.draw_cam(extra)
  local fc = Dev.cam
  if not fc.on then return false end
  Cam.set(fc.x, fc.y, fc.z, fc.yaw, fc.pitch, 0, Cam.fp_fov)
  Modes.draw_scene(nil, extra)
  Fx.draw2d()
  return true
end

local graph = {}
local gi = 0

function Dev.draw()
  font("6x12")
  local ms = stat(1)
  gi = gi % 64 + 1
  graph[gi] = ms
  local x, y = 2, 14
  urectfill(x - 2, y - 2, 128, 112, 0x101418)
  local function line_(s, c) uprint(s, x, y, c or 0xE8ECF0) y = y + 11 end
  line_(string.format("frame %5.1f ms %3d fps", ms, stat(2)), ms > 16.7 and 0xFF6060 or 0x80FF90)
  line_(string.format("update %4.1f ms", G.update_ms or 0))
  line_(string.format("3D draw %4.1f ms", stat(6)))
  line_(string.format("tri %5d  vtx %5d", stat(4), stat(7)))
  line_(string.format("px  %6d", stat(5)))
  line_(string.format("lua %4d KiB", stat(0)))
  line_("quality " .. Quality.names[G.quality + 1] .. (G.qauto and " auto" or ""), 0xFFE070)
  line_(string.format("avg %4.1f ms  actors %d", Quality.avg(), #G.actors))
  -- the last 64 frames: a bar each, the 16.7 ms line in red
  local gy = y + 18
  uline(x, gy - 17, x + 63 * 2, gy - 17, 0xFF4040)
  for i = 1, 64 do
    local v = graph[(gi + i - 1) % 64 + 1] or 0
    local h = min(30, floor(v))
    urectfill(x + (i - 1) * 2, gy - h, 1, h, v > 16.7 and 0xFF6060 or 0x60D080)
  end
  font()
end
