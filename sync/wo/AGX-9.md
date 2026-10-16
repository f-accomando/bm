# AGX-9 - TEST-MKBM-MAP: test per i livelli della mappa di `scripts/mkbm.py` (`read_map`, `read_layers`)
Assegnato ad AGX (16 ott), indipendente dagli altri WO. I livelli della mappa (fino a 8, tutti della dimensione del primo, R11) non hanno test su `bm-core`: un errore di riempimento sposterebbe le tile in tutti i giochi con più livelli.
Base: `origin/bm-core` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/test-mkbm-map C:\Users\Utente\Documents\GitHub\bm33-agx9 origin/bm-core`); branch `claude/test-mkbm-map`. Mai push su bm-core; push solo di questo branch.
Perimetro: UN file NUOVO, `tests/tools/test_mkbm_map.py` (unittest, 9 test, circa 100 righe).
NON toccare: `scripts/mkbm.py`, `tests/tools/test_mkbm_b16.py` (esiste solo su `claude/integrazione-ottobre`, non su `bm-core`: il catalogo lo citava come esempio, qui non c'è), `Makefile`, `docs/*`, `sync/*`, `SYNC.md`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_mkbm_map.py` esce 0 con `Ran 9 tests ... OK`.

## Stato reale (verificato dal planner: file scritto in una copia di `origin/bm-core` @ `e6d7bfd7` e anche di `origin/claude/integrazione-ottobre` @ `f5cd71b7`, fuori dal repo, eseguito su Windows)
- `scripts/mkbm.py` si importa senza effetti collaterali (`main()` è sotto `if __name__`, riga 608), purché `scripts/` sia in `sys.path` (importa altri moduli di `scripts/`, per esempio `bmaudio`): il test fa `sys.path.insert(0, <radice>/scripts)` e `import mkbm`. Il catalogo suggeriva "come fa `tests/tools/test_mkbm_b16.py`": su `bm-core` quel file NON esiste, quindi si usa solo `sys.path`.
- `read_map(path)` (righe 120-125): salta le righe vuote e quelle che iniziano con `#`; numeri separati da virgole (anche con spazi); la larghezza `w` è la riga più lunga, le righe corte sono completate con `0` (celle a 16 bit little endian, `struct.pack("<H", ...)`); restituisce `(w, numero_di_righe, celle)`. Apre il file senza chiuderlo (`ResourceWarning` innocuo, silenziato nel test).
- `LAYERS_MAX, LAYER_NAME = 8, 16` (riga 128). `read_layers(specs)` (righe 131-154): ogni voce è `file.csv` o `nome=file.csv` (senza nome: `main`); nome più lungo di 16 byte -> `SystemExit("--map <spec>: a layer's name is at most 16 bytes")`; due livelli con lo stesso nome -> `SystemExit("... two layers called 'x'")`; un livello più grande del primo -> `SystemExit("... bigger than the first layer (WxH)")`; uno più piccolo viene riempito di celle vuote alla dimensione del primo; più di 8 livelli -> `SystemExit("--map: at most 8 layers")`.
- Il test usa solo file CSV temporanei.

## AGX-9a - il file (bm-coder, 20 min)
Passo 1: crea `tests/tools/test_mkbm_map.py` con ESATTAMENTE questo contenuto (rientro di 4 spazi, nessuna tabulazione):
```python
"""Tests for the map layers of scripts/mkbm.py: read_map and read_layers."""
import os, struct, sys, tempfile, unittest, warnings

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mkbm  # noqa: E402

# read_map opens its file without closing it; that is the script's, not what is tested here
warnings.simplefilter("ignore", ResourceWarning)


def cells(*values):
    return b"".join(struct.pack("<H", v) for v in values)


class MkbmMapTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def csv(self, name, text):
        path = os.path.join(self._tmp.name, name)
        with open(path, "w", newline="\n") as fh:
            fh.write(text)
        return path

    def test_read_map_skips_blank_and_comment_lines(self):
        path = self.csv("m.csv", "# a comment\n1,2,3\n\n   \n4,5,6\n# end\n")
        self.assertEqual(mkbm.read_map(path), (3, 2, cells(1, 2, 3, 4, 5, 6)))

    def test_read_map_pads_short_rows_with_zero(self):
        path = self.csv("m.csv", "1,2,3\n4\n5,6\n")
        self.assertEqual(mkbm.read_map(path), (3, 3, cells(1, 2, 3, 4, 0, 0, 5, 6, 0)))

    def test_read_map_width_is_the_longest_row(self):
        path = self.csv("m.csv", "1,2\n3,4,5,6\n")
        w, h, data = mkbm.read_map(path)
        self.assertEqual((w, h), (4, 2))
        self.assertEqual(len(data), 4 * 2 * 2)

    def test_second_layer_smaller_is_padded_to_the_first(self):
        a = self.csv("a.csv", "1,2,3\n4,5,6\n7,8,9\n")
        b = self.csv("b.csv", "9,9\n9,9\n")
        layers = mkbm.read_layers(["back=" + a, "front=" + b])
        self.assertEqual([x[0] for x in layers], ["back", "front"])
        self.assertEqual(layers[1][1:3], (3, 3))
        self.assertEqual(layers[1][3], cells(9, 9, 0, 9, 9, 0, 0, 0, 0))

    def test_a_single_unnamed_layer_is_main(self):
        a = self.csv("a.csv", "1,2\n")
        self.assertEqual(mkbm.read_layers([a]), [("main", 2, 1, cells(1, 2))])

    def test_second_layer_bigger_exits(self):
        a = self.csv("a.csv", "1,2\n3,4\n")
        b = self.csv("b.csv", "1,2,3\n4,5,6\n")
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers(["a=" + a, "b=" + b])
        self.assertIn("bigger than the first layer", str(cm.exception))

    def test_two_layers_with_the_same_name_exit(self):
        a = self.csv("a.csv", "1,2\n")
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers(["x=" + a, "x=" + a])
        self.assertIn("two layers called", str(cm.exception))

    def test_more_than_eight_layers_exit(self):
        a = self.csv("a.csv", "1\n")
        specs = [f"l{i}={a}" for i in range(mkbm.LAYERS_MAX)]
        self.assertEqual(len(mkbm.read_layers(specs)), mkbm.LAYERS_MAX)   # 8 are fine
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers(specs + [f"l8={a}"])
        self.assertIn("at most 8 layers", str(cm.exception))

    def test_name_over_16_bytes_exits(self):
        a = self.csv("a.csv", "1\n")
        ok = "n" * mkbm.LAYER_NAME
        self.assertEqual(mkbm.read_layers([f"{ok}={a}"])[0][0], ok)       # exactly 16 is fine
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers([f"{ok}x={a}"])
        self.assertIn("at most 16 bytes", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
```
Passo 2: lancia `python tests/tools/test_mkbm_map.py` dalla radice del worktree. Atteso: `Ran 9 tests in <1 s` e `OK`, exit 0. Se un test fallisce: riporta l'errore intero e fermati (`BLOCKED`).
Passo 3: `python -m py_compile tests/tools/test_mkbm_map.py` senza uscita; `git status --short`: solo `?? tests/tools/test_mkbm_map.py`.
Controllo di sensibilità (facoltativo): il planner ha verificato che il test FALLISCE se in una copia di `mkbm.py` si toglie il controllo dei commenti (`and not line.startswith("#")`), si cambia `LAYERS_MAX = 8` in `9`, o si sbaglia il riempimento del livello piccolo (`ljust(w0 * 2 + 2, ...)`). Se non lo fai scrivi `non verificato che i test falliscano su uno script rotto`.

Criterio di fine: `python tests/tools/test_mkbm_map.py` -> exit 0, 9 test OK; 1 file nuovo.
Commit: un solo commit sul branch `claude/test-mkbm-map`, messaggio `tests: test_mkbm_map for the map layers of scripts/mkbm.py`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-9 --repo C:\Users\Utente\Documents\GitHub\bm33-agx9 --branch claude/test-mkbm-map --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-9 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-9 @ claude/test-mkbm-map@<sha> · verifica: python tests/tools/test_mkbm_map.py -> Ran 9 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-9 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 25 minuti.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si testa solo la lettura dei livelli, non la scrittura della sezione LAYERS (`layers_section`) né il `.bm` finale: bastano a coprire il riempimento e i limiti. Raccomandata: sì.

## Prove per l'utente
Nessuna.
