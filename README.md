# bm: reports of the consoles' tests

This branch holds only the reports that bm consoles send (src/kernel/reports.c on
the main branches): the text of a test run on a Raspberry Pi or a RGB30, with a
header saying where it comes from.

    reports/<branch>/<date>_<kind>_<board>_<kernel>.txt

- `<branch>`: the git branch the console's kernel was built from (slashes as dashes);
- `<date>`: the network's time, `20261004-153012`, or `nodate-<random hex>`;
- `<kind>`: `bench3d`, `gpu`, `render`, `stress`, `cpu`, `dma`, `room`, `log`,
  `overbit-bench`...;
- `<board>`: `pi-zero-w`, `pi-1-b...`, `rgb30`;
- `<kernel>`: git describe of the kernel (`v0.1.0-45-gc1dfebb`).

The console needs `github_token` in `bm/config.txt` (a token that can write this
repository's contents) and the network; `report_repo` and `report_branch` change
where they go, `report_upload=0` keeps them on the SD card until they are sent by
hand (Settings > System > Send the reports; `z` in the monitor).
