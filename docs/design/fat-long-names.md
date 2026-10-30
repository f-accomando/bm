# FAT long names (write, up to 16 characters)

Status: plan. No code is written. Plan made on 2026-10-30 from a read of `src/fs/fat.c` and the tests.

## Goal

The kernel writes a file or a folder with a long name (VFAT LFN). The base name has 1 to 16 characters.
A person sees `hunter_lo.bmm` or `Il mio gioco.bm` when the card is in a PC.
Today the kernel reads long names but writes only 8.3 names (`to_83` in `src/fs/fat.c`).

## Facts found in the code

- `fat_readdir` gives only the long name when a valid long name exists. The 8.3 alias is lost.
  `name_eq` is the only match: `fat_opendir`, `locate` and `fat_delete` do not find `HUNTER~1.BMM`.
- `add_entry` takes the first free slot. A long name needs K slots in a row. When the folder grows,
  the run must continue from the free tail into the new cluster. A `0x00` slot before a used slot hides the file.
- `store()` gives the CPU back until the data is written. Choose the alias and the slots after that,
  in the window with no tick. Then two writers cannot take the same alias.
- `src/fs/sdlock.c` needs no change.
- `make test-fat` needs `mkfs.vfat`, mtools and `fsck.vfat`. They are not in the Claude cloud container.
  CI installs them. The test always uses FAT32. FAT16 is not tested today.
- `tests/qemu_test.py` `test_monitor_files` expects `cp note.txt toolongname.txt` to fail with "8.3". It must change.
- The cartridge header keeps the project name in 16 bytes (`bm.h`, offset 104), as an 8.3 name.
  Project paths stay 8.3. A change is a format change.
- False positives, do not touch: `api_save.c` (slots), `api_map.c` (layers), `api_sound.c` (semitones).

## Decisions (the owner confirms D1 to D3 and D7 before WO-2)

| Id | Decision | Recommendation |
|---|---|---|
| D1 | Name rule "16.3" | Base 1 to 16 characters, one dot, extension 0 to 3 characters. Allowed: `A-Z a-z 0-9`, space, `_ - ~ ( )`. No leading or trailing space or dot. No byte of 0x80 or more. |
| D2 | When to write a long name | A name that fits strict 8.3 stays one 8.3 entry with the lowercase flags, as today. Any other name gets long pieces in the exact case of the caller. A short name with a space now gets long pieces. The spec `lowercase-names` in `docs/spec/specs.toml` changes. |
| D3 | Alias | Uppercase. Drop spaces and dots. Replace a character outside 8.3 with `_`. Base = the first characters plus `~N`. N is the lowest free number from 1 to 99. After 99: "too many similar names". |
| D4 | Write order | Long pieces first, short entry last. One `patch_sector` for each sector. The sector with the short entry is written last. A power cut leaves orphan pieces that no reader shows. |
| D5 | Lookups | Add `char alias[13]` to `fat_entry_t`. Every match accepts the long name or the alias, in any case. |
| D6 | Test oracle | No `pyfatfs`. CI runs `fsck.vfat` and mtools. A new in-memory test needs no tool. |
| D7 | Callers to widen | Monitor `cp` and `mkdir`, `.bmm` names, `netfetch` NAME, `doc_*` names. Keep project paths and `bm/web` at 8.3. |

## Risks

1. Power cut inside a long-name set: D4.
2. Two writers pick the same alias: choose the alias in the no-tick window.
3. A `0x00` slot before a used slot after the folder grows: the run rule.
4. FAT16 fixed root: the run may not fit. Return "root directory full".
5. Files written by a PC (`PROGRA~1`) must stay found: D5 and tests.
6. The spec `lowercase-names` conflicts with D2: change the text.
7. Callers have 13-byte buffers (`netfetch.h`, `api_docs.c`, `api_web.c`, `webget.c`, `webfile.c`, `market.c`,
   `webpolicy.h`, `api_editor.c` `file[16]`). Widen the buffer with the validator.

## Work orders, in this order

| WO | What | Files | Check | Agent |
|---|---|---|---|---|
| 1 | Test harness: FAT16 and FAT32 images in RAM, count sector writes, drop writes after N (power cut) | new `tests/fs/test_fat_lfn.c`, Makefile target `test-fat-lfn` | `make test-fat-lfn` returns 0 on both | bm-light |
| 2 | Acceptance cases, red first: (a) read back by long name and alias, (b) raw slots, (c) alias numbers to 99, (d) replace, (e) rejected names, (f) long folders, (g) run across sector and cluster edge, (h) full FAT16 root, (i) power cut at every write, (j) delete | `test_fat_lfn.c`, `tests/fs/test_fat.c` (`test_lfn`), `tests/fs/run.py` | `make test-fat-lfn` fails only in the new cases | bm-light |
| 3 | Read side: `alias` in `fat_entry_t`, `entry_is()` in the three match sites | `fat.h`, `fat.c` (`fat_readdir`, `fat_opendir`, `locate`, `fat_delete`) | alias lookups pass | bm-coder |
| 4 | Write side: strict `to_83`, `check_lfn`, `make_alias`, `add_entries`, `fat_mkdirs`, `fat_write_file`, `store` | `fat.c`, `fat.h` (`FAT_WNAME_MAX 21`, comments) | `make test-fat-lfn` returns 0; CI `make test-fat` returns 0 | bm-deep (necessity 4) |
| 5 | Monitor messages and help, QEMU test | `monitor_fs.c`, `monitor.c`, `tests/qemu_test.py` | CI `-k monitor_files` | bm-light |
| 6 | `.bmm` names up to 16 characters | `api_editor.c`, `bm3d.lua` (`bmm_stem` and the `sub(1, 8)`), `api_web.c`, `runtime.c`, `tests/studio/import_host.lua` | `luahost tests/studio/import_host.lua`; CI `make test-web-bmm` | bm-light |
| 7 | `netfetch` NAME and `doc_*` names | `netfetch.h`, `netfetch.c`, `api_docs.c` | build for `pi` and `rgb30` with no new warning; CI `-k docs` | bm-coder |
| 8 | Docs and spec | `docs/progress/system.md`, `system-net.md`, `specs.toml`, `docs/api/system.md`, `docs/RISORSE.md` point 6 | `docs_audit.py score`, `consistency.py check`, then `gate` | bm-light |

## Needs the owner or hardware

- Confirm D1 to D3 and D7 before WO-2.
- After CI is green, on a Pi and an RGB30: write a long name from the monitor and from bm Mesh.
  Put the card in Windows: the names show and `chkdsk` reports no error. On Linux `fsck.vfat -n` is clean.
  Pull the power during a Market install: no `~1` ghost file.
- The Claude cloud container cannot run `make test-fat` or the QEMU tests. CI is the check for WO-2, 4 and 5.

## Closure

Update `## Open work` and `## Rules (do not break)` in `docs/progress/system.md`.
Rules to add: write a name that fits 8.3 as one 8.3 entry with the lowercase flags; write a longer name as long pieces and
an alias `NAME~N`; write the pieces first and the short entry last; choose the alias and the slots after the data, with no tick;
keep project names 8.3 because the cartridge header has 16 bytes for them.
Run `docs_audit.py score`, `consistency.py check` and `gate`. Run `ci_audit.py` if `test-fat-lfn` goes into the CI.
`config-keys.toml` and `divergences.toml` do not change: both kernels share `fat.c`.
