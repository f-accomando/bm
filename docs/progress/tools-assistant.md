# Tools: the assistant

Split from [tools](tools.md); open work and rules stay there.

### Assistant (`src/ai/`, F6 in every tool, `carts/assistant` in Dev)
- Q&A over the knowledge base `src/ai/kb/` (`BMAI` file, `assist.c`, small INT8 network,
  `assist.weights` built by `make ai-model`; format in `src/ai/kb/README.md`).
- Recipes: sprites (`sprite.c`), low-poly models with skeletons (`mesh.c`), music
  (`music.c`). Panel shared by the tools (`require "assist"`).
- Low poly help: `src/ai/kb/lowpoly.txt` (guide `guide.lowpoly_1200`, howto `howto.reduce_triangles`, tips
  `tip.tri_limits`, `tip.reduce_or_build`, `tip.reduce_lost_parts`): Studio's models page (F2), key `-`. Numbers from
  the mannequin test, `.claude/skills/bm-forge/references/DETAIL.md` (1200 triangles: 0.46 cm on a 1.8 m figure;
  reduce and build low tie at 1200, reduction wins below about 900).
- Rig help for beginners: `src/ai/kb/rig.txt` (guide `guide.rig_character`, tips `tip.rig_stretch`,
  `tip.rig_one_bone`, `tip.rig_names`, howtos `howto.rig_tail`, `howto.rig_test_pose`): the keys of bm Animator's
  F2 rig page and F3 animate page. Avoid the word "scheletro" in their questions (`mesh.skeleton` is the enemy).
  Animator `t` (rig page): bends each bone 45 degrees round x and z and marks red the faces that stretch or
  whose shared corners come apart (`bone_mats`, `stress_test` in `carts/animator/main.lua`, repeating
  `animate()`'s bone matrices of `src/bm/runtime.c`: change them together; checked equal to bm_core's
  `skin.skin()`; `tests/studio/tools3d_host.lua` presses `t`). Rango test: charkit skin 12 of 984 faces marked,
  smooth `K` 82, rigid `k` 455. Next: select faces by ring/area, mirror the skin, joint rings in bm Studio,
  assistant actions on Animator.
- Organic recipes `biped` (15 bones) and `chameleon` (18 bones, curled tail): one continuous skin,
  `src/ai/mesh_skin.c` (vertex table, rings, `limb`; plan and Work Orders in
  [`design/organic-recipes.md`](../design/organic-recipes.md), WO-0..6 done). KB `mesh.biped`, `mesh.chameleon`,
  `tip.skin_continuous`; held-out 277/345 first, 314/345 in the first three. Open: the Animator `t` test on both
  (not run), the word "coda" makes `howto.rig_tail` win over `mesh.chameleon` for "lizard person with a tail",
  WO-7 (`skin` instructions in the part language / `img2mesh`).
- Mesh from images: `cutout3d` / `picture3d` in the runtime (`src/bm/cutout.c`), PC tools
  `tools/img2mesh.py`, `cutout2mesh.py`, `local2mesh.py`, `meshy*.py` (`MESHY_API_KEY`
  only in the environment).
- Overbit's figures (`carts/overbit/art/meshy/`): `pack.py` packs the models of the branch
  `meshy-out` (`SOURCES`: the image-to-3D soldier, mei, junkrat, sojourn, juno, kiriko, dva,
  beast for sarge, frost, fuse, rail, orbit, akari and the two mechs; reduced to 1200 + 450
  when missing); `meshyrig.py` fits them on the skeletons (Meshy's `.rig` if any, `OWN_GUN`
  drops the old gun). `request.txt` (the shared-key workflows were removed; run `tools/meshy_text.py` with your own `MESHY_API_KEY`):
  text-to-3D, `rig=`, `image=` (image-to-3D from `ref/`).
- Overbit models: the heroes' bodies are the primitives of `carts/overbit/art/heroes/*.py` on the
  skeletons of `art/rig.py` (guns and props on their own bones: nothing fuses in the poses). The Meshy
  figures (`art/meshy`, `meshyrig.py`) are set aside: their arms and guns fused; `OVERBIT_MESHY=1`
  (`models.py --meshy`) builds them again. `make overbit-heroes`: each hero as `.bmm` in
  `build/overbit/heroes/` for bm Mesh / bm Animator. Skeletons and clips are still written by `rig.py`,
  not made in Animator.
