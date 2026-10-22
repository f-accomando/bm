# API: Video clips

Part of the game API. Index and basics: [API.md](../API.md). The file format and the tool that makes clips: [BM-VIDEO.md](../BM-VIDEO.md).

### Video clips (.bmv)

A clip is a `.bmv` file in `bm/clips/` on the SD card. The console reads the whole file into memory (48 MiB at most) and decodes one frame at a time. The frames are baseline JPEG, so a frame costs decode time: measure it on the console (the Video bench in the Dev tab). The clip's audio is not played yet.

| Function | Description |
|---|---|
| `video_open(name)` | opens `bm/clips/<name>` (a file name, no path), or `bm/rec/<file>` for a recording when `name` starts with `"rec/"` (`"rec/R0010001.BMV"`). Returns a clip, or `nil` and a message ("no such clip", "the clip is over 48 MiB", "not a .bmv clip" ...). The clip is freed when it is closed or when the game ends |
| `clip:info()` | `width, height, fps, frames, milliseconds, has_audio` |
| `clip:frame_at(ms)` | the frame number (from 0) to show `ms` milliseconds after the start. After the end it is the last frame |
| `clip:draw(frame, x, y, [scale])` | decodes frame `frame` (from 0) to RGB565 and copies it, opaque, with its top left corner at (x, y); `scale` 1 to 8 makes each pixel `scale` times bigger. The camera and the clip rectangle apply. The same frame again costs only the copy. Returns `true`, or `false` and a message if the frame cannot be decoded. A frame number outside the clip is an error |
| `clip:close()` | frees the clip. Any other call on it is then an error |

```lua
local clip = video_open("intro.bmv")
local t0 = time()
function _draw()
  cls(0)
  local f = clip:frame_at((time() - t0) * 1000)
  clip:draw(f, 0, 0)
end
```
