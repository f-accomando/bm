-- The console's rules for the files the tools write (src/bm/project.h,
-- write_target in runtime.c), for the stand-ins of the PC's tests: a game
-- (.bm, .b16) is read only, so a write to one goes into its editable copy
-- (NAME.BME next to it, or NAME1.BME..., the question answered yes), and a
-- new .bm becomes a project (.BME).
--   target(path, exists, copy) -> the path to write, and true when it is
--   not the one asked (cart_write's second result)
-- exists(path) says whether the file is on the card; copy(from, to) copies it.
return function(path, exists, copy)
  local low = path:lower()
  if not (low:match("%.bm$") or low:match("%.b16$")) then return path, false end
  if not exists(path) then
    return (path:gsub("%.[^.]*$", ".BME")), true
  end
  local dir, name = path:match("^(.*)/([^/]+)$")
  local stem = name:gsub("%.[^.]*$", ""):upper():gsub("[^%w_%-]", "")
  if stem == "" then stem = "PROJECT" end
  stem = stem:sub(1, 8)
  local to
  for k = 0, 9 do
    to = dir .. "/" .. (k == 0 and stem or stem:sub(1, 7) .. k) .. ".BME"
    if not exists(to) then break end
  end
  copy(path, to)
  return to, true
end
