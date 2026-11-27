# bm C: C for NES projects (Shell SDK)

bm C is the C compiler of the Shell SDK. It runs on the console. It turns `main.c` into 6502 assembly, and the assembler builds the `.nes` ROM. The code is original: `core/src/kernel/shell_sdk_cc*.c`. The test is `make test-sdk-cc`.

## Use it

1. Open Shell SDK. In the Project tab, set **Language** to **C (bm C)**. Do this before you change the start file or give the project a name.
2. Press F2. The start file shows a blue screen and a ball that the pad moves.
3. Press F6 to check the code. It writes no file. A mistake moves the cursor to its line.
4. Press F7 to see the assembly beside the code. Each part says its line of C. Ctrl+W closes it.
5. Press Ctrl+B to build `/nes/NAME.nes`. Press Ctrl+R to build and run.

The message after F6 and Ctrl+B gives the ROM bytes and the RAM bytes of the program.

## The shape of a program

```c
byte x = 124;                      // a global with its first value

void start(void)                   // optional: runs once
{
    background(BLUE);
    sprite_palette(0, WHITE, SKY, NAVY);
}

void update(void)                  // runs once every frame
{
    if (held(RIGHT)) x += 1;
    sprite(0, x, 112, 1);
}
```

Write `update()`, or write `main()` and call `wait_frame()` in your own loop. Do not write both.

## Types

| Type | Bytes | Values | Same as |
|---|---|---|---|
| `byte` | 1 | 0 to 255 | `char`, `unsigned char`, `bool` |
| `sbyte` | 1 | -128 to 127 | `signed char` |
| `word` | 2 | 0 to 65535 | `unsigned`, `unsigned int` |
| `int` | 2 | -32768 to 32767 | `short` |

- Pointers (`byte *p`, up to two `*`) and arrays of one size (`byte a[16]`).
- `const` arrays stay in the ROM. A `const` number takes no memory.
- `#define NAME value` gives a name to a number.

## Statements and operators

`if`, `else`, `while`, `do`, `for`, `break`, `continue`, `return`, blocks, and the operators of C (also `?:`, `++`, `--`, `+=` and the others).

## Rules that differ from C

- An operation uses 16 bits when one side or the destination is 16 bits. Else it uses 8 bits and wraps. Example: `word w = a + b;` keeps the sum of two bytes; `byte c = a + b;` wraps.
- Every variable has a fixed place. A function cannot call itself.
- A local variable with no first value keeps its last value.
- A comparison of a signed value with an unsigned value compares the real numbers.
- `/` by 0 gives 65535.
- `*`, `/` and `%` call a routine and take hundreds of CPU cycles. A power of two is fast (`x * 8`, `w / 16`).
- Before `a[i] += 1`, put an index that changes (`i++`, a call) in a variable.

Not in bm C: `float`, `long`, `struct`, `union`, `enum`, `typedef`, `switch`, `goto`, `sizeof`, `#include`, strings (only in `asm`). The compiler says what to write instead.

## The library

| Call | What it does |
|---|---|
| `wait_frame()` | waits for the next frame and reads the pad (only with `main()`) |
| `held(button)` | not 0 while the button is down |
| `pressed(button)` | not 0 in the frame the button goes down |
| `background(colour)` | the colour behind everything |
| `sprite_palette(n, c1, c2, c3)` | the three colours of the sprite palette n (0 to 3) |
| `background_palette(n, c1, c2, c3)` | the same for the background |
| `sprite(n, x, y, tile)` | sprite n (0 to 63) shows a tile at x, y |
| `sprite_style(n, style)` | palette 0 to 3, plus `FLIP_X`, `FLIP_Y`, `BEHIND` |
| `hide_sprite(n)` | takes sprite n off the screen |
| `random()` | a byte from a pseudo-random sequence |
| `peek(address)`, `poke(address, value)` | reads or writes one byte of the address space |

- Variables of the machine: `frame` (counts the frames), `pad`, `pad_new`.
- Buttons: `A B SELECT START UP DOWN LEFT RIGHT`.
- Colours: `BLACK DARK_GRAY GRAY WHITE RED ORANGE YELLOW GREEN LIME CYAN SKY BLUE NAVY PURPLE PINK BROWN`, or a NES colour number.
- A sprite and a palette change show at the next frame.

## Assembly and tiles

```c
asm("lda {x}", "asl a", "sta {x}");     // {x} is the place of x
```

```c
tiles {
    blank(1),                            // one empty tile
    0x3C,0x72,0xE1,0xC1,0x81,0x83,0x46,0x3C,0x3C,0x4E,0x9F,0xBF,0xFF,0xFF,0x7E,0x3C,
}
```

The Sprite tab writes the `tiles` block between `//@tiles` and `//@tiles-end`. Keep these two lines. The sprites take the tiles `$000` to `$0FF`. The background takes the tiles `$100` to `$1FF`, and the whole background shows the tile `$100`.

## Memory

| Place | Use |
|---|---|
| `$0000-$001F` | the runtime |
| `$0020-$00FF` | single variables of the program |
| `$0200-$02FF` | sprites |
| `$0300-$031F` | palettes |
| `$0320-$07FF` | arrays and the variables that do not fit above |
