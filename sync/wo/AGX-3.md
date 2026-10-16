# AGX-3 - PI-QEMU-TMP: i test QEMU del Pi non lasciano più cartelle temporanee in `/tmp` (`Qemu.close()` e 4 test)
Assegnato ad AGX (16 ott), indipendente dagli altri WO. Ogni `Qemu(...)` crea una cartella `bm-XXXX` con `mon.sock` e `qmp.sock` che nessuno cancella, e quattro test non rimuovono la propria cartella `tmp` (uno la riempie con un'immagine SD da 4 GiB): su WSL il `/tmp` da 3,9 GB si riempie e gli altri processi falliscono con `No space left on device`.
Base: `origin/bm-core` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/qemu-test-tmp-cleanup C:\Users\Utente\Documents\GitHub\bm33-agx3 origin/bm-core`); branch `claude/qemu-test-tmp-cleanup`. Mai push su bm-core; push solo di questo branch.
Perimetro: solo `tests/qemu_test.py`: 1 riga in `Qemu.close()` e 1 riga nel `finally:` di ciascuno di 4 test (5 righe in tutto).
NON toccare: qualunque altro file (`tests/rgb30/qemu_test.py`, `Makefile`, `docs/*`, `sync/*`, `SYNC.md`), il resto di `tests/qemu_test.py` (NON ristrutturare i test, NON spostare `q = Qemu(...)` dentro `try:`, NON toccare gli altri test che già fanno `shutil.rmtree`), nessun timeout, nessun `expect`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host; QEMU non prova nulla dell'hardware). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: dopo un test, `/tmp` di WSL non contiene più cartelle `bm-*` create dal test stesso, e i test toccati danno lo stesso esito di prima.

## Stato reale (verificato dal planner su `origin/bm-core` @ `e6d7bfd7`, in WSL, con un `make -j4` completo)
- `tests/qemu_test.py` ha `import shutil` alla riga 15. `class Qemu`: `self.tmp = tempfile.mkdtemp(prefix="bm-")` (riga 96), poi `mon_path`/`qmp_path` dentro `self.tmp` (97-98); `self.tmp` è letto solo dentro la classe (righe 96, 97, 98, 195: `grep -n "\.tmp\b" tests/qemu_test.py` deve dare esattamente queste 4). `Qemu.close()` (righe 203-205) è:
```
    def close(self):
        self.proc.kill()
        self.proc.wait()
```
  e non cancella `self.tmp`.
- Le funzioni con `tempfile.mkdtemp` ma senza alcun `rmtree` sono 4 (più `Qemu.__init__`, che si risolve con `close()`). Il planner le ha trovate con un controllo `ast` (funzioni il cui testo contiene `mkdtemp` e non contiene `rmtree`). Numeri di riga su `bm-core` `e6d7bfd7` (sul catalogo erano quelli di `bm33-int2`, diversi per `test_pad_typing`):

| test | `def` | `mkdtemp` | ultima riga del `finally:` | tmp da cancellare |
|---|---|---|---|---|
| `test_sd_cartridges` | 951 | 952 | 997 (`        q.close()`) | immagine SD da 128 MiB |
| `test_sd_sdhc_and_usb_menu` | 3792 | 3794 | 3835 (`        os.remove(img)`) | cartella dell'immagine da 4 GiB (`os.remove(img)` toglie il file, non la cartella) |
| `test_monitor_line` | 5131 | 5138 | 5173 (`        q.close()`) | immagine e config |
| `test_pad_typing` | 6910 | 6919 | 7071 (`        q.close()`) | immagine SD |

- Prove del planner (su una copia pulita di `bm-core`, build completo, QEMU raspi0 in WSL):
  - PRIMA della modifica: `python3 tests/qemu_test.py --build build -k monitor_line` lascia 2 cartelle in `/tmp` (`ls -d /tmp/bm-* | wc -l` da 0 a 2) e un `mon.sock` (`find /tmp -maxdepth 2 -name mon.sock | wc -l` da 0 a 1); `-k sd_cartridges` altre 2 cartelle.
  - DOPO la modifica: `-k sd_cartridges`, `-k monitor_line`, `-k pad_typing`: `ls -d /tmp/bm-* | wc -l` resta 0 e `mon.sock` resta 0 dopo ogni test. `monitor_line` e `pad_typing` danno `1/1 passed`.
  - ATTENZIONE, due fatti da conoscere (non sono colpa della tua modifica):
    1. `-k sd_cartridges` dava `0/1 passed` anche PRIMA della modifica, sulla copia pulita del planner (`AssertionError` alla riga 971, `assert s_ in screen`: il menu delle cartucce non mostra "Games"/"bm native demo" come atteso). Se lo vedi rosso anche tu, ripeti con `git stash` (modifica tolta): se è rosso uguale, NON è la tua modifica; scrivilo nella consegna come `sd_cartridges rosso anche senza la modifica (riga 971)` e usa il conteggio delle cartelle come prova.
    2. `-k sd_sdhc_and_usb_menu` crea un'immagine da 4 GiB: nel `/tmp` di WSL (3,9 GB) NON ci sta (riempie il disco: "No space left on device", e lascia la cartella da 3,9 GB). Va lanciato con `TMPDIR=$PWD/build/tmp` (su disco C:, 269 GB liberi), dura circa 6 minuti e, sul computer del planner, è finito con `ConnectionRefusedError` (la connessione alla seriale di QEMU è stata rifiutata; causa non indagata, forse l'immagine da 4 GiB su `/mnt/c`). Con la modifica applicata, a fine corsa sono rimaste sia la cartella `bm-sd-*` (4 GiB) sia la cartella `bm-*` di QEMU: l'errore è quindi avvenuto dentro `Qemu(...)`, PRIMA del `try:`, e il `finally:` non può ripulire (dedotto dalle cartelle rimaste, non dallo stack). È un limite noto di questo WO (vedi Decisioni D1). Per quel test la prova richiesta è solo che il codice cambiato sia corretto (`python -m py_compile`) e, se il test arriva a `finally`, che la cartella sparisca; altrimenti scrivi `sd_sdhc_and_usb_menu non verificato (QEMU non parte con l'immagine da 4 GiB)`, e CANCELLA tu (con il percorso esatto, uno per uno) le cartelle `build/tmp/bm-sd-*` e `build/tmp/bm-*` che la tua corsa ha lasciato (sono da 4 GiB: non lasciarle).
- Fine riga: `tests/qemu_test.py` in Windows ha CRLF (nel repo è LF, `core.autocrlf=true`); i tuoi inserimenti usano il fine riga che il file ha già. `git diff --stat` deve dire `tests/qemu_test.py | 5 +++++`; se dice migliaia di righe hai cambiato i fine riga: `git checkout -- tests/qemu_test.py` e rifai.

## AGX-3a - le 5 righe (bm-coder, 15 min)
Passo 0: ambiente. Crea il worktree (riga "Base"). Poi prepara la build, che serve ai test QEMU: `scripts/wsl-run.ps1 'make -j4 > build/make.log 2>&1; echo EXIT=$?' -Tail 120` (la prima volta dura 10-20 minuti; metti `run_in_background` e leggi `build/make.log`). Atteso `EXIT=0` e `build/kernel.img` presente. Controlla il `/tmp` di WSL: `scripts/wsl-run.ps1 'df -h /tmp | tail -1; ls -d /tmp/bm-* 2>/dev/null | wc -l' -Tail 120`: ANNOTA il numero di cartelle `bm-*` (di solito 0; se è >0 non sono tue: non toccarle e confronta solo le differenze).

Passo 1: `Qemu.close()`. Trova con `git grep -n "self.proc.wait()" -- tests/qemu_test.py` (deve dare UNA riga, oggi la 205; se ne dà di più, è quella dentro `def close(self):`). Subito DOPO quella riga aggiungi, con 8 spazi di rientro:
```
        shutil.rmtree(self.tmp, ignore_errors=True)
```
Risultato:
```
    def close(self):
        self.proc.kill()
        self.proc.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)
```

Passo 2: i 4 test. Per ciascuno trova il `def` con `git grep -n "^def test_sd_cartridges(\|^def test_sd_sdhc_and_usb_menu(\|^def test_monitor_line(\|^def test_pad_typing(" -- tests/qemu_test.py` (4 righe). In ogni funzione cerca l'ULTIMO blocco `    finally:` (4 spazi) e la sua ultima riga di corpo (8 spazi), cioè l'ultima riga prima del `def` o della riga vuota che segue (tabella sopra). Subito DOPO quell'ultima riga del corpo del `finally:` aggiungi, con 8 spazi di rientro:
```
        shutil.rmtree(tmp, ignore_errors=True)
```
Risultati attesi. `test_sd_cartridges`, `test_monitor_line` e `test_pad_typing`:
```
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)
```
`test_sd_sdhc_and_usb_menu`:
```
    finally:
        q.close()
        os.remove(img)
        shutil.rmtree(tmp, ignore_errors=True)
```
Non toccare altro: i 4 test hanno già `tmp` definito prima del `try:`, quindi `tmp` è in scope nel `finally:`.
Controllo: `git diff -U0 -- tests/qemu_test.py | grep -c '^+        shutil.rmtree'` deve dare 5; `python -m py_compile tests/qemu_test.py` senza uscita (se compare `tests/__pycache__` è ignorato da git).

Passo 3: verifica (un test alla volta; il comando, da passare a `scripts/wsl-run.ps1 '<cmd>' -Tail 120`, è questo):
```
python3 tests/qemu_test.py --build build -k monitor_line; echo "bmdirs=$(ls -d /tmp/bm-* 2>/dev/null | wc -l) mon=$(find /tmp -maxdepth 2 -name mon.sock 2>/dev/null | wc -l)"
```
Atteso: `1/1 passed`; `bmdirs` e `mon` uguali al valore annotato al passo 0 (cioè nessuna cartella in più). Ripeti con `-k pad_typing` (atteso `1/1 passed`, stessi conteggi) e con `-k sd_cartridges` (atteso `1/1 passed`; se è rosso vedi il fatto 1 sopra; i conteggi devono essere comunque invariati). Il test `sd_sdhc_and_usb_menu`: vedi il fatto 2 sopra (solo con `TMPDIR=$PWD/build/tmp`, mai nel `/tmp` di WSL).
Controllo di sensibilità (facoltativo, 1 minuto): `git stash`, rilancia `-k monitor_line` e vedi `bmdirs` salire di 2 (il bug), `git stash pop`; se non lo fai scrivi `non verificato che il conteggio salga senza la modifica` (il planner l'ha visto: 0 -> 2).
Se compare un errore diverso (QEMU non parte in WSL, `NEXT` §7 lo cita): riporta le ultime 20 righe e `BLOCKED`, non cambiare i test.

Criterio di fine: `git diff --stat` = solo `tests/qemu_test.py | 5 +++++`; `monitor_line` e `pad_typing` `1/1 passed` senza cartelle `bm-*` in più in `/tmp`; `sd_cartridges` senza cartelle in più (esito uguale a prima della modifica); `sd_sdhc_and_usb_menu` come da fatto 2.
Commit: un solo commit sul branch `claude/qemu-test-tmp-cleanup`, messaggio `tests: qemu_test removes its temporary folders (Qemu.close and four tests)`. Push solo di questo branch (`git push -u origin claude/qemu-test-tmp-cleanup`).

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-3 --repo C:\Users\Utente\Documents\GitHub\bm33-agx3 --branch claude/qemu-test-tmp-cleanup --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-3 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-3 @ claude/qemu-test-tmp-cleanup@<sha> · verifica: python3 tests/qemu_test.py --build build -k monitor_line -> 1/1 passed, 0 cartelle bm-* lasciate in /tmp (prima: 2); pad_typing 1/1; sd_cartridges <esito>; sd_sdhc <esito o non verificato> · 1 file, 5 righe · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-3 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto); il passo 3 è lungo ma meccanico.
Stima: 35 minuti (15 di modifica, 10-20 di build la prima volta, 10 di test; più 6 se provi `sd_sdhc_and_usb_menu`).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si segue la forma richiesta (rmtree nel `finally:` dei 4 test), che non ripulisce se `Qemu(...)` stesso fallisce prima del `try:` (come per `sd_sdhc_and_usb_menu` sul computer del planner). Lo spostamento di `q = Qemu(...)` dentro `try:` cambierebbe la struttura dei test: fuori da questo WO. Raccomandata: sì. Alternativa: WO a parte con `try`/`finally` anche nel costruttore.
- D2 `Qemu.close()` cancella `self.tmp` anche se il test fallisce: i socket di debug spariscono, ma non servivano a nessuno (nessun test legge `self.tmp` dopo `close()`: `grep -n "\.tmp\b"`). Raccomandata: sì.
- Coordinamento: altri branch non ancora fusi (API-1, APP-5b, `claude/ci-repair`) toccano `tests/qemu_test.py` in altri punti; queste 5 righe sono lontane dalle loro. Se `git merge`/`rebase` dà un conflitto è di SPV.

## Prove per l'utente
Nessuna.
