# Dev kit: weight, performance plan, compat policy

Tool: `scripts/bm_weight.py`. Data: `docs/spec/compat-policy.toml`. Test: `tests/tools/test_bm_weight.py` (run it with `python tests/tools/test_bm_weight.py`; `make test-docs` finds it). Design decisions: [single-bm-and-adaptive-screen.md](../design/single-bm-and-adaptive-screen.md) (decisions 1 and 2). Overview: [progress.md](../progress.md).

## Rules (do not break)
* The Market shows no size warning. The ceiling is 100 MiB per file. The kit tells the author about weight and cost.
* The performance plan is data. Each number names its source doc and says "estimate". It uses no live game data.
* Hardware numbers come from the benchmarks: 3D Bench, Dev > GPU test, Stack bench, Video bench. The report prints the command for the next measurement.
* The compat policy is a data table. A new kernel or a new rule changes the table, not the code. Grey is not a lock.
* A new cost constant goes in `COSTS` of `bm_weight.py` with its source doc. Read the section parser from `bmmesh.cart_sections`. Do not write a second parser.

## What it does
* `python scripts/bm_weight.py FILE` reads a `.bm`, `.b16` or `.bme` (one container, see [BM-FORMAT.md](../BM-FORMAT.md)). It prints bytes per section, per kind (sprites, tiles, maps, audio, models, Lua), and per asset (each model one by one), biggest first, with the total and the percent. The kind of file comes from the extension, then from the header flags.
* The performance plan estimates the SD read and write time (5.14 and 2.50 MB/s, Pi Zero W), the RAM of the file plus the decoded sheet, the copies of each model per frame at 60 fps on the GPU path and on the ARM rasterizer, and flags heavy parts (model above the largest one measured, file above the Market ceiling or the old `.b16` limit, sheet above 4096, audio bank near the PCM limit, an asset over half of the file).
* `--json` prints one object. `--weight-only` skips the plan and the policy. `--policy FILE` reads another table. `--kernel ID` limits the policy output.
* Module use: `parse`, `weight`, `plan`, `load_policy`, `facts_of`, `classify(facts, kernel, policy, where)`.

## Compat policy table
Rows: `[[kernel]]` (id as in `platforms.toml`, default action and reason) and `[[rule]]` (kernel, match keys, `action` for the menu, optional `market_action`, `reason`). The first matching rule of a kernel wins. Actions: `show`, `grey`, `hide`. Match keys: `edition` (bm, b16, project), `res`, `uses_3d`, `audio_bytes_over`, `size_over_mib`. Today: old `.bm` is grey on the RGB30; `.b16` shows on both; a `.bme` project is hidden; an audio bank over 2 MiB is grey; over 100 MiB is hidden in the Market.

A consumer (the Market list, `src/kernel/market.c` `adopt()`, and the menu scan) would read these fields:
* from the file: header flags (bit 0 project, bit 1 `.b16`), the extension, the resolution, the AUDIO section size, the file size, the presence of a MESH section;
* from the table: the kernel id of the running kernel, then `action` (menu) or `market_action` (Market) and `reason` (the text shown next to a greyed item).

No kernel or Market code reads the table yet.

## Open work
* Kernel and Market: read the table (menu scan and `adopt()`); the compat header (bytes 120..127) will add `targets`, `input` and `aspect` match keys.
* Plan: add measured costs when the Pi runs give them (CRC time, sheet decode time, queue on). Add a row for the RGB30 3D path when the Mali benchmark has numbers.
* SDK page: show the weight report on the device. Not started.
