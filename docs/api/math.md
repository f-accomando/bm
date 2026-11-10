# API: Procedural noise and math helpers

Part of the game API. Index and basics: [API.md](../API.md).

| Function | Description |
|---|---|
| `noise(x, [y, z])` | 1D, 2D or 3D Perlin 2002 improved noise in `[-1, 1]` |
| `fbm(x, y, [octaves, lacunarity, gain])` | normalized Fractal Brownian Motion over Perlin noise (`octaves` default 4, `lacunarity` 2.0, `gain` 0.5) |
| `random_normal([mean, stddev])` | Gaussian random number using Box-Muller transform (`mean` default 0, `stddev` 1) |
| `gamma_to_linear(r, [g, b])` | converts sRGB value(s) in `[0, 1]` to linear light space |
| `linear_to_gamma(r, [g, b])` | converts linear light value(s) in `[0, 1]` to sRGB gamma space |
| `bezier(t, p0x, p0y, p1x, p1y, p2x, p2y, [p3x, p3y])` | evaluates a quadratic (7 arguments) or cubic (9 arguments) Bezier curve at parameter `t`, returning `x, y` |
| `is_convex(points)` | returns `true` if the polygon (`{x1, y1, ...}` or `{{x1, y1}, ...}`) is strictly convex |
| `triangulate(points)` | decomposes a simple polygon into a list of triangles `{ {x1, y1, x2, y2, x3, y3}, ... }` via ear clipping |