- bm Agent (`carts/agent`, `.bme` and `.bm` from `make build/carts/agent.bme`, Market id `agent`, spec 6): a chat with a cloud
  model through `ai_ask` ([ai-agent](../design/ai-agent.md)). Keyboard or pad (`padtype`, on with the first pad button);
  `/setup` (also at the first start: a free provider and its key, or the relay by address and pairing code; a test
  question after saving), `/new`, `/save` (`bm/web/chat.txt`), `/model NAME`; a `<<<file NAME.EXT ... >>>` block in an answer becomes a proposal that
  `file_write` saves into `bm/web` only after `y` or ok. Test: `make test-agent` (bmhost, a fake model in
  `tests/agent/prelude.lua`).
- `tools/bm_agent_relay.py`: the subscription path. `cert` makes a P-256 CA, a certificate for the PC's IP and a token;
  `serve` prints the pairing code (CA fingerprint + 8-character token), serves the CA on the next port (plain http), and
  answers `POST /v1/chat/completions` with one run of `claude -p`, `codex exec`, `gemini` or `--cmd`, under the user's
  own login, one request at a time. Test: `make test-agent-relay`.
- `scripts/mkbm.py`: builds a `.bm` (`--lua --sheet --sheet8 --map --flags --sprites --audio
  --cover --models --res --from <cart>`…); header `BMCART` with CRC-32 (`src/bm/bm.h`).
- `scripts/bmres.py`: extract/convert `BMRES` resources. `scripts/bmaudio.py`: sound banks.
  `scripts/bmmesh.py`, `bmdecimate.py`, `tools/bmreduce.py` (quadric collapse, same code as
  `src/bm/decimate.c`).
- `tools/qpuasm.py` → `src/gpu/shaders.h` ([graphics](graphics.md)).
- Yharnam's drawn map: `carts/yharnam/assets/map_ground.csv`, `map_overlay.csv` (drawn: `MAP.drawn`)
  and `map_objects.csv` (`layers_yharnam`, 256×256 cells). `objects` is not drawn directly in-game:
  its placeholders (`art/places.py`, `PLACE` in the atlas, `mkassets.py --places` without rendering
  the frames) place the units `gen()` makes (houses: corner H7/H8/CH + roof tiles = width, body
  tiles only shown; trees, bushes, lamps, braziers, pyres, graves, mausoleums, statues,
  fountains, wells, benches, fences, crates, coffins, cages, bollards, carriages, plus hunter's
  lamps `shrine`=1186, barricades `barricade`=1188, gates `gate`=1190, and boss arenas `boss`=1192):
  a unit or gameplay object stays where its placeholder is, goes without it, and a placeholder no
  unit took makes one (hash of its tile). `ch.kind` (road, pave, grass) is derived from the drawn
  ground tiles in `map_ground.csv` so creature spawning follows edits made in the SDK. Beyond the
  edge lies the ring (`MAP.ring`; the camera stops at the edge, there is no mist since 26 Oct; the edge and the area borders are walls, tree lines and gates, see [yharnam](yharnam.md)). The kinds `wall_h`, `wall_v`, `rocks`, `treeline`, `iron_gate_h`, `iron_gate_v`, `passage_h`, `passage_v` and `deadfall` are appended to `KINDS`; `mkassets.py` writes `assets/sprites.txt` (zones `pv<cell>`) for the SDK's sprite previews. `carts/yharnam/build/mkmap.py` makes the first map from the plan
  (`make yharnam-map`, `tests/yharnam/mapgen.lua`: it overwrites the drawn one), takes it back
  (`--from YHARNAM.BME`), draws it (`--png`).
- `tools/bm_net.py`: kernels, cartridges and files over WiFi (protocol in
  [system](system.md)); `tools/bm_load.py`: serial chainloader and terminal;
  `tools/overbit_relay.py`: the internet relay for `bmnet`.
- `easy_install.sh` (WSL, SD on `/mnt/d`): kernel, `make install`, image, network kernel or
  files, release, Market, monitor, `bm/config.txt`.
- Releases: `scripts/release.sh vX.Y.Z` (signed manifests, `src/net/release.c`).

