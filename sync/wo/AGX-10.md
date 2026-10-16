# AGX-10 - TEST-PSF2C: test per `scripts/psf2c.py` (convertitore di font PSF in array C)
Assegnato ad AGX (16 ott), indipendente dagli altri WO. `psf2c.py` genera `src/gfx/font8x16.c` e `font6x12.c` dai font Terminus; non ha test, e un errore di mappatura Unicode lascerebbe glifi vuoti nella console senza che nessuno lo veda.
Base: `origin/bm-core` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/test-psf2c C:\Users\Utente\Documents\GitHub\bm33-agx10 origin/bm-core`); branch `claude/test-psf2c`. Mai push su bm-core; push solo di questo branch.
Perimetro: UN file NUOVO, `tests/tools/test_psf2c.py` (unittest, 5 test, circa 80 righe).
NON toccare: `scripts/psf2c.py`, `src/gfx/*.c` (non rigenerare i font!), `Makefile`, `docs/*`, `sync/*`, `SYNC.md`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_psf2c.py` esce 0 con `Ran 5 tests ... OK`.

## Stato reale (verificato dal planner: file scritto in una copia di `origin/bm-core` @ `e6d7bfd7` fuori dal repo ed eseguito su Windows; `psf2c.py` è identico su `claude/integrazione-ottobre`)
- `scripts/psf2c.py` (85 righe): `python scripts/psf2c.py FONT.psf[.gz] > font.c`. Il file si lancia con `subprocess` (la sua `main()` legge `sys.argv[1]` e stampa il C su stdout).
- `load_psf` (righe 16-48): se il file inizia con `1f 8b` lo decomprime (gzip). PSF2 (magic `72 b5 4a 86`): intestazione `<8I` (magic, versione, dimensione intestazione, flag, numero glifi, byte per glifo, altezza, larghezza). PSF1 (magic `36 04`): byte 2 = modo, byte 3 = byte per glifo (= altezza); modo bit 0 = 512 glifi, bit 1 = tabella Unicode (u16 little endian; `0xFFFF` chiude la voce di un glifo). Altrimenti: `SystemExit("not a PSF font")` (stderr, exit 1).
- `main` (righe 51-81): se la larghezza > 8: `SystemExit("only fonts up to 8 pixels wide are supported")`. Poi stampa l'intestazione, `static const uint8_t glyphs[256][ALTEZZA] = {`, 256 righe `    { 0x.., ... }, /* etichetta */` (etichetta = il carattere per i codici 0x20-0x7E tranne `\`, altrimenti `0x%02x`) e `const font_t font_console_<L>x<A> = {`. L'ordine dei 256 glifi è la code page 437 (`bytes([code]).decode("cp437")`); il codice 0 resta vuoto e non è "mancante". I codici senza voce nella tabella Unicode restano vuoti e finiscono in stderr come `missing glyphs: [1, 2, ...]`.
- Nel test: un PSF1 modo 2 con 256 glifi da 16 byte il cui indice è anche il suo codice Unicode per 0x20-0x7E (quindi 'A' = indice 65 = 0x41) e tutti gli altri codici senza voce; il glifo 'A' ha le righe `18 24 42 42 7e 42 42 00 ...`. In stdout deve comparire la riga `    { 0x18, 0x24, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* A */`. I codici mancanti includono 1 e 255 ma non 0 né 65.

## AGX-10a - il file (bm-coder, 20 min)
Passo 1: crea `tests/tools/test_psf2c.py` con ESATTAMENTE questo contenuto (rientro di 4 spazi, nessuna tabulazione):
```python
"""Tests for scripts/psf2c.py: the script runs as a subprocess on PSF fonts built in a temporary folder."""
import ast, gzip, os, struct, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "psf2c.py")

A_ROWS = bytes([0x18, 0x24, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])


def psf1(height=16, glyph_a=A_ROWS):
    """PSF1, mode 2 (256 glyphs, with a Unicode table): glyph i is U+0020..U+007E for i in that range"""
    glyphs = b"".join(glyph_a if i == 0x41 else bytes(height) for i in range(256))
    table = b""
    for i in range(256):
        if 0x20 <= i <= 0x7E:
            table += struct.pack("<H", i)
        table += struct.pack("<H", 0xFFFF)
    return b"\x36\x04" + bytes([2, height]) + glyphs + table


def psf2_wide():
    """PSF2 with 1 glyph, 9 pixels wide (2 bytes per row, 16 rows)"""
    return struct.pack("<8I", 0x864AB572, 0, 32, 0, 1, 32, 16, 9) + bytes(32)


class Psf2cTest(unittest.TestCase):
    def run_psf(self, data, name="font.psf"):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, name)
            with open(path, "wb") as fh:
                fh.write(data)
            return subprocess.run([sys.executable, SCRIPT, path], capture_output=True, text=True)

    def test_psf1_glyph_a_and_font_name(self):
        res = self.run_psf(psf1())
        self.assertEqual(res.returncode, 0, res.stderr)
        row = "    { " + ", ".join(f"0x{b:02x}" for b in A_ROWS) + " }, /* A */"
        self.assertIn(row, res.stdout.splitlines())
        self.assertIn("font_console_8x16", res.stdout)
        self.assertIn("static const uint8_t glyphs[256][16]", res.stdout)

    def test_psf1_gzip_gives_the_same_output(self):
        plain = self.run_psf(psf1())
        packed = self.run_psf(gzip.compress(psf1()), "font.psf.gz")
        self.assertEqual(packed.returncode, 0, packed.stderr)
        self.assertEqual(packed.stdout, plain.stdout)

    def test_unmapped_codes_are_reported_on_stderr(self):
        res = self.run_psf(psf1())
        self.assertIn("missing glyphs:", res.stderr)
        # the table maps only U+0020..U+007E: code 1 and code 255 are missing, 'A' (65) is not
        missing = ast.literal_eval(res.stderr.split("missing glyphs:")[1].strip())
        self.assertIn(1, missing)
        self.assertIn(255, missing)
        self.assertNotIn(65, missing)
        self.assertNotIn(0, missing)

    def test_psf2_wider_than_8_pixels_is_refused(self):
        res = self.run_psf(psf2_wide())
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("only fonts up to 8 pixels wide", res.stderr)

    def test_not_a_font_is_refused(self):
        res = self.run_psf(b"this is not a font at all")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("not a PSF font", res.stderr)


