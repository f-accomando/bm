# Titan Clash — design (M20)

Italian version: [mecha-fighter-design-IT.md](mecha-fighter-design-IT.md).

Answer to point 25 of the [concept](mecha-fighter-concept.md). On 2026-09-29 the author
asked to start right away with a playable base (MVP): **a single robot** for all
players, few options (**light or heavy armor**, **sword or arm-mounted machine
guns**), high-quality sprites, freedom on the detail choices. This document records
the decisions made for the MVP and how they extend toward the full concept.

Code: `carts/titan/` (cartridge *Titan Clash*). Tests: `make test-titan` (host) and
`test_titan` in `tests/qemu_test.py`.

## 1. Conflicts between the mechanics and how they are resolved

| Conflict | Decision |
|---|---|
| Robots "as tall as skyscrapers" but readable like SF2 | SF2 sprite scale (robot ~190 px out of 360, half the screen); the gigantic scale is told by the background (16 px cars, street lamps, buildings at the same height) and by the hangar (11 px workers) |
| Many equipment combinations vs many frames | **Layered** robot: each frame has a base image and overlaid layers (heavy armor, intact/cracked pauldron, cannons, sword) rendered from the same skeleton; a combination costs zero extra frames |
| Rich pixel art vs memory budget | **Pre-rendered** sprites (procedural 3D model → cel shading with 6-tone ramps and outlines) and sheets with palette and RLE (SHEET8 section, up to 2048×4096) |
| Armor as a "second bar" vs physical armor | A single armor bar, synchronized with the pauldron: intact, **cracked at half**, **torn off at zero** (the piece flies off, bounces and stays in the street) |
| Ranged weapons vs "projectile spam" | Heat: 6 shots per burst, overheating at 100 (2.5 s locked, smoke); the sword costs energy, which is also needed for dashes |
| Many resources vs arcade HUD | At the top health and armor (the two that decide the round), at the bottom energy and heat; heat only for those with cannons |
| Tag team from the start vs 1v1 MVP | Postponed (below): the fighter logic keeps no global state, a team will be a list of fighters with one active |

## 2. Technical structure

- **Art** (Python, the outputs are in git too, `make` does not regenerate them):
  `mkrobot.py` (model of the VANGUARD robot: solids on a 16-bone skeleton, 63 poses
  in 24 animations, orthographic render in 3/4 view, per-frame hurtboxes and hitboxes
  from the bones), `art.py` (city in 4 parallax planes, hangar, effects, text),
  `mkassets.py` (packs everything into `sheet.png` and writes `src/05_sprites.lua`).
  Sparse layers are split into 8×8 blocks and identical pieces are shared.
  P2 has a **second livery** (crimson and gold, green visor), recoloring the ramps.
- **Kernel**: `SHEET8` section of the `.bm` format (palette ≤256 colors + RLE,
  decoded at load time), `BM_SHEET_MAX` 4096; `mkbm.py --sheet8`.
- **Game** (Lua, files in `src/` joined by `build.py`): `20_fighter` (controls, states,
  hits), `30_fx` (particles and projectiles), `40_stage` (arena), `50_hud`, `60_cpu`,
  `70_screens` (title, mode, hangar, match, pause), `80_audio`.
- Logic at a fixed 60 Hz; combat uses only integer frames (move ticks, hitstop,
  stun), no dependency on the frame rate.

## 3. Core gameplay loop

Title → mode (1P vs CPU, 2 players, CPU vs CPU; CPU level) → hangar (each player
configures their robot and gives READY) → best-of-3 match of 99 s rounds (ROUND n,
FIGHT!, K.O. or TIME OVER) → result (rematch, hangar, title). With no players, the
title shows a CPU vs CPU demo.

## 4. Resources

| Resource | MVP |
|---|---|
| Health | 1000; K.O. at zero |
| Armor | light 240, heavy 400; absorbs 64% / 75% of the damage while it lasts; blocking wears it down a little (12%) |
| Energy | 100, recharges (0.40 / 0.26 per frame); dash 16 / 22, air dash 20, slash 30 |
| Heat | cannons only: +9 per shot, −0.3 per frame; at 100 overheated for 150 frames |
| Tag | postponed (see 8) |

## 5. Movement

Walk forward/backward, crouch, jump (vertical, forward, backward) with weight
control: light jumps 10.6 with a **double jump**, heavy 8.7 and none. **Dash** with a
double tap forward/backward, also **in the air**. Heavy landing with dust and screen
shake. The robots do not overlap (push at 88 px) and the camera follows the midpoint
in an arena 1024 px wide.

## 6. Combos

