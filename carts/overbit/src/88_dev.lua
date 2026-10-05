-- Dev kit: the system's overlay (F11; Select, Tab or F1 here: simple,
-- detailed, off), which shows the frame in its phases, the GPU and what was
-- drawn; Overbit adds its lines to the detailed page (devinfo): the quality,
-- the governor and its average, the actors. While it is shown: F2 the next
-- quality level (manual), F3 the governor on or off, F4 a full ultimate, F5
-- lose this life (a mech breaks and its pilot ejects), F6 the free camera
-- (fly with the movement keys, look with the arrows or the right stick, jump
-- up, crouch down; the hero waits).

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

-- Overbit's lines on the dev kit's detailed page
function Dev.info()
  devinfo("quality " .. Quality.names[G.quality + 1],
          string.format("%s avg %.1fms", G.qauto and "auto" or "fixed", Quality.avg()),
          "actors " .. #G.actors)
end
