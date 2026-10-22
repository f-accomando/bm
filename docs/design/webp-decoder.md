# WebP decoder (`src/bm/webp.c`)

The decoder reads WebP pictures and writes RGB565. It serves the web browser cart
([web-browser-cart.md](web-browser-cart.md)) and every cart that loads pictures. The API for
Lua (`image_load`) does not exist yet.

## API (`src/bm/webp.h`)

| Function | Use |
|---|---|
| `webp_is(buf, len)` | 1 if the bytes start with `RIFF`, a size, and `WEBP`. |
| `webp_info(buf, len, &w, &h)` | Reads the size. Returns 0, or -1 if the file is not a readable WebP. |
| `webp_decode565(buf, len, out, stride, maxw, maxh)` | Decodes into `out` (`uint16_t`, `stride` in pixels). The decoder cuts the picture at the top left corner to `maxw` x `maxh`. It writes nothing outside that rectangle. Returns 0 or -1. |

The decoder does not scale. To get a small picture, the caller decodes the whole picture and
shrinks it, or uses `maxw` and `maxh` to cut a part.

## What the decoder reads

- Lossy: one VP8 key frame (RFC 6386). It reads all intra modes, the 1, 2, 4 and 8 token
  partitions, segments, the normal and the simple loop filter. The chroma planes are smoothed
  with the 9/3/3/1 filter of libwebp. The YUV to RGB step uses the BT.601 limited range.
- Lossless: VP8L. It reads the four transforms, the color cache, the entropy image with many
  prefix code groups, and LZ77 copies.
- Container: `RIFF`, then `VP8 `, `VP8L` or `VP8X` chunks. A `VP8X` chunk with the animation
  flag is refused. The decoder ignores alpha (`ALPH` chunks and the alpha of VP8L).
- Refused: inter frames, a bad start code, a size of zero, a truncated file, a code tree that is
  not full, a copy that points outside the picture.

## Memory

The decoder makes its allocations with `malloc` and frees all of them before it returns.

| Kind | Size |
|---|---|
| Lossy | The three planes, rounded up to whole 16x16 blocks (1.5 bytes a pixel), plus 4 bytes a block, plus about 1 KB. |
| Lossless | 4 bytes a pixel, plus the code tables (at most `WEBP_LL_TABLE_MAX`, 4 MB), plus the transform images. |

`WEBP_MAX_PIXELS` (8 M pixels by default) is the largest picture. A bigger picture is refused.
Both limits are macros: define them before `webp.h` to change them. The lossless decoder accepts
at most 4096 prefix code groups.

## Tests

`make test-webp` builds `tests/webp/test_webp.c` with the address and undefined behaviour
sanitizers and runs `tests/webp/run_webp_test.py`.

- Fixtures (`tests/webp/fixtures/*.webp` with the libwebp decode in `*.ref.z`): pictures from
  libwebp (through Pillow), from libvpx (token partitions, simple filter) and from the ffmpeg test
  sources. Lossless pictures must match libwebp to the bit after the cut to RGB565. Lossy pictures
  must match to a mean error under 1 level of 255 (the largest error seen is 16).
- With Pillow and WebP support, the test also makes new pictures (sizes from 1x1 to 320x240,
  qualities, palettes of 2 to 200 colors, alpha).
- Each fixture goes through a crop test (a cut with a wide stride, guard words around it) and a
  fuzz test (truncations, flipped bits, damaged headers). The sanitizers find any read or write
  outside the buffers.
- `python3 tests/webp/run_webp_test.py build/host/test_webp --regen` writes the fixtures again
  (needs Pillow, and `ffmpeg` with libvpx for some of them).

## Not done

- Speed on the Pi Zero W: not measured. The lossless prefix decoder reads one bit at a time.
- A decode with a scale factor (to save memory on big pictures).
- Alpha, animation, color profiles, VP8 inter frames.
- The Lua call `image_load` and the use in the browser cart.
