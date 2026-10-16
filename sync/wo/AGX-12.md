# AGX-12 - TEST-SLOW-NAMES: ogni nome della tabella `SLOW` di `tests/qemu_test.py` è un test che esiste davvero
Assegnato ad AGX (16 ott), indipendente dagli altri WO. La tabella `SLOW` (i secondi dei test lenti, usata da `--shard K/N` per dividere il CI in gruppi uguali) ha 26 nomi; se un test viene rinominato o tolto, il nome resta nella tabella e i gruppi si sbilanciano senza che nessuno se ne accorga.
Base: `origin/bm-core` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/test-qemu-slow C:\Users\Utente\Documents\GitHub\bm33-agx12 origin/bm-core`); branch `claude/test-qemu-slow`. Mai push su bm-core; push solo di questo branch.
Perimetro: UN file NUOVO, `tests/tools/test_qemu_slow.py` (unittest, 1 test, circa 35 righe).
NON toccare: `tests/qemu_test.py` (non va importato né modificato), `Makefile`, `docs/*`, `sync/*`, `SYNC.md`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_qemu_slow.py` esce 0 (`Ran 1 test ... OK`) e, se un nome di `SLOW` non ha più la sua `def test_*`, fallisce elencando i nomi.

## Stato reale (verificato dal planner: file scritto in una copia di `origin/bm-core` @ `e6d7bfd7` fuori dal repo ed eseguito su Windows)
- `tests/qemu_test.py`: `SLOW = {` alla riga 66 (comando: `grep -n "^SLOW" tests/qemu_test.py`, una riga; su `bm33-int2` è uguale), fino alla `}` della riga 74; 26 chiavi stringa (`"test_chainloader": 70, ...`). Il catalogo diceva "26 chiavi" e "int2 :66-74": confermato anche su `bm-core`.
- Le 26 chiavi corrispondono oggi a 26 funzioni `def test_*` del file (verificato con `ast`): il test passa subito.
- Il file NON si importa: `import qemu_test` richiederebbe QEMU, `tools/` e `scripts/` in `sys.path`. Il test lo legge con `ast.parse` e cerca (a) il nodo `Assign` di `SLOW` e le sue chiavi (`ast.Constant`), (b) tutti i `FunctionDef` il cui nome inizia con `test_` (a qualunque livello: `ast.walk`).
- Il test è lento solo per `ast.parse` di un file da 7700 righe: circa 1 s.

## AGX-12a - il file (bm-coder, 10 min)
Passo 1: crea `tests/tools/test_qemu_slow.py` con ESATTAMENTE questo contenuto (rientro di 4 spazi, nessuna tabulazione):
```python
"""Every name in the SLOW table of tests/qemu_test.py (the shards' weights) must be a `def test_*` of that file.

The file is read with `ast`, not imported (an import would need QEMU, the tools folder and a build)."""
import ast, os, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QEMU_TEST = os.path.join(ROOT, "tests", "qemu_test.py")


def read_tree():
    with open(QEMU_TEST, encoding="utf-8") as fh:
        return ast.parse(fh.read(), QEMU_TEST)


def slow_keys(tree):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "SLOW" for t in node.targets):
            return [k.value for k in node.value.keys]
    return None


class QemuSlowTest(unittest.TestCase):
    def test_every_slow_name_is_a_test_function(self):
        tree = read_tree()
        keys = slow_keys(tree)
        self.assertIsNotNone(keys, "SLOW = {...} not found in tests/qemu_test.py")
        self.assertGreater(len(keys), 0)
        funcs = {n.name for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name.startswith("test_")}
        missing = sorted(k for k in keys if k not in funcs)
        self.assertEqual(missing, [], "SLOW names without a `def test_*`: " + ", ".join(missing))


if __name__ == "__main__":
    unittest.main()
```
Passo 2: lancia `python tests/tools/test_qemu_slow.py` dalla radice del worktree. Atteso: `Ran 1 test in ~1 s` e `OK`, exit 0. Se fallisce con `SLOW names without a def test_*: <nomi>`: significa che un test è stato rinominato o tolto su questa base: riporta i nomi e `BLOCKED` (non si corregge la tabella qui; il planner non l'ha visto succedere).
Passo 3: `python -m py_compile tests/tools/test_qemu_slow.py` senza uscita; `git status --short`: solo `?? tests/tools/test_qemu_slow.py`.
Controllo di sensibilità (facoltativo, 1 minuto): copia `tests/qemu_test.py` in un file temporaneo FUORI dal repo con `"test_lib_tab": 46` cambiato in `"test_lib_tabx": 46` e fai puntare `QEMU_TEST` a quel file (`python -c "import sys; sys.path.insert(0,'tests/tools'); import test_qemu_slow as t; t.QEMU_TEST=r'<copia>'; import unittest; unittest.main(module=t, argv=['x'])"`): il planner l'ha fatto e ha visto `FAIL ... SLOW names without a def test_*: test_lib_tabx`. Se non lo fai scrivi `non verificato che il test fallisca con un nome sbagliato`.

Criterio di fine: `python tests/tools/test_qemu_slow.py` -> exit 0, 1 test OK; 1 file nuovo.
Commit: un solo commit sul branch `claude/test-qemu-slow`, messaggio `tests: test_qemu_slow checks the SLOW table of qemu_test.py`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-12 --repo C:\Users\Utente\Documents\GitHub\bm33-agx12 --branch claude/test-qemu-slow --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-12 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-12 @ claude/test-qemu-slow@<sha> · verifica: python tests/tools/test_qemu_slow.py -> Ran 1 test OK, exit 0 · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-12 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto) o bm-mechanic.
Stima: 15 minuti.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si controlla solo che i nomi esistano, non che i secondi siano giusti (non si possono sapere senza il CI). Raccomandata: sì.
- Coordinamento: altri branch (API-1, APP-5b, `claude/ci-repair`) aggiungono test a `qemu_test.py` ma non toccano `SLOW`; se uno la modifica, questo test dice se i nomi restano veri.

## Prove per l'utente
Nessuna.
