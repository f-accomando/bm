# AGY-17 - DOCS-CHECK: `docs_audit.py check` elenca i link rotti e esce 1 (serve a `make test-docs`)
Assegnato ad AGY (16 ott), indipendente dagli altri WO (AGX-5 dipende da questo). Oggi `make test-docs` lancia `docs_audit.py score`, che esce sempre 0 anche con link rotti: un link rotto costa solo punti di penalità e nessuno lo vede. Serve un comando che fallisca.
Base: `origin/bm-core` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-audit-check C:\Users\Utente\Documents\GitHub\bm33-agy17 origin/bm-core`); branch `claude/docs-audit-check`. Mai push su bm-core; push solo di questo branch.
Perimetro: `scripts/docs_audit.py` (si sposta un blocco in una funzione nuova, si aggiunge un comando) e il file NUOVO `tests/tools/test_docs_audit.py`.
NON toccare: `Makefile` (lo cambia AGX-5, dopo questo WO), `docs/*`, `CLAUDE.md`, `SYNC.md`, `sync/*`, le funzioni `cmd_rereads`, `cmd_repeats`, `cmd_log`, `cmd_best`, e l'OUTPUT di `docs_audit.py score` (deve restare identico, parola per parola).
Vincoli: solo host, nessun WSL necessario (il `python` di Windows basta; WSL solo con `scripts/wsl-run.ps1 ... -Tail 120`, e `/tmp` di WSL è condiviso e piccolo); `not verified on hardware`. I numeri di riga sono stati aperti dal planner su `origin/bm-core` @ `e6d7bfd7` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi sotto) e NON fidarti del numero. Ciò che non ritrovi come descritto: `non verificato` e `BLOCKED`.
Obiettivo: `python scripts/docs_audit.py check` stampa ogni link rotto come `  ! <messaggio>` ed esce 1; senza problemi stampa `docs_audit: 0 broken` ed esce 0; `score` stampa le stesse righe di prima.

## Stato reale (verificato dal planner: ho applicato questo stesso testo a una copia fuori dal repo; test e `score` eseguiti)
- `scripts/docs_audit.py`: `cmd_score(as_json)` (oggi riga 53) contiene, dopo il ciclo degli orfani (`for f in files[2:]:` … `orphan:`), questo blocco (oggi righe 63-71), da `    for f in files:` fino alla riga `pen.append((P_BROKEN, f"missing file named in {f}: {p}"))`:
```
    for f in files:
        for l in md_links(f):
            if not os.path.exists(os.path.join(ROOT, l)):
                pen.append((P_BROKEN, f"broken link in {f}: {l}"))
        for m in re.finditer(r"`((?:docs|scripts|src|carts|tests)/[\w./-]+\.\w+)`", rd(f)):
            p = m.group(1)
            vague = "/." in p or (not p.startswith(("docs/", "scripts/")) and p.count("/") < 2)
            if not vague and "*" not in p and not os.path.exists(os.path.join(ROOT, p)) and not p.endswith(("-IT.md",)):
                pen.append((P_BROKEN, f"missing file named in {f}: {p}"))
```
  Comando per ritrovarlo: `git grep -n "broken link in" -- scripts/docs_audit.py` (una riga) e `git grep -n "missing file named in" -- scripts/docs_audit.py` (una riga).
- `ROOT` (riga 19) è una variabile di modulo; `rd`, `tracked`, `md_links` la leggono quando vengono chiamate: un test può cambiarla.
- La CLI è in fondo (`if __name__ == "__main__":`, oggi riga 181-195) con una catena `if c == "score": … elif c == "best": … else: sys.exit(__doc__)`.
- Sulla base `e6d7bfd7`: `python scripts/docs_audit.py score` dice `score 6646  (always loaded 5066 + mean area 1580 + penalties 0)`; il planner ha verificato che `check` darebbe `docs_audit: 0 broken` ed exit 0 (il numero 6646 cambia solo se cambiano i docs: fa fede il confronto prima/dopo del TUO worktree).
- Fine riga: `git ls-files --eol scripts/docs_audit.py` mostra `w/lf` o `w/crlf`: mantieni quello del file nelle righe nuove. Il file nuovo, se `git ls-files --eol` dei test vicini (`tests/tools/test_agent_audit.py`) dice `w/crlf`, va bene con CRLF o LF: `git add` lo normalizza.

## AGY-17a - la modifica (bm-coder, 20 min)
Passo 0 (misura PRIMA): dalla radice del worktree crea la cartella `build` se manca (`mkdir build`; è ignorata da git) e lancia `python scripts/docs_audit.py score > build/score_before.txt` (PowerShell: stesso comando; cartella ignorata da git, nessun file entra nel commit). Apri il file: la prima riga è `score N  (always loaded … + penalties P)`. Segna N e P.

Passo 1: funzione `broken(files)`. Subito PRIMA della riga `def cmd_score(as_json):` inserisci ESATTAMENTE (poi DUE righe vuote prima di `def cmd_score`):
```
def broken(files):
    """Messages for the broken links and the missing files named in `files` (each one costs P_BROKEN in the score)."""
    msgs = []
    for f in files:
        for l in md_links(f):
            if not os.path.exists(os.path.join(ROOT, l)):
                msgs.append(f"broken link in {f}: {l}")
        for m in re.finditer(r"`((?:docs|scripts|src|carts|tests)/[\w./-]+\.\w+)`", rd(f)):
            p = m.group(1)
            vague = "/." in p or (not p.startswith(("docs/", "scripts/")) and p.count("/") < 2)
            if not vague and "*" not in p and not os.path.exists(os.path.join(ROOT, p)) and not p.endswith(("-IT.md",)):
                msgs.append(f"missing file named in {f}: {p}")
    return msgs


def cmd_check():
    msgs = broken(tracked())
    for m in msgs:
        print("  !", m)
    if msgs:
        sys.exit(1)
    print("docs_audit: 0 broken")


```
(`broken` e `cmd_check` stanno prima di `cmd_score`; l'espressione regolare e le condizioni sono copiate IDENTICHE dal blocco originale: copia dal file, non riscrivere a mano.)

Passo 2: in `cmd_score` sostituisci l'intero blocco da `    for f in files:` (quello con `md_links(f)`) fino a `pen.append((P_BROKEN, f"missing file named in {f}: {p}"))` compresa, con ESATTAMENTE queste 2 righe:
```
    for m in broken(files):
        pen.append((P_BROKEN, m))
```
Il ciclo successivo (`    for f in files[2:]:` con `n = rd(f).count("\n")`) resta com'è, subito dopo.

Passo 3: nella catena CLI, subito PRIMA di `    elif c == "best":` aggiungi ESATTAMENTE:
```
    elif c == "check":
        cmd_check()
```

Passo 4: docstring in cima al file. Dopo la riga `  docs_audit.py score [--json]   one number (lower is better) and what makes it up` aggiungi la riga
```
  docs_audit.py check            list the broken links and the named files that are missing (exit 1 if any)
```
(`git grep -n "docs_audit.py score" -- scripts/docs_audit.py` la trova: riga 4.)

Passo 5: crea `tests/tools/test_docs_audit.py` con questo contenuto ESATTO:
```
"""Tests for `docs_audit.py check`: it must list the broken links of the tracking docs and exit 1."""
import contextlib, io, os, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "scripts"))
import docs_audit


def put(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8", newline="\n") as fh:
        fh.write(text)


def run_check():
    out = io.StringIO()
    code = 0
    with contextlib.redirect_stdout(out):
        try:
            docs_audit.cmd_check()
        except SystemExit as e:
            code = e.code
    return code, out.getvalue()


def main():
    old_root = docs_audit.ROOT
    try:
        with tempfile.TemporaryDirectory() as tmp:
            docs_audit.ROOT = tmp
            put(tmp, "CLAUDE.md", "# Notes\nThe index is docs/progress.md\n")
            put(tmp, "docs/progress.md", "# Index\n- [area a](progress/a.md)\n")
            put(tmp, "docs/progress/a.md", "# A\nSee [gone](missing.md).\n")
            code, out = run_check()
            assert code == 1, f"exit {code}, expected 1 (output: {out!r})"
            lines = [l for l in out.splitlines() if l.startswith("  !")]
            assert len(lines) == 1, f"expected 1 message, got {lines}"
            assert "docs/progress/missing.md" in lines[0], lines[0]
            assert len(docs_audit.broken(docs_audit.tracked())) == 1
            put(tmp, "docs/progress/a.md", "# A\nNothing broken here.\n")
            code, out = run_check()
            assert code == 0, f"exit {code}, expected 0 (output: {out!r})"
            assert "docs_audit: 0 broken" in out, out
            assert docs_audit.broken(docs_audit.tracked()) == []
    finally:
        docs_audit.ROOT = old_root
    print("All docs_audit tests PASSED")


if __name__ == "__main__":
    main()
```

## AGY-17b - verifica
1. `python tests/tools/test_docs_audit.py; "EXIT=$LASTEXITCODE"` (bash: `; echo EXIT=$?`). Atteso: `All docs_audit tests PASSED`, EXIT=0 (provato dal planner).
2. `python scripts/docs_audit.py score > build/score_after.txt` poi confronto con il file del passo 0: `git diff --no-index --stat build/score_before.txt build/score_after.txt` deve dire nulla (file uguali: stessa riga `score N`, stesse righe di token e di penalità). Se differiscono: hai cambiato `score` e va corretto prima di consegnare.
3. `python scripts/docs_audit.py check; "EXIT=$LASTEXITCODE"`. Atteso sulla base: `docs_audit: 0 broken` ed EXIT=0. Scrivi nella consegna l'esito VERO sul tuo worktree (è l'informazione che serve ad AGX-5): se esce 1, incolla le righe `  !`.
4. Prova a mano: crea un link rotto temporaneo, cioè aggiungi a `docs/progress/sync.md` in fondo la riga `[x](nonesiste.md)`, lancia `python scripts/docs_audit.py check`: atteso una riga `  ! broken link in docs/progress/sync.md: docs/progress/nonesiste.md` ed EXIT=1; poi `git checkout -- docs/progress/sync.md` SUBITO. `git status --short` finale deve elencare solo i due file del perimetro (più i due `build_score_*.txt` non tracciati: cancellali con `Remove-Item build/score_before.txt, build/score_after.txt`).
5. `python -m py_compile scripts/docs_audit.py tests/tools/test_docs_audit.py` senza uscita; cancella le cartelle `__pycache__` che hai creato.

Criterio di fine: punti 1-5 come sopra; `git diff --stat` finale: `scripts/docs_audit.py` circa 30 righe aggiunte e 9 tolte, `tests/tools/test_docs_audit.py` nuovo (~50 righe); nessun altro file. `docs_audit.py score` identico a prima.
Commit: un solo commit sul branch `claude/docs-audit-check`, messaggio `docs_audit: check command lists broken links and exits 1`. Push solo di questo branch (`git push -u origin claude/docs-audit-check`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-17 --repo C:\Users\Utente\Documents\GitHub\bm33-agy17 --branch claude/docs-audit-check --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-17 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-17 @ claude/docs-audit-check@<sha> · verifica: python tests/tools/test_docs_audit.py -> All docs_audit tests PASSED; docs_audit.py score identico a prima; docs_audit.py check su base -> <esito vero: "docs_audit: 0 broken", exit 0> · 2 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-17 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto); le decisioni, se serve, vanno a SPV.
Stima: 30 minuti di lavoro; nessuna compilazione (secondi per test e `score`).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Il test importa il modulo e cambia `ROOT` invece di lanciare un sottoprocesso: `ROOT` è calcolato dal percorso del file, quindi solo così si può puntare a una cartella temporanea. Raccomandata: sì.
- D2 `check` conta anche i "missing file named in" (file nominati nei docs e non esistenti), non solo i link `.md`: sono le stesse penalità P_BROKEN di `score`. Raccomandata: sì.
- D3 `Makefile` (`test-docs` → `check`) NON in questo WO: lo fa AGX-5 dopo questa consegna.

## Prove per l'utente
Nessuna.
