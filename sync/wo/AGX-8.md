# AGX-8 - TEST-MANUALS: test per `scripts/manuals.py` (divisione dei capitoli, confronto en/it, lacune)
Assegnato ad AGX (16 ott), indipendente dagli altri WO. `scripts/manuals.py check` decide se i manuali inglese e italiano dicono la stessa cosa: se la sua logica (`split_chapters`, `signature`, `technical`) si rompe, nessuno lo nota.
Base: `origin/bm-core` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/test-manuals C:\Users\Utente\Documents\GitHub\bm33-agx8 origin/bm-core`); branch `claude/test-manuals`. Mai push su bm-core; push solo di questo branch.
Perimetro: UN file NUOVO, `tests/tools/test_manuals.py` (unittest, 6 test, circa 95 righe).
NON toccare: `scripts/manuals.py` (si testa com'è), `manuals/*`, `Makefile` (il target `test-tools` lo aggiunge AGX-5), `docs/*`, `sync/*`, `SYNC.md`, gli altri file di `tests/tools/`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_manuals.py` esce 0 con `Ran 6 tests ... OK`.

## Stato reale (verificato dal planner: file scritto in una copia di `origin/bm-core` @ `e6d7bfd7` fuori dal repo ed eseguito, su Windows con Python 3.14 e in WSL)
- `scripts/manuals.py`: `import tomllib` (riga 21) richiede Python 3.11 o più (AGX ha 3.12 su Windows: va bene). Si importa senza effetti collaterali (la CLI è sotto `if __name__`), quindi il test mette `scripts/` in `sys.path` e fa `import manuals`.
- `split_chapters(path)` (righe 44-55): divide su `# ` (titolo di capitolo) ma NON dentro un blocco recintato da righe che iniziano con tre apici inversi; restituisce `[(titolo, testo)]` con la testa del file con titolo `""`. Apre il file senza chiuderlo (`open(...).read()`): in Python 3.12+ compare un `ResourceWarning` (innocuo); il test lo silenzia con `warnings.simplefilter`.
- `signature(text)` (righe 74-133) restituisce `(sig, spans, numero_di_lacune)`: la forma di un capitolo (titoli, tabelle, elenchi, riquadri, codice senza commenti né contenuto delle stringhe, immagini), gli `` `span` `` tecnici ordinati e il numero di lacune. `no_comment` (righe 58-65) toglie commenti e il testo delle stringhe. `technical(span)` (righe 68-71): vero se non ha spazi, `<`, `>`, `=` e contiene `_`, `(`, `.` o `/`. `GAP` (riga 28): `\[(?:TO FILL|DA COMPLETARE):`.
- Il test non legge i manuali veri: crea tutto in una cartella temporanea.

## AGX-8a - il file (bm-coder, 20 min)
Passo 1: crea `tests/tools/test_manuals.py` con ESATTAMENTE questo contenuto (copia e incolla; `FENCE` evita di scrivere tre apici inversi nel sorgente; rientro di 4 spazi, nessuna tabulazione):
```python
"""Tests for scripts/manuals.py: split_chapters, signature, technical and the gap counter."""
import os, sys, tempfile, unittest, warnings

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import manuals  # noqa: E402  (needs Python 3.11+: tomllib)

# split_chapters opens its file without closing it; that is the script's, not what is tested here
warnings.simplefilter("ignore", ResourceWarning)

FENCE = "`" * 3

CHAPTER_EN = f"""\
# Start

Some prose with `foo()` and `a/b.md` in it.

{FENCE}lua
-- a comment
local s = "hello"
print(s) -- say it
{FENCE}

| name | value |
|------|-------|
| a    | 1     |

- first item
1. first step
"""

# the same shape: other prose, other comments, other string text; the technical spans are the same
CHAPTER_IT = f"""\
# Inizio

Un po' di prosa con `foo()` e `a/b.md` dentro.

{FENCE}lua
-- un commento
local s = "ciao"
print(s) -- dillo
{FENCE}

| nome | valore |
|------|--------|
| b    | 2      |

- primo punto
1. primo passo
"""


class ManualsTest(unittest.TestCase):
    def test_split_chapters_ignores_hash_inside_fence(self):
        text = f"head line\n# One\ntext\n{FENCE}\n# not a chapter\n{FENCE}\n# Two\nmore\n"
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "m.md")
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            chapters = manuals.split_chapters(path)
        self.assertEqual([t for t, _ in chapters], ["", "One", "Two"])
        self.assertIn("# not a chapter", chapters[1][1])
        self.assertNotIn("# not a chapter", chapters[2][1])

    def test_signature_same_for_translated_pair(self):
        self.assertEqual(manuals.signature(CHAPTER_EN), manuals.signature(CHAPTER_IT))

    def test_signature_differs_with_extra_table_row(self):
        longer = CHAPTER_IT.replace("| b    | 2      |\n", "| b    | 2      |\n| c    | 3      |\n")
        self.assertNotEqual(manuals.signature(longer), manuals.signature(CHAPTER_EN))

    def test_signature_differs_when_a_technical_span_changes(self):
        other = CHAPTER_IT.replace("`foo()`", "`bar()`")
        self.assertNotEqual(manuals.signature(other), manuals.signature(CHAPTER_EN))

    def test_technical(self):
        self.assertTrue(manuals.technical("foo()"))
        self.assertTrue(manuals.technical("a/b.md"))
        self.assertFalse(manuals.technical("key=value"))
        self.assertFalse(manuals.technical("<x>"))

    def test_gap_counts_both_languages(self):
        text = "one [TO FILL: a] and two [DA COMPLETARE: b] and plain [text]"
        _sig, _spans, gaps = manuals.signature(text)
        self.assertEqual(gaps, 2)
        self.assertEqual(len(manuals.GAP.findall(text)), 2)


if __name__ == "__main__":
    unittest.main()
```
Passo 2: lancia `python tests/tools/test_manuals.py` dalla radice del worktree. Atteso: `Ran 6 tests in <1 s` e `OK`, exit 0. Se compare `ModuleNotFoundError: tomllib` hai un Python vecchio: usa `python3` di WSL (`scripts/wsl-run.ps1 'python3 tests/tools/test_manuals.py' -Tail 120`). Se un test fallisce: riporta l'errore intero e fermati (`BLOCKED`).
Passo 3: `python -m py_compile tests/tools/test_manuals.py` senza uscita; `git status --short`: solo `?? tests/tools/test_manuals.py`.
Controllo di sensibilità (facoltativo): il planner ha verificato che il test FALLISCE (1 fallimento ciascuno) se in una copia di `manuals.py` si cambia `if ln.startswith("# ") and not fence:` in `if ln.startswith("# "):`, oppure `(?:TO FILL|DA COMPLETARE)` in `(?:TO FILL)`. Se non lo fai scrivi `non verificato che i test falliscano su uno script rotto`.

Criterio di fine: `python tests/tools/test_manuals.py` -> exit 0, 6 test OK; 1 file nuovo.
Commit: un solo commit sul branch `claude/test-manuals`, messaggio `tests: test_manuals for scripts/manuals.py`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-8 --repo C:\Users\Utente\Documents\GitHub\bm33-agx8 --branch claude/test-manuals --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-8 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-8 @ claude/test-manuals@<sha> · verifica: python tests/tools/test_manuals.py -> Ran 6 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-8 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 30 minuti.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si testa la logica pura (le funzioni), non `build` (che richiede `reportlab`) né `check` sui manuali veri. Raccomandata: sì.
- D2 `ResourceWarning` silenziato nel test: il file non chiuso è dello script, correggerlo è un altro WO. Raccomandata: sì.

## Prove per l'utente
Nessuna.
