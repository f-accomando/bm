# API: pure jobs (worker cores)

Part of the game API. Index and basics: [API.md](../API.md).

A job is a function of the kernel. You post it by name with a string of bytes. You read the answer later, in the next frame or after.
On the RGB30 with `cores=4` the job runs on a worker core while the game goes on. On the Pi Zero W, on the host and on a console without workers, the job runs at once on core 0. The answer is the same in all cases. A lockstep game can use jobs.
A game cannot add a job. A worker never runs Lua and never touches the Lua memory.

## Functions

| Function | What it does |
|---|---|
| `job_run(name, data)` | Posts the job `name` with the string `data` (at most 4608 bytes). Returns the id, or `nil` and a reason: `"no such job"`, `"the data is too long"`, `"too many jobs"` (8 at a time). |
| `job_poll(id)` | Returns `"run"` while the job works. Returns `"done"` and the answer string when it is over. Returns `"fail"` when the data was not right. Returns `"none"` for an id that is wrong or already read. The call that says `"done"` or `"fail"` frees the job: read the answer once. |
| `job_cancel(id)` | The answer is not wanted. The job is freed. |
| `job_info()` | Returns `workers, ran`: the worker cores that can take a job (0: a job runs at once on core 0) and the jobs that ran on a worker since the start. |

The jobs of a game end when the game ends.

## The jobs

All the numbers are bytes. Make the data with `string.pack("BBBBBBB", ...)` and read the answer with `string.byte` or `string.unpack`.

| Job | Data | Answer |
|---|---|---|
| `grid_path` | `w h sx sy gx gy flags`, then `w * h` cells. `w` and `h` are 1 to 64. Flags bit 0: eight directions (a diagonal does not cut the corner of a blocked cell). A cell is 0 (free), 255 (blocked) or 1 to 254 (free, with that many tenths of a step added to each step into it). | `found` (1 or 0), `n` (two bytes, little endian), then `n` pairs `x y` from the start to the goal. With `found` 0 `n` is 0. The path is the cheapest one (A\*). A step costs 10, a diagonal 14. |
| `grid_flood` | `w h gx gy flags`, then `w * h` cells (255 is blocked, any other value is free). | `w * h` bytes: the steps from the goal (at most 254). 255 is a blocked cell or a cell that the goal does not reach. An agent steps to its neighbour with the smallest number (a flow field for a crowd). |

## Example

```lua
local W, H = 16, 12
local cells = {}
for i = 1, W * H do cells[i] = string.char(solid(i) and 255 or 0) end
local data = string.pack("BBBBBBB", W, H, 1, 1, 14, 10, 1) .. table.concat(cells)
local id = job_run("grid_path", data)        -- in _update

-- some frames later
local state, path = job_poll(id)
if state == "done" and path:byte(1) == 1 then
  local n = path:byte(2) + 256 * path:byte(3)
  for i = 0, n - 1 do
    local x, y = path:byte(4 + 2 * i), path:byte(5 + 2 * i)
    -- the cell (x, y) of the path
  end
end
```

## Rules

- Read the answer a frame later. Do not poll in a loop until it is done: on the Pi Zero W the job has run before `job_run` returns, on a worker it takes some microseconds.
- Eight jobs at most are open at the same time. Free a job with `job_poll` or `job_cancel`.
- Check `job_info()` only for a report. Do not change the game for the number of workers.
