-- Cameras: first person (the eye of the local actor, its bob and shake),
-- an orbit for the reel and the spectators, the death camera.

Cam = { x = 0, y = 2, z = 0, yaw = 0, pitch = 0, roll = 0, fov = 96, focal = 144, mode = "fp", fp_fov = 96,
        orbit_d = 6, orbit_h = 1.6, orbit_yaw = 0, orbit_pitch = -0.12 }

local bob = 0

function Cam.set(x, y, z, yaw, pitch, roll, fov)
  Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch, Cam.roll = x, y, z, yaw, pitch, roll or 0
  Cam.fov = fov or Cam.fov
  local th = math.tan(Cam.fov * pi / 360)
  Cam.focal = SCREEN_W * 0.5 / th
  camera3d(x, y, z, yaw, pitch, Cam.fov, Cam.roll)
  -- the axes of the view, for Cam.sees (the roll is small: ignored)
  local sp, cp, sy, cy = sin(pitch), cos(pitch), sin(yaw), cos(yaw)
  Cam.fx, Cam.fy, Cam.fz = sy * cp, sp, cy * cp
  Cam.rx, Cam.rz = cy, -sy
  Cam.ux, Cam.uy, Cam.uz = -sp * sy, cp, -sp * cy
  local tv = th * SCREEN_H / SCREEN_W
  Cam.th, Cam.tv = th, tv
  Cam.sh, Cam.sv = sqrt(1 + th * th), sqrt(1 + tv * tv)
end

-- can the camera see something in the sphere at (x, y, z) of radius r?
function Cam.sees(x, y, z, r)
  local dx, dy, dz = x - Cam.x, y - Cam.y, z - Cam.z
  local fz = dx * Cam.fx + dy * Cam.fy + dz * Cam.fz
  if fz < -r then return false end
  local fx = dx * Cam.rx + dz * Cam.rz
  if abs(fx) > fz * Cam.th + r * Cam.sh then return false end
  local fy = dx * Cam.ux + dy * Cam.uy + dz * Cam.uz
  return abs(fy) <= fz * Cam.tv + r * Cam.sv
end

-- the first-person view of actor a
function Cam.first(a)
  local x, y, z = Actors.eye(a)
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if a.on_ground and speed > 0.5 then bob = bob + DT * speed * 1.6 end
  local by = a.on_ground and sin(bob * 2) * 0.025 * min(1, speed / 5) or 0
  local roll = 0
  if a.st.boost_t and a.st.boost_t > 0 then roll = -a.cmd.mx * 0.12 end
  local sh = Fx.shake
  local jx, jy = 0, 0
  if sh > 0 then jx, jy = (random() - 0.5) * sh * 0.06, (random() - 0.5) * sh * 0.06 end
  Cam.roll = lerp(Cam.roll, roll, 0.12)
  -- on the network the view turns at once, the hero a few frames later (83_net)
  local yaw, pitch = a.yaw, a.pitch
  if Net.on and a == G.local_actor and Net.view_yaw then yaw, pitch = Net.view_yaw, Net.view_pitch end
  Cam.set(x, y + by, z, yaw + jx, pitch + jy, Cam.roll, Cam.fp_fov)
end

-- orbit around a point (reel, spectating, death)
function Cam.orbit(tx, ty, tz, yaw, pitch, d, fov)
  local cp = cos(pitch)
  local x, y, z = tx - sin(yaw) * cp * d, ty - sin(pitch) * d, tz - cos(yaw) * cp * d
  Cam.set(x, y, z, yaw, pitch, 0, fov or 60)
end

-- the camera of a dead player: it looks at its killer from where it fell
function Cam.death(a)
  local k = a.last_hit_by
  local ex, ey, ez = a.x, a.y + 2.5, a.z
  if k and k.alive and k ~= a then
    local dx, dy, dz = k.x - ex, k.y + k.height * 0.6 - ey, k.z - ez
    local yaw = atan(dx, dz)
    local pitch = atan(dy, sqrt(dx * dx + dz * dz))
    Cam.orbit(a.x, a.y + 1.2, a.z, lerp(Cam.yaw, yaw, 0.08), pitch * 0.5 - 0.25, 5, 70)
  else
    Cam.orbit(a.x, a.y + 1.2, a.z, Cam.yaw + DT * 0.3, -0.35, 6, 70)
  end
end
