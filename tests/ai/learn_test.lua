-- Tests on-device INT8 head fine-tuning (ai.learn) and BMAI patching (ai.save).

local entries = ai.list()
assert(#entries > 10, "expected entries in knowledge base")

local target = entries[math.floor(#entries / 2)].id
local query = "blargonauta qwerty zanzara"

local ok = ai.learn(query, target, 16)
assert(ok == true, "ai.learn failed")

local hits = ai.ask(query, { n = 3 })
assert(#hits >= 1, "expected at least 1 hit after ai.learn")
assert(hits[1].id == target, "expected learned entry " .. target .. " at #1, got " .. tostring(hits[1].id))

local bad, err = ai.learn("hello", "no_such_entry_id_xyz")
assert(not bad and err == -2, "expected error -2 for unknown entry")

local tmp = "build/test_learn_assist.bin"
local saved = ai.save(tmp)
assert(saved == true, "ai.save failed")

local f = assert(io.open(tmp, "rb"), "saved BMAI file not readable")
local data = f:read("*a")
f:close()
os.remove(tmp)

assert(#data > 1024, "saved BMAI too small: " .. #data)
assert(data:sub(1, 4) == "BMAI", "bad BMAI magic")

print("learn_test OK: learned " .. target .. " (" .. #data .. " bytes)")
