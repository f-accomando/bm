# Giant robot fighting game — concept (M20)

Italian version: [mecha-fighter-concept-IT.md](mecha-fighter-concept-IT.md).

The author's original brief (2026-09-28), reproduced with no changes to its content.
The technical design (point 25) will be in `docs/giochi/mecha-fighter-design.md`,
to be written when M20 starts.

---

I want to develop a 2D one-on-one fighting game with giant robots, strongly inspired
by the structure and feel of Street Fighter II Turbo on SNES, but evolved wherever
technically and visually possible, also taking inspiration from later 2D fighting
games, in particular from the philosophy of Marvel Super Heroes vs. Street Fighter:
very dynamic combat, more elaborate combos, verticality, dashes, air combat,
juggles, tag team and greater freedom of movement.

The project must, however, keep a strong 16-bit / SNES-like identity, avoiding
"retro" being interpreted as "minimalist".

## 1. Visual scale — fundamental constraint

This is a fundamental point: the robots must NOT be represented with small
minimalist indie-game sprites. The size and presence of the characters on screen
must be comparable, in order of magnitude and visual importance, to the character
sprites of Street Fighter II Turbo on SNES.

So I want:

- large, clearly readable sprites;
- characters that take up a substantial part of the screen;
- animations made of many frames when possible;
- very readable silhouettes;
- visible details in the armor;
- clearly noticeable impact effects;
- clearly visible deformations, damage and parts breaking off;
- backgrounds with great depth;
- no "micro-sprite" or overly simplified representation.

The reference must be: "Street Fighter II Turbo / Marvel Super Heroes vs. Street
Fighter, but with giant robots and more advanced technology" and NOT: "a small
pixel-art game with tiny robots". When you have to make technical compromises, favor
the readability and the presence of the robots on screen.

## 2. Concept

The game is a 2D fighting game in which gigantic combat robots, as tall as
skyscrapers, fight. They are not simply standard humanoid robots. They are true
modular war machines.

Each robot can be customized:

1. visually;
2. in its stats;
3. in its weapons;
4. in its movement;
5. in its abilities;
6. in its special moves.

The player builds their own robot before the fight. The goal is to create a system in
which the robot's configuration really affects the gameplay, rather than being merely
cosmetic.

## 3. Game modes

The game must support at least:

**Single player** — arcade/campaign mode in which the player faces a series of
opponents. Possible elements: normal matches; bosses; specially configured robots;
fights with special rules; progression through different settings.

**Versus 1v1** — two robots fight directly.

**Tag team** — two robots per team. The player can switch from one robot to the other
during the fight.

**2v2 tag** — two robots against two robots.

The system must be designed from the start to support tag combat, rather than
building it as a later addition.

## 4. Tag system

The tag system must be important and tactical. At the start of the match the team has
**2 switch slots**. Each switch consumes a slot. The slots are not simply consumables
for the whole match: they slowly regenerate over time. So the player has to decide
when to use the tag.

This makes it possible to create: offensive tags; defensive tags; rescues when the
robot is in trouble; combos that end with a switch; alternation strategies;
continuous pressure.

The tag bar/resource must be clearly visible in the interface.

## 5. Customizable robots

Customization is one of the main features of the game. During robot selection the
player must not simply choose a character. They must build/configure a combat
machine. The selection should allow choosing elements such as:

**Base structure** — body; weight; height; frame; mass distribution.

**Armor** — different armor configurations. Armor must have both aesthetic and
gameplay consequences.

- More armor: greater resistance; greater weight; lower acceleration; lower
  agility.
- Less armor: greater speed; greater agility; greater movement capability;
  less protection.

**Weapons** — possible equipment: greatswords; hammers; blades; arm-mounted cannons;
machine guns; rocket launchers; micro-missiles; shoulder weapons; weapons built into
the body; possibly energy weapons.

**Boosters** — installed under the feet; on the back; on the legs; on the shoulders.
They can affect: dash; jump; speed; air movement; recovery; the ability to perform
air dashes. Boosters consume energy.

## 6. Weight and equipment

This must be one of the fundamental principles of the game. More equipment = more
weight.

- More weight means: lower speed; lower acceleration; lower jump; slower dash;
  greater inertia.
- Less equipment means: greater agility; greater speed; better jump; faster dash;
  greater air mobility.

So there must not be a configuration that is simply "the best". The player must
choose between: power / protection / mobility / energy / offensive capability.

## 7. Basic combat