if __name__ == "__main__":
    unittest.main()
```
Passo 2: lancia `python tests/tools/test_psf2c.py` dalla radice del worktree. Atteso: `Ran 5 tests in <3 s` e `OK`, exit 0. Se un test fallisce: riporta l'errore intero e fermati (`BLOCKED`).
Passo 3: `python -m py_compile tests/tools/test_psf2c.py` senza uscita; `git status --short`: solo `?? tests/tools/test_psf2c.py` (nessun `.c` rigenerato).
Controllo di sensibilità (facoltativo): il planner ha verificato che il test FALLISCE (1 fallimento) se in una copia di `psf2c.py` si cambia `if width > 8:` in `if width > 80:`. Se non lo fai scrivi `non verificato che i test falliscano su uno script rotto`.

Criterio di fine: `python tests/tools/test_psf2c.py` -> exit 0, 5 test OK; 1 file nuovo.
Commit: un solo commit sul branch `claude/test-psf2c`, messaggio `tests: test_psf2c for scripts/psf2c.py`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-10 --repo C:\Users\Utente\Documents\GitHub\bm33-agx10 --branch claude/test-psf2c --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-10 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-10 @ claude/test-psf2c@<sha> · verifica: python tests/tools/test_psf2c.py -> Ran 5 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-10 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 30 minuti.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si costruiscono i font nel test (nessun font Terminus nel repo, nessun download). Raccomandata: sì.

## Prove per l'utente
Nessuna.
