-- Stage 7 test program: standard functions and bm kernel function table (cls, pix, rectfill, spr, btn).

function clamp_pos(v: i32, lo: i32, hi: i32): i32
  return min(max(v, lo), hi)
end

function step_and_draw_hud(px: i32, py: i32, hp: i32, max_hp: i32): i32
  cls(1)
  local nx = px
  local ny = py
  if btn(0) then nx = nx - 2 end
  if btn(1) then nx = nx + 2 end
  if btn(2) then ny = ny - 2 end
  if btn(3) then ny = ny + 2 end
  nx = clamp_pos(nx, 8, 312)
  ny = clamp_pos(ny, 8, 232)

  -- Draw HUD health bar and border pixels
  local bar_w = (hp * 80) // max(max_hp, 1)
  rectfill(10, 10, 80, 6, 2)
  if bar_w > 0 then
    rectfill(10, 10, bar_w, 6, 11)
  end
  pix(9, 10, 7)
  pix(90, 10, 7)

  -- Draw player sprite and companion sprites
  spr(16, nx, ny)
  for i = 1, 3 do
    spr(16 + i, nx + i * 12, ny - i * 4)
  end

  return nx * 1000 + ny
end
