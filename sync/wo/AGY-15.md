# AGY-15 - QEMU-TMP: i test QEMU dell'RGB30 (`tests/rgb30/qemu_test.py`) rimuovono le loro cartelle temporanee
Assegnato ad AGY (16 ott), indipendente da AGY-14. Nasce dal revisore di B16/SDK: `sdk_kb_run`, `test_sdk_pad_lib`, `test_sdk_pad_build` e gli altri test creano `tempfile.mkdtemp(prefix="bm64kb-")` (~129 MB l'una: l'immagine SD da 64 MB e il suo dump `after.img`) e non le tolgono mai; un `-k sdk_` completo riempie il `/tmp` di WSL (tmpfs condiviso da 3,9 GB) e produce falsi `mkfs.vfat failed whilst writing FAT`.
Base: `origin/claude/sdk-b16-dev-without-pc` @ `3db6a92c` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/rgb30-test-tmp-cleanup <cartella> origin/claude/sdk-b16-dev-without-pc`); branch `claude/rgb30-test-tmp-cleanup`. Mai push su bm-core; push solo di questo branch.
Perimetro: `tests/rgb30/qemu_test.py` soltanto (3 punti: gli `import`, un blocco nuovo di helper, `main()`; più 19 righe in cui cambia una chiamata).
NON toccare: `tests/qemu_test.py` (il test del Pi), `src/**`, `Makefile`, `rgb30.mk`, `scripts/*`, `docs/*`, `SYNC.md`, `sync/*`, il corpo di qualunque test (nessun test cambia comportamento, nessun `assert` si tocca).
Vincoli: solo QEMU (`not verified on hardware`). Ogni `file:riga` qui sotto è stato aperto dal planner su `3db6a92c`; l'operatore lo riscontra con `grep -n` e ciò che non riscontra lo scrive `non verificato`.
Obiettivo: dopo un test non resta nessuna cartella `/tmp/bm64*`, e il test passa come prima.

## Stato reale (verificato dal planner su un worktree temporaneo di `3db6a92c`, in WSL)
- `grep -c 'tempfile.mkdtemp(prefix=' tests/rgb30/qemu_test.py` → 19. Righe: 39 (`Qemu.__init__`, `self.tmp`, prefisso `bm64-`: contiene `mon.sock` e `screen.ppm`), 262, 307, 492, 535, 570, 652 (`bm64sd-`), 341 (`bm64up-`), 595, 843 (`bm64sdk-`), 751 (`bm64mk-`), 975 (`sdk_kb_run`), 1095 (`test_sdk_pad_lib`), 1145 (`test_sdk_pad_build`) (tutte e tre `bm64kb-`), 1220 (`bm64bat-`), 1276 (`bm64low-`), 1336 (`bm64riff-`), 1406 (`bm64nnet-`), 1438 (`bm64snd-`). Nessun `rmtree`, `shutil` o `TemporaryDirectory` nel file.
- Misurato prima: `python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k sdk_pad_sprite` → PASS, e dopo `df -h /tmp` dice `129M` usati e `ls -d /tmp/bm64*` elenca `/tmp/bm64-pi_vzrdb` e `/tmp/bm64kb-8cxxvhmh` (129 MB).
- Perché non un `try/finally` per ogni sito: in `sdk_kb_run` il dump viene letto DOPO il `finally` che chiude QEMU (`check(cart_secs(sd_read(dump, ...)))`, riga ~985), in `test_sdk_pad_lib` e `test_sdk_pad_build` pure (`sd_read(dump, ...)` dopo `q.close()`): un `rmtree` nel `finally` esistente cancellerebbe il dump prima della lettura; chiudere tutti e 19 i siti con un `try/finally` esterno vuol dire rientrare il corpo di ogni test (diff molto grande). Si usa invece UNA funzione `mktmp()` che registra le cartelle e UNA pulizia in `main()` dopo ogni test (anche se fallisce). Nessun test usa una cartella dopo il proprio ritorno (QEMU è già chiuso: `Qemu.close()` fa kill+wait).
- CRLF: le copie su Windows hanno i fine riga CRLF (`file tests/rgb30/qemu_test.py`). Mantenerli: `git diff --stat` finale deve dire `tests/rgb30/qemu_test.py | 56 +++++++++++++++++++++++++++----------------` (37 inserimenti, 19 cancellazioni). Se le righe cambiate sono molte di più: `git checkout -- tests/rgb30/qemu_test.py` e rifare mantenendo CRLF.
- Ambiente: `/tmp` di WSL è condiviso e si riempie (altri processi QEMU). Prima: `wsl -e bash -c "df -h /tmp | tail -1"`. Per i test QEMU `TMPDIR` deve restare in `/tmp` di WSL (non su `/mnt/c`: mkfs.vfat e mtools lì sono lenti o falliscono). Non cancellare in `/tmp` ciò che non hai creato; le due cartelle `bm64*` della prova "prima" sono tue e le togli tu.

## AGY-15a - la modifica (bm-coder, 15 min)
Passo 1: `import shutil`. Subito DOPO la riga `import re` (riga 10; riscontrare con `grep -n '^import re' tests/rgb30/qemu_test.py`: una riga) inserire la riga
```
import shutil
```
(l'ordine alfabetico resta: re, shutil, socket).

Passo 2: gli helper. Subito DOPO la riga (riga 29, unica)
```
QEMU = os.environ.get("QEMU64", "qemu-system-aarch64")
```
inserire ESATTAMENTE (una riga vuota, poi il blocco; resta una riga vuota prima di `SCREEN = ...`):
```

TMP_DIRS = []           # the temporary folders of the running test, removed by main() after it


def mktmp(prefix):
    """A temporary folder (an SD image is 64 MB, and /tmp of WSL is a 3.9 GB tmpfs)."""
    d = tempfile.mkdtemp(prefix=prefix)
    TMP_DIRS.append(d)
    return d


def rm_tmps():
    while TMP_DIRS:
        shutil.rmtree(TMP_DIRS.pop(), ignore_errors=True)

```
(la riga vuota finale del blocco è quella che separa da `SCREEN = ...`; `grep -n "^SCREEN" tests/rgb30/qemu_test.py` deve dire che `SCREEN` segue il blocco con UNA riga vuota in mezzo.)

Passo 3: i 19 siti. Sostituire OVUNQUE, nel solo `tests/rgb30/qemu_test.py`, la stringa `tempfile.mkdtemp(prefix="` con `mktmp(prefix="` (19 sostituzioni; `grep -c 'mktmp(prefix="' ` deve dare 19 e `grep -c 'tempfile.mkdtemp(prefix="'` zero; resta una sola `tempfile.mkdtemp(prefix=prefix)` nell'helper). Il resto di ogni riga resta com'è: per esempio `tmp = tempfile.mkdtemp(prefix="bm64kb-")` diventa `tmp = mktmp(prefix="bm64kb-")` e `self.tmp = tempfile.mkdtemp(prefix="bm64-")` diventa `self.tmp = mktmp(prefix="bm64-")`.
(Con sed: `sed -i 's/tempfile\.mkdtemp(prefix="/mktmp(prefix="/' tests/rgb30/qemu_test.py` — `sed -i` mantiene i CRLF.)

Passo 4: `main()`, ciclo dei test (riga ~1498-1508 prima del passo 2). Dopo la riga
```
            traceback.print_exc()
```
(una sola nel file, rientro 12 spazi) aggiungere, con rientro di 8 spazi, queste 2 righe, prima di `    print(f"{len(TESTS) - failed ...`:
```
        finally:
            rm_tmps()
```
Il blocco diventa:
```
        try:
            t(opts.build, opts)
            print(f"PASS {t.__name__} ({time.time() - t0:.1f}s)")
        except Exception:
            failed += 1
            print(f"FAIL {t.__name__}")
            traceback.print_exc()
        finally:
            rm_tmps()
```
`python3 -m py_compile tests/rgb30/qemu_test.py` deve passare (nessuna uscita; poi `rm -rf tests/rgb30/__pycache__` se si è creata).

## AGY-15b - verifica (bm-ops o bm-coder, 15 min)
Passo 0, una volta: il kernel virt. Nel worktree: `powershell -File scripts/wsl-run.ps1 "make -f rgb30.mk PLAT=virt all -j4 2>&1 | tail -3; ls build/rgb30-virt/kernel.elf" -Tail 10` (3 minuti la prima volta; `build/` è ignorata da git).
Passo 1: `powershell -File scripts/wsl-run.ps1 "df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l; python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k sdk_pad_sprite; echo EXIT=\$?; df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l" -Tail 30`
- Atteso: `PASS test_sdk_pad_sprite (…s)`, ` passed, 0 failed`, `EXIT=0`; il `wc -l` DOPO dà `0`; la colonna Used di `df` DOPO è uguale a quella PRIMA (nei numeri del planner `0` prima e `0` dopo; con altri processi che usano `/tmp` il numero assoluto cambia: fa fede `ls -d /tmp/bm64*`, che deve essere vuoto). Se prima c'erano `bm64*` non tuoi, il conteggio non è zero: confrontare prima e dopo (nessun nome NUOVO dopo).
- Il test deve passare come sul codice vecchio: se per `-k sdk_pad_sprite` compare un FAIL, riportarlo intero e `BLOCKED` (non toccare i test).
Passo 2 (facoltativo, solo se `/tmp` ha almeno 1 GB liberi): `-k sdk_pad_build` (usa il dump, `bm64kb-`): stesso controllo. Lascia scritto nella consegna se non fatto.
Passo 3: prova dell'helper. Creare `build/chk.py` (ignorata da git) con questo contenuto ESATTO (il modulo si carica con un altro nome perché `tests/rgb30/qemu_test.py` importa a sua volta `qemu_test` di `tests/`):
```
import importlib.util, os, sys
sys.path.insert(0, 'tests')
spec = importlib.util.spec_from_file_location('rgb30_qt', 'tests/rgb30/qemu_test.py')
q = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q)
d = q.mktmp('bm64chk-')
assert os.path.isdir(d)
q.rm_tmps()
print('ok', not os.path.exists(d))
```
poi `powershell -File scripts/wsl-run.ps1 "python3 build/chk.py" -Tail 5` → atteso `ok True` (provato dal planner).

Criterio di fine: passo 1 con `bm64*` a zero dopo il test e il test passa; `git diff --stat`: 1 file, 37 inserimenti, 19 cancellazioni; nessuna altra modifica (`git status --short` solo `tests/rgb30/qemu_test.py`).
Commit: un solo commit sul branch `claude/rgb30-test-tmp-cleanup`, messaggio `tests: rgb30 qemu_test removes its temporary folders after each test`. Push solo di questo branch.
Consegna: `DELIVERED AGY-15 @ claude/rgb30-test-tmp-cleanup@<commit> · verifica: qemu_test.py -k sdk_pad_sprite -> PASS, /tmp/bm64* prima <n> dopo 0 · 1 file, +37 -19 · non verificato su hardware (QEMU) · <ISO>` in `## Consegne` di `sync/AGY.md`, poi FREE. Agente consigliato: bm-coder (testo esatto).
Stima: 15 minuti di lavoro, 3 di compilazione del kernel virt la prima volta, ~25 s per il test.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Una funzione `mktmp()` + una pulizia in `main()` invece di un `try/finally` per sito: stesso effetto (le cartelle spariscono a fine test, anche se fallisce), diff piccolo, e la lettura del dump dopo `q.close()` resta valida. Raccomandata: sì. Alternativa: `tempfile.TemporaryDirectory` nei soli 3 siti `bm64kb-`, ma richiede di rientrare i corpi dei test e lascia le altre 16 cartelle (anche quelle `bm64sd-` da 64 MB, ai siti 262-652).
- D2 Si coprono TUTTI i 19 siti, non solo i 3 `bm64kb-`: i `bm64sd-`/`bm64sdk-` contengono anch'essi un'immagine SD (stesso rischio). Raccomandata: sì.
- D3 `ignore_errors=True` in `rmtree`: una cartella già sparita o bloccata non fa fallire un test che è passato. Raccomandata: sì.

## Prove per l'utente
Nessuna.
