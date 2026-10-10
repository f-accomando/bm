#!/usr/bin/env python3
"""Episode 3: bm Sound. Sounds, effects and the level music of Skyvale World,
on the cartridge episode 2 saved (start.bm).

  python3 video/03-sound/record.py [--build-only] [--no-record]

bmhost --tool runs carts/sound/main.lua on a clean SD that holds start.bm; the
sound the editor plays goes to a .wav and into the video. verify.py reads the
saved bank (scripts/bmaudio.py) and checks that the sound is not silent.
Needs build/host/bmhost-bin and build/sound.bm (make bmhost build/sound.bm).
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))

from bmvideo import (Script, call, encode, write_ass, FPS, write_storyboard, write_script,   # noqa: E402
                     card_cmd, hook_cmd, concat_cmd)

OUT = os.path.join(HERE, "out")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
SOUND = os.path.join(ROOT, "build", "sound.bm")
SD = os.path.join(OUT, "sd")
TITLE = "SKYVALE WORLD  ·  Episode 3: bm Sound"
SUBTITLE = "bm · the bare-metal fantasy console"

# an original tune for the first level: drums, a bass line and a chip melody
RIFF_EXAMPLE_LEN = len('s "kick*4, ~ snare, hat*8"')     # the line the dialog starts with
RIFF = ('stack(s "kick*2, ~ snare, hat*8", note "c2 c2 g1 g1 a1 a1 f1 f1" :s "acid", '
        'n "0 2 4 7 4 2 [1 2] ~" :scale("C:major") :s "chip")')
NAMES = ["JUMP", "COIN", "STOMP"]


class Piano:
    """The piano keys of bm Sound: z s x d c v g b h n j m (C..B of the octave),
    q 2 w 3 e r 5 t 6 y 7 u (the next octave); "," and "." change the octave (4 at the start)."""
    LOW, HIGH = "zsxdcvgbhnjm", "q2w3er5t6y7u"

    def __init__(self, s):
        self.s, self.octave = s, 4

    def octave_to(self, n):
        while self.octave < n:
            self.s.key(".", "piano octave up", hold=6); self.octave += 1
        while self.octave > n:
            self.s.key(",", "piano octave down", hold=6); self.octave -= 1

    def play(self, semitone, octave, what="a note"):
        self.octave_to(octave)
        self.s.key((self.LOW + self.HIGH)[semitone], what, hold=14)


def build():
    s = Script()
    pn = Piano(s)

    # ------------------------------------------------------------- open
    s.chapter("What is bm Sound")
    s.scene("Dev ▸ bm Sound", [("Ctrl+O", "open a .bm"), ("F1 - F4", "the four pages"), ("Esc", "menu"), ("Ctrl+S", "save")])
    s.say("bm Sound makes the sounds, the sound effects and the music of a cartridge: the AUDIO section of the .bm, the same bank the games play with sfx and music.")
    s.sec(6)
    s.say("Ctrl+O opens the cartridge from episode 2.")
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2)
    s.say("Four pages: F1 the sounds, the instruments; F2 the sound effects; F3 patterns, a step sequencer with eight tracks; F4 the songs.")
    for k, what in (("f1", "sounds"), ("f2", "sound effects"), ("f3", "patterns"), ("f4", "songs")):
        s.key(k, what); s.sec(2.2)

    # ----------------------------------------------------------- sounds
    s.chapter("Sounds: the instruments")
    s.scene("bm Sound ▸ F1 sounds", [("Enter", "rename"), ("Arrows", "choose a value"), ("- =", "value -1 / +1"),
                                   ("_ +", "value -10 / +10"), ("i", "listen"), ("PgUp PgDn", "next sound")])
    s.key("f1", "sounds"); s.sec(1.5)
    s.say("A sound is an instrument: a wave, an envelope and a pitch, then a filter, then room and echo. The first one will be the jump. Enter names it.")
    s.key("up", "to the name"); s.key("enter", "rename"); s.sec(1)
    s.type_text("JUMP    ", "the name", per_key=6); s.key("enter", "done"); s.sec(1.5)
    s.say("Down goes into the values and left or right between the columns. For a jump the pitch starts low and slides up: BEND FROM minus twelve, over a tenth of a second.")
    s.key("down", "first value"); s.key("right"); s.key("right")
    s.key("_", "BEND FROM -12", hold=20); s.key("down"); s.key("+", "BEND TIME +10", hold=20)
    s.sec(0.5)
    s.say("I plays the sound as it is: listen to the slide.")
    for _ in range(2):
        s.key("i", "listen", hold=50)
    s.sec(1)
    s.say("PgDn is the next sound: the coin. Another wave, a pluck, and a name.")
    s.key("pgdn", "next sound"); s.sec(0.5)
    s.key("up"); s.key("up"); s.key("enter", "rename"); s.sec(0.8)
    s.type_text("COIN    ", "the name", per_key=6); s.key("enter", "done"); s.sec(1)
    s.key("down"); s.key("left"); s.key("left")
    for _ in range(7):
        s.key("=", "WAVE +1", hold=8)
    s.sec(0.5)
    s.key("i", "listen", hold=50); s.sec(1)
    s.say("And the third, the stomp on an enemy: noise with a quick drop in pitch.")
    s.key("pgdn", "next sound"); s.sec(0.5)
    s.key("up"); s.key("enter", "rename"); s.sec(0.8)
    s.type_text("STOMP   ", "the name", per_key=6); s.key("enter", "done"); s.sec(1)
    s.key("down")
    for _ in range(3):
        s.key("=", "WAVE +1", hold=8)
    s.key("right"); s.key("right"); s.key("_", "BEND FROM -12", hold=20)
    s.key("down"); s.key("+", "BEND TIME +10", hold=20)
    s.key("i", "listen", hold=50); s.sec(1.5)
    s.shot("sounds")

    # ------------------------------------------------------------- effects
    s.chapter("Sound effects")
    s.scene("bm Sound ▸ F2 sound effects", [("Piano keys", "z s x d c v g b h n j m"), (", .", "piano octave"), ("Space", "play"),
                                          ("Enter", "add / remove a step"), ("up, Enter", "name the effect")])
    s.say("F2 is for sound effects: up to 32 steps, each a note of a sound. The keyboard is a piano: the lower letters are one octave, the upper row the next.")
    s.key("pgup", "previous sound", hold=3); s.key("pgup", "previous sound", hold=3)
    s.key("f2", "sound effects"); s.sec(2)
    s.say("The jump: four notes going up, played with the jump sound. Each key places a step and moves on.")
    for sem, octv in ((0, 4), (4, 4), (7, 4), (0, 5), (4, 5)):
        pn.play(sem, octv, "jump note")
    s.sec(0.5)
    s.key("space", "play", hold=70); s.sec(0.8)
    s.say("Up selects the header of the effect: Enter names it, and with minus or underscore the speed changes: a step lasts 60 milliseconds, here faster.")
    s.key("up"); s.key("enter", "name it"); s.sec(0.8)
    s.type_text("JUMP    ", "the name", per_key=6); s.key("enter", "done"); s.sec(1)
    s.key("right", "next field"); s.key("_", "faster: -10 ms", hold=8); s.sec(0.5)
    s.key("down", "back to the steps"); s.sec(0.5)
    s.key("space", "play", hold=70); s.sec(1)

    s.say("PgDn is the next effect: the coin, two high notes. The sound for new steps is the one chosen on F1.")
    s.key("pgdn", "next effect"); s.sec(0.5)
    s.key("f1", "sounds"); s.key("pgdn", "next sound", hold=3); s.key("f2", "sound effects"); s.sec(0.5)
    for _ in range(5):
        s.key("left", "back to the first step", hold=3)       # the cursor stays on step 6 of the jump
    pn.play(11, 5, "coin note"); pn.play(4, 6, "coin note")
    s.key("up"); s.key("enter", "name it"); s.sec(0.8)
    s.type_text("COIN    ", "the name", per_key=6); s.key("enter", "done"); s.key("down"); s.sec(0.5)
    s.key("space", "play", hold=70); s.sec(1)

    s.say("And the stomp: three notes falling.")
    s.key("pgdn", "next effect"); s.sec(0.5)
    s.key("f1", "sounds"); s.key("pgdn", "next sound", hold=3); s.key("f2", "sound effects"); s.sec(0.5)
    for _ in range(2):
        s.key("left", "back to the first step", hold=3)
    pn.play(7, 3, "stomp note"); pn.play(2, 3, "stomp note"); pn.play(0, 3, "stomp note")
    s.key("up"); s.key("enter", "name it"); s.sec(0.8)
    s.type_text("STOMP   ", "the name", per_key=6); s.key("enter", "done"); s.key("down"); s.sec(0.5)
    s.key("space", "play", hold=70); s.sec(1.5)
    s.shot("sfx")

    # --------------------------------------------------------------- music
    s.chapter("Music with riff")
    s.scene("bm Sound ▸ F7 riff", [("F7", "riff: a pattern in a line"), ("Enter", "play it"), ("Ctrl+Enter", "put it in the bank"),
                                  ("Esc", "close")])
    s.say("For the music, F7 opens riff: a pattern written in one line, in the style of live-coding music. Drums, a bass, a melody with the chip instrument.")
    s.key("f7", "riff"); s.sec(2)
    for _ in range(RIFF_EXAMPLE_LEN):
        s.key("bksp", "clear the example", hold=1)
    s.sec(0.5)
    s.type_text(RIFF, "the pattern", per_key=2)
    s.sec(0.8)
    s.say("Enter plays it live: the words light up as the notes sound.")
    s.key("enter", "play", hold=3); s.sec(14)
    s.say("Ctrl+Enter puts four bars of it into the bank as a song, with its patterns.")
    s.key("ctrl+enter", "four bars into the bank", hold=6); s.sec(2)
    s.shot("song")

    # -------------------------------------------------------------- patterns, song
    s.chapter("Patterns and the song")
    s.scene("bm Sound ▸ F3 patterns / F4 songs", [("F3", "patterns"), ("F4", "songs"), ("Space", "play / stop"),
                                                ("PgUp PgDn", "next pattern / song")])
    s.say("The patterns are the steps on eight tracks, one voice each: riff wrote them, and they can be edited by hand. The song orders them.")
    s.key("f3", "patterns"); s.sec(4)
    s.key("f4", "songs"); s.sec(2)
    s.say("Space plays the song from the bank, the way a game plays it with music.")
    s.key("space", "play", hold=3); s.sec(16)
    s.key("space", "stop", hold=3); s.sec(1)

    # ---------------------------------------------------------------- save
    s.chapter("Save")
    s.scene("bm Sound ▸ save", [("Ctrl+S", "save"), ("F5", "try the game"), ("Ctrl+E", "export to a game")])
    s.say("Ctrl+S saves the bank into the .bm. In a game: sfx for the effects, music for the song. Next time: bm Code, to make Kip move.")
    s.chord("s", "save"); s.sec(3)
    s.end()
    return s


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 3  ·  bm Sound", 56, "0xe0e4f0", 520),
         ("Effects and music for the first level", 34, "0x9098b0", 640)]
OUTRO = [("Next: Episode 4  ·  bm Code", 56, "0xffc050", 400),
         ("Kip moves: running, jumping and the map", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
HOOK = "TODAY: SOUNDS, EFFECTS AND MUSIC"


def main():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    total = s.t + 30
    hook_s, intro_s, outro_s = 8, 4, 5
    offset = (hook_s + intro_s) * FPS
    s.write_input(os.path.join(OUT, "input.txt"))
    write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
    desc = ("Skyvale World is a 2D platformer built only with the tools inside the bm console. "
            "In this episode, bm Sound: the jump, coin and stomp sounds, sound effects, and the music of the first level "
            "written with riff.\n\nbm: https://github.com/f-accomando/bm")
    write_storyboard(s, os.path.join(HERE, "storyboard.md"), TITLE, offset)
    write_script(s, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, offset, desc)
    if "--build-only" in sys.argv:
        print("total %.0f s" % (total / FPS))
        return
    main_mp4 = os.path.join(OUT, "main.mp4")
    wav = os.path.join(OUT, "sound.wav")
    if "--no-record" not in sys.argv:
        shutil.rmtree(SD, ignore_errors=True)
        os.makedirs(os.path.join(SD, "carts"))
        shutil.copy(os.path.join(HERE, "start.bm"), os.path.join(SD, "carts", "SKYVALE.BM"))
        raw = os.path.join(OUT, "raw.rgb")
        call([BMHOST, SOUND, "--tool", "--sd", SD, "--seconds", str(total / FPS), "--input",
              os.path.join(OUT, "input.txt"), "--video", raw, "--wav", wav, "--quiet"])
        call(encode(raw, os.path.join(OUT, "overlay.ass"), main_mp4, audio=wav))
        os.remove(raw)
    # the result up front: the song playing
    at = next(f for f, pl, _ in s.scenes if "F3 patterns" in pl) / FPS + 5
    for cmd in (hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, hook_s, HOOK),
                card_cmd(os.path.join(OUT, "intro.mp4"), intro_s, INTRO, silent=True),
                card_cmd(os.path.join(OUT, "outro.mp4"), outro_s, OUTRO, silent=True)):
        call(cmd)
    call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                    os.path.join(OUT, "ep03.mp4"), audio=True))


if __name__ == "__main__":
    main()