Combat must start from the philosophy of Street Fighter II Turbo. The robots must be
able to perform: punches; kicks; heavy attacks; light attacks; crouching attacks;
jumping attacks; air attacks; grabs; throws; blocks; dashes; jumps; double jump when
allowed by the configuration; special attacks; supers; combos.

But the system must be able to evolve toward the greater spectacle of later fighting
games. So also take inspiration from: air combos; juggles; launches; air dash;
extended combos; cancels; chain combos; special cancels; supers; tag combos.

Do not directly copy existing moves or characters: use these ideas as a reference
for the kind of depth of the combat.

## 8. Verticality

Verticality must be very important. The robots are huge and must be able to exploit
vertical space. Possible mechanics: normal jumps; powered jumps; vertical boosts; air
dashes; attacks from above; launchers; juggles; fights temporarily suspended in the
air; heavy landing; downward attacks.

The robot's configuration must affect these possibilities. A very heavy robot could
have a short jump but an enormous impact on landing. A light robot could have a much
higher jump and greater air control.

## 9. Armor system

This is one of the most important features of the game. Each robot must have a
**health bar** and above it an **armor bar**.

Armor is not simply a second, abstract health bar. It must be physically represented
on the robot. When the armor takes damage: the armor bar decreases; the corresponding
part of the sprite gets damaged; the armor can deform; panels can break; pieces can
come off; internal components can become visible.

And above all: the destruction of the armor must be synchronized between bar and
sprite.

Example: a robot has a large plate on its right shoulder. When that part takes enough
damage:

1. the corresponding portion of the armor bar decreases;
2. the plate shows damage;
3. it eventually comes off;
4. the sprite changes, showing the underlying component;
5. the robot remains visibly damaged for the rest of the fight.

This must create a sense of progressive destruction.

## 10. Localized damage

When possible, consider a localized damage system. Possible sections: head; torso;
right arm; left arm; right leg; left leg; shoulders; external components.

It is not necessary to physically simulate every component. The goal is to get
readable visual and gameplay feedback.

Example: if a cannon on the arm is destroyed: the cannon disappears from the sprite;
that weapon can no longer be used; the robot has to keep fighting with its other
capabilities.

## 11. Holographic shields

Some configurations can use energy/holographic shields. Shields must have their own
condition. When they take damage: they become unstable; they flicker; they show
distortions; they "glitch"; they can lose sections; finally they collapse.

So a perfectly intact shield must look different from an almost destroyed one.
Shields can possibly regenerate slowly, but this must depend on the chosen
configuration.

## 12. Weapon overheating

Firearms must not be infinite. Cannons, machine guns and missile systems must have a
**heat / overheating** resource. After a certain number of shots: the weapon
overheats; the risk of malfunction increases; the weapon becomes temporarily
unusable; the player must wait for it to cool down.

Overheating must be clearly visible through: a bar; effects on the model/sprite;
smoke; glows; animations; possibly sound cues.

This keeps ranged combat from simply becoming "projectile spam".

## 13. Energy

The robot must also have an energy reserve. Energy can be used by: boosters; shields;
some weapons; energy weapons; special abilities.

So the player has to manage several resources at the same time: **health, armor,
energy, heat, tag**. But the interface must stay readable. I don't want a complicated
simulator HUD. It must feel like an arcade fighting game.

## 14. Destruction effect

When a robot takes heavy damage it must look progressively destroyed. Possible
effects: panels blowing off; sparks; smoke; exposed components; cables; glowing
parts; damaged mechanical parts; destroyed weapons; broken glass/sensors.

Above all, this must be readable during combat, not just a cinematic sequence.

## 15. Robot selection — hangar

The selection screen must be one of the distinctive aesthetic features of the game. I
don't want a normal "SELECT YOUR FIGHTER" screen with simple boxes.

The player must be inside a huge industrial hangar. The selected robot is in the
center. Around the robot there are: engineers; workers; platforms; cranes; robotic
arms; scaffolding; elevators; tools; welders; cables; containers; machinery.

The humans must be tiny compared to the robot, so as to make the gigantic scale
evident.

The workers can: weld; carry materials; work on the feet; work on the legs; check
panels; use moving platforms.

The mechanical arms can: mount armor; move components; install weapons; lift panels.

These elements are mainly aesthetic, but they must make the hangar feel alive.

## 16. Hangar animation

The hangar must not be completely static. There must be small, continuous
environmental events: welding; sparks; mechanical arms moving; platforms rising;
workers walking; lights flashing; panels opening; components being carried.

These elements can loop and do not necessarily need a gameplay function. The purpose
is to give the feeling that the robot is really being prepared before the battle.

