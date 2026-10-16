# AGY-14 - APP5B-FIXTURE: la fixture del lint (`tests/predict/lint_sample.lua`) prende le 4 mutazioni che oggi lascia passare (APP-5b)
Assegnato ad AGY (16 ott), indipendente da AGY-15. Nasce dal revisore di APP-5b: 4 mutazioni di `src/ai/predict.lua` passano `make test-predict` con 33 check, 0 falliti.
Base: `origin/claude/app5-api-help-lint` @ `d2987835` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/app5b-lint-fixture <cartella> origin/claude/app5-api-help-lint`); branch `claude/app5b-lint-fixture`. Mai push su bm-core; push solo di questo branch.
Perimetro: `tests/predict/lint_sample.lua` (9 righe aggiunte in fondo) e `tests/predict/predict_test.lua` (6 righe aggiunte, riga 87 di oggi: subito PRIMA di `check(#P.lint({ "zz()" }`).
NON toccare: `src/ai/predict.lua` (le mutazioni si applicano solo per la prova e si annullano con `git checkout -- src/ai/predict.lua`), `scripts/*` (anche `api_lint_allow.txt`), `carts/*`, `Makefile`, `docs/*`, `SYNC.md`, `sync/*`, e le righe esistenti dei due file (nessun check si toglie o si allenta).
Vincoli: solo host (`not verified on hardware`). Ogni `file:riga` qui sotto è stato aperto dal planner su `d2987835`; l'operatore lo riscontra con `grep -n`; ciò che non riscontra lo scrive `non verificato`.
Obiettivo: `make test-predict` resta verde (37 check, 0 falliti) e ciascuna delle 4 mutazioni M2, M4, M7, M8 lo fa fallire.

## Stato reale (verificato dal planner: ho eseguito tutto su un worktree temporaneo di `d2987835`, in WSL)
- Prima: `predict: 33 checks, 0 failed`. La fixture ha 10 righe (`sprt(1, 2, 3)` ... `math.floor(1.5)`) e un solo check la guarda (`predict_test.lua:86`: `#hits == 1 and hits[1].line == 1 and hits[1].name == "sprt"`).
- Perché le 4 mutazioni passano oggi:
  - M2 (stringhe non cancellate): l'unica stringa con una chiamata è `print("foo(")` e `foo` è definita nella riga 3, quindi non viene mai segnalata. Serve un nome SCONOSCIUTO dentro una stringa.
  - M4 (lista `allow` ignorata): nella fixture nessun nome dipende da `scripts/api_lint_allow.txt` (contiene `hurt` e `danno`, righe 3-4; `hurt` NON è in `src/ai/api.tsv`, `grep -n hurt src/ai/api.tsv` non dà righe).
  - M7 (`local N` non definisce): l'unico `local` della fixture è `local function foo`, già coperto dalla regola `function N`; `local s = [[ baz( ]]` non chiama `s`. Serve `local a, b = ...` e poi `a()` / `b()`.
  - M8 (commenti `--[[ ]]` non cancellati): `-- bar(1)` è un commento di riga; la sua parte `--` basta comunque a cancellarlo. Serve un blocco su PIÙ righe, con la chiamata sulla seconda riga.
- Le righe nuove vanno in FONDO: la riga 1 (`sprt`) non si sposta, quindi il check esistente di riga 86 resta vero e non si tocca.
- CRLF: le copie di lavoro su Windows hanno i fine riga CRLF (`file tests/predict/lint_sample.lua` dice "CRLF"). Le righe nuove devono avere gli stessi fine riga; `git diff --stat` deve dire `tests/predict/lint_sample.lua | 9 +` e `tests/predict/predict_test.lua | 6 +`. Se i numeri sono molto più grandi: `git checkout -- <file>` e rifare mantenendo i fine riga.

## AGY-14a - fixture e check (bm-coder, 15 min)
Passo 1: `tests/predict/lint_sample.lua`. Dopo l'ultima riga (`math.floor(1.5)`) aggiungere ESATTAMENTE queste 9 righe, nell'ordine, senza righe vuote e senza spazi finali (il file finisce con un a-capo):
```
print("zzstr(1)")
print('zzsq(1)')
hurt(1)
local lva, lvb = 1, 2
lva(1)
lvb(2)
--[[
blockcall(1)
]]
```
(righe 11-19. `zzstr` e `zzsq` sono nomi sconosciuti dentro stringhe `"` e `'`; `hurt` è noto solo per `api_lint_allow.txt`; `lva`/`lvb` solo per `local a, b`; `blockcall` sta sulla riga 18, dentro un blocco che comincia alla 17.)

Passo 2: `tests/predict/predict_test.lua`. Subito PRIMA della riga (riscontrare con `grep -n 'check(#P.lint({ "zz()" }' tests/predict/predict_test.lua`: una sola riga, la 87)
```
check(#P.lint({ "zz()" }, { defined = { zz = true } }) == 0, "lint: a name given as defined is known")
```
inserire ESATTAMENTE queste 6 righe (nessun rientro):
```
local seen = {}
for _, h in ipairs(hits) do seen[h.name] = h.line end
check(seen.zzstr == nil and seen.zzsq == nil, "lint: a name inside a string is not a call")
check(seen.hurt == nil and P.api("hurt") == nil, "lint: a name of scripts/api_lint_allow.txt is known")
check(seen.lva == nil and seen.lvb == nil, "lint: local a, b defines both names")
check(seen.blockcall == nil, "lint: a --[[ ]] block comment is not code")
```
(`hits` è definita alla riga 85 e `seen` non esiste già nel file: `grep -n "seen" tests/predict/predict_test.lua` prima di inserire deve dare zero righe, altrimenti BLOCKED.)

Passo 3: verifica verde. Dalla radice del TUO worktree (lo script prende la radice dalla propria cartella):
`powershell -File scripts/wsl-run.ps1 "make test-predict; echo EXIT=\$?" -Tail 120`
- Atteso: ultime righe `predict: 37 checks, 0 failed` e `EXIT=0`. La prima volta compila `build/host/luahost` (1-3 min); poi ~20 s.
- Ambiente: `/tmp` di WSL è un tmpfs condiviso da 3,9 GB che si riempie (altri processi QEMU). Prima: `wsl -e bash -c "df -h /tmp | tail -1"`. Se Use% è 100%: `TMPDIR=\$PWD/build/tmp` come in AGX-1, `mkdir -p build/tmp` prima. Non cancellare nulla in `/tmp` che non hai creato.

## AGY-14b - NON incluso
Il marcatore `t.lint_lines` (`carts/code/main.lua:740-741`, letto a :1545) non viene azzerato quando una scheda viene modificata: si ricalcola solo in `lint_tab` (:729), chiamata dopo il salvataggio (:800, :809). Il punto naturale è `snapshot` (:174, che mette `t.dirty = true` a :179), ma `undo/redo` (:187) mette `dirty` per conto proprio e `snapshot` esce prima di :179 quando `kind` e riga sono gli stessi: servono due o tre punti e un giudizio su quali, e non c'è un test su host per `carts/code`. Non è "una riga evidente": lasciato fuori, va al planner come WO separato (non verificato su hardware né su host).

## AGY-14c - prova delle 4 mutazioni (bm-coder, 15 min)
Creare `build/mut.py` (la cartella `build/` è ignorata da git: `git check-ignore build/words.lua` stampa il percorso) con questo contenuto, ESATTO:
```
import sys
M = {
  'M2': ('local s = string.find(text, "[%-\\"\'%[]", i)', 'local s = string.find(text, "[%-%[]", i)'),
  'M4': ('  for name in pairs(dict("lua").allow) do known[name] = true end\r\n', ''),
  'M7': ('scan_word(text, "local", function', 'scan_word(text, "localX", function'),
  'M8': ('text:match("^%-%-%[(=*)%[()", s)', 'text:match("^%-%-Q(=*)Q()", s)'),
}
a, b = M[sys.argv[1]]
p = 'src/ai/predict.lua'
s = open(p, 'rb').read().decode()
assert s.count(a) == 1, (sys.argv[1], s.count(a))
open(p, 'wb').write(s.replace(a, b).encode())
```
(Cosa fa ciascuna: M2 toglie `"` e `'` dalla ricerca di `strip_code`; M4 cancella la riga che carica `allow` in `P.lint`; M7 rinomina la parola cercata dalla regola `local` di `defined_of`; M8 fa sì che `--[[` non sia riconosciuto come blocco. Se l'`assert` scatta, il file ha fine riga LF invece di CRLF: per M4 sostituire `\r\n` con `\n` nella prima stringa, e scriverlo nella consegna.)

Per ognuna di M2, M4, M7, M8, in ordine (da root del worktree):
1. `python build/mut.py <M>` e `git diff --stat src/ai/predict.lua` deve dire `1 file changed` con 1 riga (M4: `1 deletion`, le altre `1 insertion(+), 1 deletion(-)`).
2. `powershell -File scripts/wsl-run.ps1 "make test-predict; echo EXIT=\$?" -Tail 120`
3. Atteso, esattamente queste due righe `FAIL` + il riepilogo + `EXIT=2`:
| Mutazione | righe `FAIL` attese | riepilogo |
|---|---|---|
| M2 | `lint: the sample gives one report, sprt on line 1` e `lint: a name inside a string is not a call` | `predict: 37 checks, 2 failed` |
| M4 | `lint: the sample gives one report, sprt on line 1` e `lint: a name of scripts/api_lint_allow.txt is known` | `predict: 37 checks, 2 failed` |
| M7 | `lint: the sample gives one report, sprt on line 1` e `lint: local a, b defines both names` | `predict: 37 checks, 2 failed` |
| M8 | `lint: the sample gives one report, sprt on line 1` e `lint: a --[[ ]] block comment is not code` | `predict: 37 checks, 2 failed` |
4. `git checkout -- src/ai/predict.lua` SUBITO dopo ogni prova (prima della mutazione successiva). A fine prova `git status --short` deve elencare solo i due file di test.
Se una mutazione NON fa fallire il suo check: non cambiare la fixture a tentativi; riportare la mutazione, l'uscita intera e `BLOCKED`.

Criterio di fine: verde a 37 check (passo 3) e le 4 mutazioni rosse come in tabella; `git diff --stat` finale: 2 file, 15 righe aggiunte (9 + 6), 0 tolte; `src/ai/predict.lua` senza modifiche.
Commit: un solo commit sul branch `claude/app5b-lint-fixture`, messaggio `tests: lint fixture catches strings, allow list, local N and block comments`. Push solo di questo branch.
Consegna: `DELIVERED AGY-14 @ claude/app5b-lint-fixture@<commit> · verifica: make test-predict -> 37 checks 0 failed; mutazioni M2 M4 M7 M8 -> 2 failed ciascuna · 2 file, 15 righe · non verificato su hardware · <ISO>` in `## Consegne` di `sync/AGY.md`, poi FREE. Agente consigliato: bm-coder (testo esatto); le decisioni, se serve, vanno a SPV.
Stima: 15 minuti di lavoro, 5 di compilazione la prima volta, ~20 s per ogni esecuzione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Aggiungere i casi in fondo alla fixture (la riga 1 resta `sprt`) e un check per mutazione, oltre al check esistente sul numero di segnalazioni. Raccomandata: sì. Alternativa: un solo check sul numero (meno chiaro quando fallisce: dice solo "una segnalazione in più").
- D2 La fixture usa `hurt` come nome noto solo per `api_lint_allow.txt`. Se quel file cambia (la voce `hurt` sparisce), il check `lint: a name of scripts/api_lint_allow.txt is known` fallisce: è voluto, la fixture dipende dal file. Raccomandata: sì.
- D3 AGY-14b (azzerare `lint_lines` alla modifica) non in questo WO; vedi sopra. Raccomandata: WO separato dal planner.

## Prove per l'utente
Nessuna.
