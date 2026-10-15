# AGY-2 — API-LINT: controllo dei nomi API sul PC + guide verificate (APP-5 lint, BETA-7)
Inizia dopo: DELIVERED AGY-1.
Base: branch agy/api-lint da origin/agy/api-source (serve src/ai/api.tsv). Mai push su bm-core.
Perimetro: scripts/api_lint.py (nuovo), scripts/api_lint_allow.txt (nuovo), tests/tools/test_api_lint.py (nuovo), docs/GAME-GUIDE.md, docs/GUIDA-GIOCHI.md (solo righe di codice Lua segnalate). Sola lettura: src/ai/api.tsv, src/script/*.lua. NON toccare: scripts/mkapi.py, src/ai/api.tsv, carts/editor/*, carts/sdk_b16/*, docs/api/*.
Obiettivo: uno script che segnala le chiamate a nomi globali inesistenti nel Lua dei giochi e delle guide, e le due guide corrette finché lo script passa.

## AGY-2a — lint (bm-coder, 40 min)
- scripts/api_lint.py FILE...: per i .lua analizza tutto il testo; per i .md solo i blocchi ```lua. Prima toglie i commenti (-- fino a fine riga, --[[ ]]) e le stringhe ("..", '..', [[..]]).
- Chiamate: (?<![\w.:])([A-Za-z_]\w*)\s*\( .
- Nomi noti = nomi globali di src/ai/api.tsv ∪ primo segmento dei nomi con "." o ":" ∪ LUA_STD {assert error ipairs pairs next pcall xpcall select tonumber tostring type unpack rawget rawset rawequal rawlen setmetatable getmetatable require collectgarbage print string table math coroutine utf8} ∪ parole chiave {function if while until return and or not elseif in local} ∪ nomi definiti nello stesso file (per un .md: in tutti i suoi blocchi lua) con "function N", "local function N", "local N[, M] =", "N = function", parametri di "function x(a, b)", variabili di "for N[, M] in" ∪ righe di scripts/api_lint_allow.txt (un nome per riga, "# motivo" obbligatorio, file inizialmente con la sola intestazione).
- Output: "<file>:<riga>: unknown API name '<N>'"; exit 1 se c'è almeno una segnalazione, 0 se nessuna, 2 per file non leggibile.
- tests/tools/test_api_lint.py: fixture con un nome sbagliato (exit 1, riga giusta), un nome API valido, una funzione locale, una chiamata di metodo obj:m( (nessuna segnalazione), una stringa che contiene "foo(" (nessuna segnalazione).
- Verifica: python tests/tools/test_api_lint.py → OK.

## AGY-2b — guide verificate sul codice (bm-light, 30-45 min)
- Lanciare python scripts/api_lint.py docs/GAME-GUIDE.md docs/GUIDA-GIOCHI.md. Per ogni segnalazione si applica una regola fissa:
  (a) nome assente da docs/api, da api[] di runtime.c e da src/script/*.lua → errore della guida: correggerlo con il nome documentato in docs/api, nella stessa riga di entrambe le guide;
  (b) nome definito in src/script/*.lua o falso positivo → aggiungerlo in api_lint_allow.txt con "# motivo".
- Nessun'altra modifica alla prosa delle guide.
- Verifica: python scripts/api_lint.py docs/GAME-GUIDE.md docs/GUIDA-GIOCHI.md → exit 0; python scripts/docs_audit.py score senza link rotti.
Criterio di fine: lint 0 sulle due guide; test OK; il DELIVERED elenca le correzioni (a) e le voci (b).
Consegna: riga DELIVERED standard, poi FREE.
