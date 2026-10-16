# AGX-11 - TEST-CIAUDIT: test per `scripts/ci_audit.py` (le regole sul file `ci.yml`: timeout per job, concurrency, `sync/**`)
Assegnato ad AGX (16 ott), indipendente dagli altri WO. `ci_audit.py` controlla che il CI abbia i timeout e ignori i branch `sync/**`; non ha test. Il test guarda solo gli identificatori e i prefissi in inglese, quindi non dipende dalla traduzione di `ci_audit.py` che farà AGY-24.
Base: `origin/claude/integrazione-ottobre` @ `f5cd71b7` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/test-ci-audit C:\Users\Utente\Documents\GitHub\bm33-agx11 origin/claude/integrazione-ottobre`); branch `claude/test-ci-audit`. Mai push su bm-core; push solo di questo branch. (Il planner ha verificato che `scripts/ci_audit.py` è IDENTICO su `origin/bm-core` @ `e6d7bfd7`.)
Perimetro: UN file NUOVO, `tests/tools/test_ci_audit.py` (unittest, 4 test, circa 85 righe).
NON toccare: `scripts/ci_audit.py` (lo traduce AGY-24: non farlo qui), `.github/workflows/*`, `Makefile`, `docs/*`, `sync/*`, `SYNC.md`, gli altri file di `tests/tools/` (su questo branch ci sono `test_agent_audit.py`, `test_layout_side_by_side.py`, `test_mkbm_b16.py`).
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_ci_audit.py` esce 0 con `Ran 4 tests ... OK` (oppure stampa `SKIP: PyYAML not installed` ed esce 0 se PyYAML manca).

## Stato reale (verificato dal planner: file scritto in una copia di `origin/claude/integrazione-ottobre` @ `f5cd71b7` fuori dal repo ed eseguito in WSL con PyYAML 6.0.3; `4 tests OK`, circa 33 s)
- `scripts/ci_audit.py`: `python scripts/ci_audit.py [--gh] [--ci PATH]`. `--ci PATH` sostituisce `.github/workflows/ci.yml`. `check_ci` (riga 70) stampa righe che iniziano con `PROBLEM [ci]` per: PyYAML assente, file non trovato, YAML non valido, un job senza `timeout-minutes` (messaggio con `'<job>'` e `timeout-minutes`, righe 94-95), `concurrency` top-level mancante (96-97), `on.push.branches-ignore` senza `sync/**` (112-113; il messaggio contiene `branches-ignore`). Il test guarda SOLO le righe `PROBLEM [ci]` e solo `'host'`, `timeout-minutes`, `branches-ignore`: i messaggi sono oggi in italiano e dopo AGY-24 in inglese, gli identificatori restano.
- Il resto dello script legge il repo (`check_waits` su `tests/qemu_test.py` e `tests/rgb30/qemu_test.py`, `read_games` dal `Makefile`) e può stampare `PROBLEM [waits]`, `INFO`: il test li ignora e NON controlla l'exit code del caso "fixture valida" (potrebbe essere 1 per `[waits]`), solo le righe `[ci]`. Per i casi con un difetto controlla anche `returncode == 1` (vero comunque).
- PyYAML: manca nel Python di Windows del planner (`ModuleNotFoundError`), c'è in WSL (6.0.3). Il test, se `import yaml` fallisce, stampa `SKIP: PyYAML not installed (pip install pyyaml)` ed esce 0 PRIMA di definire i test: quindi su Windows senza PyYAML "passa" senza provare nulla. Per questo la verifica richiesta è in WSL.
- Lentezza: `ci_audit.py` impiega circa 8 s per esecuzione su `/mnt/c` (legge `qemu_test.py`, 7000+ righe, con una regex pesante): 4 esecuzioni = circa 30 s. Normale.
- Il test crea file in cartelle temporanee e non scrive nel repo.

## AGX-11a - il file (bm-coder, 20 min)
Passo 1: crea `tests/tools/test_ci_audit.py` con ESATTAMENTE questo contenuto (rientro di 4 spazi, nessuna tabulazione; la fixture `VALID` è YAML: il rientro di 2 spazi è parte del contenuto):
```python
"""Tests for scripts/ci_audit.py: the script runs as a subprocess on fake workflow files.

Only the `PROBLEM [ci]` lines are looked at, and only their identifiers (job name, timeout-minutes,
branches-ignore), so the tests do not depend on the language of the messages. The other checks of the
script (waits, carts) read the repository and are not what is tested here."""
import os, subprocess, sys, tempfile, unittest

try:
    import yaml  # noqa: F401
except ImportError:
    print("SKIP: PyYAML not installed (pip install pyyaml)")
    sys.exit(0)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "ci_audit.py")

VALID = """\
name: ci
on:
  push:
    branches-ignore: [sync/**]
concurrency:
  group: ci
  cancel-in-progress: true
jobs:
  host:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - run: make test-host
  qemu:
    runs-on: ubuntu-latest
    timeout-minutes: 60
    steps:
      - run: make test-qemu
"""


def ci_lines(stdout):
    return [ln for ln in stdout.splitlines() if ln.startswith("PROBLEM [ci]")]


class CiAuditTest(unittest.TestCase):
    def run_audit(self, text):
        """text: the content of ci.yml, or None for a path that does not exist"""
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "ci.yml")
            if text is not None:
                with open(path, "w", encoding="utf8", newline="\n") as fh:
                    fh.write(text)
            return subprocess.run([sys.executable, SCRIPT, "--ci", path], capture_output=True, text=True)

    def test_valid_workflow_has_no_ci_problem(self):
        res = self.run_audit(VALID)
        self.assertEqual(ci_lines(res.stdout), [], res.stdout)

    def test_job_without_timeout_minutes(self):
        res = self.run_audit(VALID.replace("    timeout-minutes: 30\n", ""))
        lines = ci_lines(res.stdout)
        self.assertEqual(len(lines), 1, res.stdout)
        self.assertIn("'host'", lines[0])
        self.assertIn("timeout-minutes", lines[0])
        self.assertEqual(res.returncode, 1)

    def test_branches_ignore_without_sync(self):
        res = self.run_audit(VALID.replace("[sync/**]", "[other/**]"))
        lines = ci_lines(res.stdout)
        self.assertEqual(len(lines), 1, res.stdout)
        self.assertIn("branches-ignore", lines[0])
        self.assertEqual(res.returncode, 1)

    def test_missing_file(self):
        res = self.run_audit(None)
        self.assertEqual(len(ci_lines(res.stdout)), 1, res.stdout)
        self.assertEqual(res.returncode, 1)


if __name__ == "__main__":
    unittest.main()
```
Passo 2: verifica in WSL (PyYAML c'è su Ubuntu per `python3`; controlla con `scripts/wsl-run.ps1 'python3 -c "import yaml; print(yaml.__version__)"' -Tail 120`):
- `scripts/wsl-run.ps1 'python3 tests/tools/test_ci_audit.py' -Tail 120`. Atteso: `Ran 4 tests in ~30 s` e `OK`, exit 0. Se un test fallisce: riporta l'errore intero e fermati (`BLOCKED`).
- Se PyYAML manca anche in WSL e il test stampa `SKIP`: NON installare nulla; scrivi nella consegna `test_ci_audit non verificato (PyYAML assente)` e riportalo a SPV.
Passo 3: `python -m py_compile tests/tools/test_ci_audit.py scripts/ci_audit.py` senza uscita; `git status --short`: solo `?? tests/tools/test_ci_audit.py`.

Criterio di fine: `python3 tests/tools/test_ci_audit.py` (WSL) -> exit 0, 4 test OK; 1 file nuovo.
Commit: un solo commit sul branch `claude/test-ci-audit`, messaggio `tests: test_ci_audit for scripts/ci_audit.py`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-11 --repo C:\Users\Utente\Documents\GitHub\bm33-agx11 --branch claude/test-ci-audit --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-11 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-11 @ claude/test-ci-audit@<sha> · verifica: python3 tests/tools/test_ci_audit.py (WSL) -> Ran 4 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-11 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 25 minuti.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si guardano solo le righe `PROBLEM [ci]` (identificatori inglesi) per non dipendere dalla lingua dei messaggi (AGY-24). Raccomandata: sì.
- D2 Senza PyYAML il test salta ed esce 0, come fa lo script (che stampa un `PROBLEM [ci]` e basta): dentro `make test-tools` (AGX-5) un computer senza PyYAML non fallisce. Raccomandata: sì.

## Prove per l'utente
Nessuna.
