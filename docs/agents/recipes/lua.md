# Lookup recipes: area lua

One row per golden question (`golden.toml`); measured by `python scripts/recipe_bench.py bench --area lua`. Max bytes = recipe output on the bench, with headroom; a recipe that grows past it or stops containing the answer is dropped.

| question | command | expected output shape | max bytes |
|---|---|---|---|
| Which C function in runtime.c implements Lua `rgb()`? | `python scripts/cindex.py src/bm/runtime.c l_rgb` | one `<line>   l_rgb` row | 100 |
| Which C function in runtime.c implements Lua `stick()`? | `python scripts/cindex.py src/bm/runtime.c l_stick` | one `<line>   l_stick` row | 100 |
| What does `mget()` return outside the map? | `python -c "[print(l,end='') for l in open('docs/api/draw.md',encoding='utf8') if 'mget(' in l]"` | table row(s) with `mget(` | 600 |
| What is the signature of `fog3d()`? | same, file `docs/api/light3d.md`, key `fog3d(` | table row(s) with `fog3d(` | 600 |
| Where does the `volume()` setting persist? | same, file `docs/api/sound.md`, key `volume([` | one table row | 400 |

Then `Read` the C function with `offset`/`limit` at the printed line when the body is needed.