Six buttons reduced to four (SNES/DS4 pad): X light punch, Y heavy punch,
A light kick, B heavy kick; crouching and air versions.
- **Chain/cancel**: a move that hits (or is blocked) can be canceled into one of
  higher rank (light → medium → heavy → weapon).
- **Launcher**: the crouching heavy punch launches into the air; the airborne robot
  can be hit again (**juggle**, at most 4 hits).
- **Knockdown**: heavy kick, sweep, slash.
- Damage scaled in combos (−12% per hit, minimum 40%), "N HITS" counter.
- Weapon: forward quarter-circle (↓↘→) + punch, or Y+B together (for beginners).

## 7. Blocking

Back = high block, down-back = low block; sweeps must be blocked low, jumping attacks
high. Blocking stops the damage to health but consumes armor.

## 8. Tag (after the MVP)

As in the concept: 2 switch slots that slowly regenerate, a visible bar under the
health; the partner comes in with an attack (offensive tag) or to rescue. In the MVP:
structure ready (independent fighters, HUD with space under the bars), no tag.

## 9. Armor and damage

MVP: one zone (the **pauldron**), 3 states synchronized with the bar (notch at half):
intact → cracked (blue sparks, sound) → torn off (the piece flies off with an
explosion, stays on the ground and then disappears). The text ARMOR BROKEN flashes in
the name. Later: other zones (head, arms with the weapons, legs), destructible
weapons, holographic shields (semi-transparent layer with glitch states).

## 10. Equipment

MVP: **armor** (light / heavy: `hv` layers and large pauldron) and **weapon**
(sword on the back that moves to the hand during the slash / two cannons on the
forearms). Changing in the hangar really changes the sprite. Later: boosters, shield,
other chassis, hammer, missiles (each piece = one layer + stats).

## 11. Stats and moves from the configuration

| | Light | Heavy |
|---|---|---|
| Walk / backward | 2.8 / 2.2 | 1.9 / 1.5 |
| Jump | 10.6 + double jump | 8.7 |
| Dash | 9.5 for 15 frames | 7.2 for 12 frames |
| Armor / absorption | 240 / 64% | 400 / 75% |
| Damage | ×1.05 | ×1.1 |

The sword gives the **slash** (175, knockdown, ×1.5 against armor); the cannons the
**burst** (6 shots of 24, also at range). In CPU vs CPU tests the four combinations
win between 44% and 59% of the rounds.

## 12. Arenas

MVP: **abandoned city at sunset**: banded sky with a low sun, distant skyline
(parallax 0.25) with distant explosions, destroyed towers at the robots' scale (0.55)
with columns of smoke behind, street with miniature cars and street lamps (1.0) and
small fires, rubble in front of everything (1.3). Later: industrial zone, port,
canyon, forest.

## 13. HUD

At the top: health (with a red damage trail) and below it the armor (as long as the
robot's armor, notch at half), large-digit clock in the center, rounds won, name and
configuration. At the bottom: energy (slash notch) and heat (flashes HOT! when
overheated). P1/P2/CPU label above the head. Select shows the frame time.

## 14. Hangar

Split screen: the left half has player 1 in their bay, the right half player 2 or the
CPU in theirs (for now the same bay mirrored; in the future a different place).
Three-quarter view in depth, Gunpla diorama style (the author's reference,
2026-09-30; the photo is not in the repository because of third-party rights, only
the framing and style are taken from it): black/anthracite structure, trusses, yellow
gantry crane and service arm, yellow-and-black hazard stripes on the edges of the
platform and the walkway, low lights on the back wall. The robot is in the foreground
on the outer side, on the platform, standing still with its arms at rest (`stand`
pose) and hooked at the back by the two bridges of the docking tower;
at the back the **maintenance cage** with two grated bridges, where the tag team
partner will go (empty for now, with workers on the bridges). Each half has its own
crane that moves onto the modified robot (which shakes among the sparks) and a welder
at its feet; between the halves a dark bar with yellow notches. At the top, toward the
center, a compact panel per side: armor, weapon, READY and the 5 stats as notches. No
title on screen. Art: `bay()` in `art.py` (320×360, player 1's half).

## 15. Vertical slice (this base) and next steps

Done: 1v1 against the CPU (3 levels) or 2 players, the same robot with 2×2
configurations, movement, jump and double jump, dashes including air dashes, punches,
kicks, high/low blocks, combos with cancels, launcher and juggle, two weapons (sword
with energy, cannons with heat), health, armor with a destructible pauldron, parallax
arena, animated hangar, music and sounds, pause with the move list, demo.

Next steps, in the order of the concept: test on the Pi and feel tuning →
grabs/throws and supers → localized damage (destructible weapons) and shields → other
robots/chassis and boosters → tag team and 2v2 → arcade with bosses and other arenas.
