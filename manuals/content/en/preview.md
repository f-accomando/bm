title: Screenshots
accent: shots
kicker: A bm how-to
subtitle: Save what is on the screen as a picture, from the keyboard, from a pad or from your own app.
footer: bm manuals - preview

::: readme
**What this is.** One chapter of the bm manuals, here to show a theme. The content is real: it comes from `docs/SCREENSHOTS.md`.

**How to use it.** Follow the numbered steps in order. Anything in a bordered box with a shadow is meant to be copied as it is.

**What is missing.** A manual marks every gap with a highlighted TO FILL tag; nothing is invented.
:::

# Taking a screenshot

Three ways to take a picture of a game or an app, in the order most people need them.

| Way | What you press or call | Where the file goes |
|---|---|---|
| Keyboard | Print Screen | bm/shots/ on the SD card |
| Pad | Select + L1 + R1 together | bm/shots/ on the SD card |
| Your own app | `shot()` in the Lua code | bm/shots/ on the SD card |

## What it does

The console saves the game's own screen, as it was drawn, as a PNG file. The dev kit's overlays, the battery icon and the mouse pointer are left out, so the picture is clean enough for a manual or a cover.

## Why it matters

A bug report with the picture takes a minute to understand. A manual with real pictures is a manual people trust.

## Good to know

- The file name starts with the first letters of the game's title, then a number: `PONG0001.PNG`, `PONG0002.PNG`.
- The number goes up by itself: bm never overwrites a picture. The Pi has no clock, so the name has no date.
- A box says `Screenshot saved` for two seconds.

## Caveats

- The screens of bm's own menu cannot be saved yet. Games and apps can.
- [TO FILL: check Select + L1 + R1 on the PowKiddy RGB30; not tried on the console yet]

## Try it, step by step

1. Put the SD card in the console and start **Pong** from the menu.
2. Press **Print Screen** on the keyboard, or hold **Select + L1 + R1** on the pad.
3. Wait for the box that says `Screenshot saved`.
4. Switch the console off and put the SD card in your computer.
5. Open the folder `bm/shots/`: the picture is there.

![Pong, as the console saved it: bm/shots/PONG0001.PNG](shots/pong-game.png)

::: try
Press Print Screen three times while Pong is running. You get `PONG0001.PNG`, `PONG0002.PNG` and `PONG0003.PNG`.
:::

## From your own app

Call `shot()` anywhere in `_draw`. It returns the name the picture will have.

```lua
function _draw()
  cls(1)
  print("hello", 8, 8, 7)
  if btnp("a") then
    local path = shot()          -- "bm/shots/GAME0001.png"
    log("saved " .. path)
  end
end
```

::: copy
shot()          -- name from the game's title
shot("menu")    -- name starts with MENU
:::

::: warn
`shot()` saves the frame when `_draw` ends. Calling it twice in one frame gives one file.
:::