## 17. Equipment selection

While in the hangar, the player must be able to modify the robot. The screen can
allow selecting: chassis; armor; arms; weapons; shoulders; boosters; shield; energy
system; movement system.

When the player changes a piece of equipment, the robot displayed in the hangar must
really change. Example:

- if I equip a huge hammer, the hammer appears in the robot's hand;
- if I change armor, the model/sprite changes;
- if I add boosters to the feet, they physically appear;
- if I remove a cannon, the cannon disappears.

This must make customization visually satisfying.

## 18. Arenas

Fights must take place in settings where the scale of the robots is evident.
Examples:

- **Abandoned city**: destroyed skyscrapers; streets; cars; buildings; bridges;
  debris.
- **Industrial zone**: factories; tanks; pipelines; cranes; metal structures.
- **Forest**: gigantic trees; terrain; ruins; vegetation.
- **Badlands**: desert; canyons; rocks; abandoned structures.
- **Coastal zone / port**: ships; containers; cranes; industrial buildings.

The settings must help communicate: "these robots are gigantic".

## 19. Background depth

Even though the game is 2D, I want a strong feeling of depth. Use, when possible:
parallax; multiple background layers; elements in front of the robots; elements
behind; particles; smoke; debris; animated environmental objects.

The background must feel like a real place, not a simple static image.

## 20. Graphic style

Art direction: evolved SNES / 16-bit. The aesthetic reference is the SNES generation,
but the project must try to push beyond the classic limits where possible. So:
detailed pixel art; large sprites; smooth animations; rich but consistent palettes;
particle effects; explosions; trails; flashes; energy distortions; large impact
effects.

Do not turn the game into 3D simply because it would be easier to represent the
robots. The combat must remain primarily: a 2D side-view fighting game.

## 21. Design philosophy

The game must be easy to understand but hard to master.

A new player must be able to: move; hit; jump; block; use a weapon; do a simple
combo.

An expert player must be able to exploit: combos; juggles; air combos; dashes; energy
management; heat management; armor management; shield management; tag; robot
configuration; timing.

Complexity must emerge progressively, without making the game immediately feel like
a simulator.

## 22. Priorities

When you have to make design decisions, use this hierarchy:

1. Arcade fighting game feel
2. Large, visually imposing robots
3. Fluid combat
4. Readability
5. Meaningful customization
6. Damage/armor system
7. Tag team
8. Verticality
9. Spectacular effects
10. Aesthetic details

Do not sacrifice playability to add needlessly complex systems.

## 23. Important: avoid minimalism

Do not interpret the project as: "SNES = few pixels = few elements". The goal is
instead: "What would an extremely ambitious SNES fighting game look like, designed
with some of the evolved ideas of later fighting games?"

So I want to see: large sprites; detailed robots; many animations; rich backgrounds;
effects; progressive destruction; customization; vertical combat; combos; tag;
weapons; resource management.

When a feature is not possible to the same extent as on real SNES hardware, implement
a version compatible with the retro style, but do not automatically reduce the scale
or the complexity of the concept.

## 24. Final goal

The result should give this impression: "Street Fighter II Turbo meets Marvel Super
Heroes vs. Street Fighter, but all the fighters are gigantic modular robots built in
a huge hangar."

The player must immediately perceive: weight; scale; power; destruction;
customization; speed; technology; spectacle. And at the same time it must remain
clearly recognizable as: a 2D arcade fighting game.

## 25. What I want from you

Before implementing anything:

1. analyze the concept;
2. identify any conflicts between the mechanics;
3. propose a feasible technical structure;
4. define the core gameplay loop;
5. define the combat resources;
6. define the movement system;
7. define the combo system;
8. define the tag system;
9. define the armor/damage system;
10. define the equipment system;
11. define how the configuration affects stats and moves;
12. define the structure of the arenas;
13. define the HUD;
14. define the hangar screen;
15. define a first playable vertical slice.

Do not start by immediately building the whole game. First I want a concrete, modular
technical design. After the design, proceed in increments, always keeping a playable
build. Each new feature must be testable without breaking the previous ones.

The initial priority is to create a complete, playable 1v1 vertical slice, with:

- 2 robots;
- movement;
- jump;
- dash;
- punches;
- kicks;
- block;
- combos;
- at least one weapon;
- energy;
- heat;
- health bar;
- armor bar;
- at least one destructible armor piece;
- a small arena;
- sprites large enough to immediately communicate the scale of the robots.

Only after this core works well, expand the project with full customization, tag
team, more weapons, other arenas and content.
