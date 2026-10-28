# Menu at 1080p on the Pi Zero W: cost of each layer (28 Oct 2026)

Kernel `v0.2.5-1871-gdd990ace` with a test key `menu_probe` (the patch is `menu_probe.patch`, not in the tree). `menu_scale=3`: the V3D draws the menu at 1920x1080. The menu was on the cover grid. The numbers are the `menu frame: draw` line (300 frames each); the reports are on the `reports` branch, folder `reports/claude-menu-gpu-fill-profile/`.

| menu_probe | What is skipped | Draw per frame |
|---|---|---|
| 0 | nothing | 73.5 ms |
| 31 | all GPU drawing (the ARM work only) | 4.1 ms |
| 9 | background (the blit and the ARM work) | 46.9 ms |
| 2 | rectangles | 72.5 ms |
| 4 | text | 50.8 ms |
| 16 | cover blits | 31.9 ms |

For comparison, the menu at 360p on the ARM (`menu_scale` unset): 6.6 ms.

Reading: the ARM work in the menu is 4 ms. The GPU spends about 69 ms. The cover blits cost 41.6 ms, the background 26.6 ms, the text 22.7 ms, the rectangles about 1 ms. The parts add up to 92 ms, more than the 69 ms: the layers overlap in the GPU, so they are not additive.
