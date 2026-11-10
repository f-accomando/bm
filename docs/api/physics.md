# API: 2D Rigid-Body Physics

Part of the game API. Index and basics: [API.md](../API.md).

A compact sequential-impulse 2D physics world (`phys_world`), with up to 128 bodies, 256 contacts and 64 joints per world.

### World

| Function | Description |
|---|---|
| `phys_world([gx, gy])` | creates a 2D physics world with gravity `(gx, gy)` (defaults to `0, 9.8`) |
| `w:body(type, x, y, shape, ...)` | creates a body (`"static"`, `"kinematic"` or `"dynamic"`) at `(x, y)` with `"circle", r, [density]`, `"box", w, h, [density]` or `"poly", pts, [density]` |
| `w:step([dt, iters])` | advances the simulation by `dt` seconds (default `1/60`) using `iters` impulse iterations (default `8`) |
| `w:gravity([gx, gy])` | gets or sets the world gravity `(gx, gy)` |
| `w:joint_distance(b1, b2, [ax1, ay1, ax2, ay2, len])` | creates a distance constraint between two bodies (anchors in world coordinates; `len` defaults to initial distance) |
| `w:joint_revolute(b1, b2, [ax, ay])` | pins two bodies together at world anchor `(ax, ay)` allowing relative rotation |
| `w:joint_weld(b1, b2, [ax, ay])` | rigidly welds two bodies at world anchor `(ax, ay)` |
| `w:contacts()` | returns the list of active contacts from the last step: `{ {a, b, nx, ny, x, y, depth}, ... }` |
| `w:count()` | returns the number of active bodies in the world |

### Bodies

| Function | Description |
|---|---|
| `b:pos([x, y, angle])` | returns `x, y, angle`, or sets the body's position and angle in radians |
| `b:vel([vx, vy, av])` | returns `vx, vy, angular_vel`, or sets linear and angular velocity |
| `b:apply_force(fx, fy, [px, py])` | applies force `(fx, fy)` (optional world point `(px, py)` adds torque) |
| `b:apply_impulse(ix, iy, [px, py])` | applies instantaneous impulse `(ix, iy)` (optional world point adds angular velocity) |
| `b:material([restitution, friction])` | gets or sets `restitution` and `friction` coefficients |
| `b:mass()` | returns `mass, inertia` of the body (`0, 0` for static/kinematic) |
| `b:id()` | returns the integer body slot index |
| `b:remove()` | removes the body and its joints from the world |
