-- The texts of the benchmark of the word completion: 100 characters each
-- (100 keys on a keyboard, a capital with Shift counting as one), in code
-- page 437 like the console's.
return {
  { name = "italiano", lang = "it",
    text = "Ciao Marco, domani sera giochiamo da me? La console nuova \138 arrivata: tu porta la pizza e le bibite!" },
  { name = "english", lang = "en",
    text = "Hi Mark, do we play at my place tomorrow night? The new console is here: you bring pizza and drinks!" },
  { name = "lua", lang = "lua",
    text = "function _update()\n  if btn(0) then x = x - 2 end\n  if btn(1) then x = x + 2 end\n  t = t + 0.125\nend" },
}
