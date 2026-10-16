# AGX-4 - TEST-CINDEX: test per `scripts/cindex.py`, l'indice dei file grandi (C e Lua) che il progetto usa per leggere `runtime.c`
Assegnato ad AGX (16 ott), indipendente dagli altri WO. `scripts/cindex.py` (CLAUDE.md §1) non ha alcun test: una modifica alle sue espressioni regolari potrebbe rompere l'indice di tutti senza che nessuno se ne accorga.
Base: `origin/bm-core` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/test-cindex C:\Users\Utente\Documents\GitHub\bm33-agx4 origin/bm-core`); branch `claude/test-cindex`. Mai push su bm-core; push solo di questo branch.
Perimetro: UN file NUOVO, `tests/tools/test_cindex.py` (unittest, 5 test, circa 75 righe).
NON toccare: `scripts/cindex.py` (si testa com'è: se un test fallisce NON si cambia lo script, si cambia il test solo se l'attesa è sbagliata e lo si scrive nella consegna), `Makefile` (il target `test-tools` lo aggiunge AGX-5), `docs/*`, `sync/*`, `SYNC.md`, gli altri file di `tests/tools/`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_cindex.py` esce 0 con `Ran 5 tests ... OK`.

## Stato reale (verificato dal planner: ho scritto il file qui sotto in una copia di `origin/bm-core` @ `e6d7bfd7` fuori dal repo ed eseguito il test, su Windows e in WSL)
- `scripts/cindex.py` (43 righe): `python3 scripts/cindex.py FILE [WORD]`. Per un file `.lua` elenca le righe `function nome`, `local function nome`, `P.x = function` (regex `lfun`, riga 21); per ogni altro file (C) elenca i banner `/* ---- nome */` come `# nome` (regex `banner`, riga 19) e le funzioni come `  nome` (regex `cfun`, riga 20): una riga di funzione C non deve iniziare con spazio, tab, `#`, `}` o `/` (riga 35) e il nome non può essere `if`, `while`, `for`, `switch`, `return`. Con WORD (minuscolo) tiene solo le righe il cui testo (compreso il prefisso `# ` o due spazi) lo contiene (riga 38). Ogni riga stampata è `f"{ln:6d} {txt}"` (riga 40): numero di riga allineato a destra su 6 colonne, uno spazio, il testo. Il riepilogo `-- N lines; ...` va su stderr (riga 41), quindi lo stdout contiene solo l'indice.
- Il file contiene la chiamata `main()` senza protezione `if __name__`, quindi il test lancia lo script con `subprocess` (non lo importa).
- Il catalogo diceva `python tests/tools/test_cindex.py` come verifica: confermato. Altri test di `tests/tools/` (`test_agent_audit.py`) usano `unittest` e `subprocess` allo stesso modo: questo ne segue lo stile.
- Python: basta quello di Windows (3.12) o quello di WSL; nessuna libreria esterna.

## AGX-4a - il file (bm-coder, 15 min)
Passo 1: crea `tests/tools/test_cindex.py` con ESATTAMENTE questo contenuto (copia e incolla, rientro di 4 spazi, nessuna tabulazione):
```python
"""Tests for scripts/cindex.py: the script runs as a subprocess on temporary C and Lua files."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "cindex.py")

C_SRC = """\
#include <stdio.h>
/* ---- Drawing */
static int l_cls(lua_State *L)
{
    if (x) {
        return 0;
    }
    while (y) {
        y--;
    }
    return 1;
}

void foo(void)
{
}
"""

LUA_SRC = """\
function a()
end
local function b()
end
P.c = function(x)
end
  local d = 1
"""


class CindexTest(unittest.TestCase):
    def run_index(self, name, text, *extra):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, name)
            with open(path, "w", encoding="utf8", newline="\n") as fh:
                fh.write(text)
            res = subprocess.run([sys.executable, SCRIPT, path, *extra],
                                 capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        return res.stdout.splitlines()

    def test_c_banner_and_functions(self):
        lines = self.run_index("x.c", C_SRC)
        # banner on line 2, l_cls on line 3, foo on line 14
        self.assertEqual(lines, ["     2 # Drawing", "     3   l_cls", "    14   foo"])

    def test_c_skips_if_while_and_indented_lines(self):
        lines = self.run_index("x.c", C_SRC)
        text = "\n".join(lines)
        self.assertNotIn("if", text)
        self.assertNotIn("while", text)
        self.assertEqual(len(lines), 3)

    def test_lua_functions(self):
        lines = self.run_index("x.lua", LUA_SRC)
        self.assertEqual(lines, ["     1   a", "     3   b", "     5   P.c"])

    def test_word_filter_is_case_insensitive(self):
        lines = self.run_index("x.c", C_SRC, "CLS")
        self.assertEqual(lines, ["     3   l_cls"])
        lines = self.run_index("x.c", C_SRC, "drawing")
        self.assertEqual(lines, ["     2 # Drawing"])

    def test_line_numbers_right_aligned_6_columns(self):
        for line in self.run_index("x.c", C_SRC):
            self.assertEqual(line[6], " ")
            self.assertTrue(line[:6].strip().isdigit(), line)
            self.assertEqual(line[:6], f"{int(line[:6]):6d}")


if __name__ == "__main__":
    unittest.main()
```
Passo 2: lancia `python tests/tools/test_cindex.py` dalla radice del worktree. Atteso: `Ran 5 tests in <1,5 s` e `OK`, exit 0. Se un test fallisce: riporta l'errore intero e fermati (`BLOCKED`); non adattare le attese a ciò che vedi senza capire perché.
Passo 3: `python -m py_compile tests/tools/test_cindex.py` senza uscita. `git status --short`: solo `?? tests/tools/test_cindex.py` (le cartelle `__pycache__` sono ignorate da git).
Controllo di sensibilità (facoltativo): il planner ha verificato che il test FALLISCE se in una copia di `cindex.py` si cambia `word not in txt.lower()` in `word not in txt` (1 fallimento) o `{ln:6d}` in `{ln:5d}` (4 fallimenti). Se non lo fai scrivi `non verificato che i test falliscano su uno script rotto` nella consegna.

Criterio di fine: `python tests/tools/test_cindex.py` -> exit 0, 5 test OK; `git diff --stat` / `git status`: 1 file nuovo.
Commit: un solo commit sul branch `claude/test-cindex`, messaggio `tests: test_cindex for scripts/cindex.py`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-4 --repo C:\Users\Utente\Documents\GitHub\bm33-agx4 --branch claude/test-cindex --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-4 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-4 @ claude/test-cindex@<sha> · verifica: python tests/tools/test_cindex.py -> Ran 5 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-4 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 25 minuti.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si usa `unittest` come `test_agent_audit.py` (il `make test-tools` di AGX-5 lancia ogni `tests/tools/test_*.py` come script: `unittest.main()` esce 0/1). Raccomandata: sì.
- D2 Lo script si lancia con `subprocess` perché `cindex.py` chiama `main()` senza `if __name__`; non si modifica lo script. Raccomandata: sì.

## Prove per l'utente
Nessuna.
