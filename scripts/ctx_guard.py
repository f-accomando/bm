#!/usr/bin/env python3
"""Hook UserPromptSubmit: avvisa quando il contesto supera una soglia e
chiede a Claude di chiudere la risposta con un `/compact ...` pronto.

Env: CTX_GUARD_WINDOW (token della finestra, default 1000000),
     CTX_GUARD_PCT (soglia %, default 20 = 200k su 1M), CTX_GUARD_STEP (riavviso ogni +N %, default 10 = +100k).
Se esiste .claude/state/better-prompt.json (plugin better-prompt >= 0.4.0) usa i suoi token/finestra e aggiunge topic e divergenza;
altrimenti calcola dal transcript. Stato in .claude/state/ctx_guard_<session>.json (ignorato da git).
"""
import json, os, sys

WINDOW = int(os.environ.get("CTX_GUARD_WINDOW", "1000000"))
PCT = float(os.environ.get("CTX_GUARD_PCT", "20"))
STEP = float(os.environ.get("CTX_GUARD_STEP", "10"))

MSG = (
    "[ctx_guard] Contesto al {pct:.0f}% ({tok}k/{win}k token), oltre la soglia {thr:.0f}%. Dopo aver risposto, chiudi con UN solo blocco di codice: la riga `/compact` seguita dal testo qui sotto, compilato dal contesto reale, così che un agente nuovo riparta SENZA rileggere la conversazione. Frasi corte e positive (\"fai X\"). Ogni dato volatile (hash, ID agente, stato di branch/CI) porta data/ora e la fonte da cui rileggerlo; in caso di dubbio vince la fonte. Struttura fissa, in quest'ordine:\n"
    "1 RUOLO (3-5 righe): chi sono, progetto/branch/worktree (path completo), cosa non è di mia competenza e a chi va passato, primi file/comandi da leggere alla ripartenza (in ordine).\n"
    "2 REGOLE DELL'UTENTE (testuali, con data): stile di conversazione, cosa si fa a fine giornata o su richiesta, come comportarsi davanti a impedimenti e tempi alti; un'eccezione dell'utente va scritta con data e portata.\n"
    "3 VINCOLI STABILI: consensi richiesti (merge, push, PR, eliminazioni, rerun/cancel CI, azioni sull'hardware), ciascuno 'serve il via esplicito' o 'libero'; attribuzione nei commit/PR; soglie di costo; convenzioni (orari, lingua, lunghezza righe di log).\n"
    "4 STRUMENTI E PROTOCOLLO: comandi fissi con sintassi minima, skill/agenti e limiti dei subagent; se la sessione è in un sync (SYNC.md) anche formati obbligatori (preso/concluso WO, DELIVERED, REVIEW, SIGNOFF, RULE, da <ID> a <ID>), stati operatore, casella sync/inbox, procedura di consegna, costo dei battiti.\n"
    "5 STATO (max 15 righe): una riga per lavoro aperto `ID · chi · stato · ultimo hash/branch · prossimo passo · fonte`, ogni riga con data e fonte, raggruppate per operatori, branch e integrazione, CI, repo pubblici, docs da aggiornare, agenti in corso (ID + cosa devono restituire). Scrivi solo ciò che è cambiato dall'ultimo compact; un dato superato si SOSTITUISCE.\n"
    "6 IN SOSPESO CON L'UTENTE: elenco numerato di decisioni/consensi attesi, una riga ciascuno, senza quelle risolte (includi prove hardware di fine giornata e doveri di fine sessione).\n"
    "7 LEZIONI OPERATIVE: solo quelle che hanno già causato errori, una riga con il comando o il gesto corretto.\n"
    "SCARTA: output di comandi e letture di file già usate, ragionamenti intermedi, hash superati, tentativi falliti già risolti, conversazione di cortesia.\n"
    "Prima di emettere: i blocchi 1-4 e 7 restano identici al compact precedente salvo modifiche esplicite (non riscriverli da zero); nessun dato volatile senza data e fonte; blocchi durevoli (1-4, 7) max 60 righe, stato+sospesi (5-6) max 30. Nessun altro testo sul compact."
)


def context_tokens(path):
    """Token dell'ultimo messaggio assistant (input + cache), 0 se non trovati."""
    last = 0
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                u = (e.get("message") or {}).get("usage") if isinstance(e.get("message"), dict) else None
                if u and not e.get("isSidechain"):
                    last = (u.get("input_tokens", 0) + u.get("cache_read_input_tokens", 0)
                            + u.get("cache_creation_input_tokens", 0)) or last
    except OSError:
        pass
    return last


def plugin_state(cwd):
    """Stato esportato da better-prompt >= 0.4.0, oppure None."""
    try:
        with open(os.path.join(cwd, ".claude", "state", "better-prompt.json"), encoding="utf-8") as f:
            s = json.load(f)
        if s["tokens"] > 0 and s["window"] > 0:
            return s
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None


def main():
    try:
        d = json.load(sys.stdin)
    except ValueError:
        return
    bp = plugin_state(d.get("cwd") or ".")
    if bp:  # token e finestra esatti dal plugin
        tok, win = bp["tokens"], bp["window"]
    else:
        tok, win = context_tokens(d.get("transcript_path", "")), WINDOW
    pct = 100.0 * tok / win
    sdir = os.path.join(d.get("cwd") or ".", ".claude", "state")
    sfile = os.path.join(sdir, "ctx_guard_%s.json" % d.get("session_id", "x"))
    try:
        with open(sfile) as f:
            seen = json.load(f).get("pct", 0)
    except (OSError, ValueError):
        seen = 0
    if pct < seen - 5:  # contesto sceso: compact avvenuto, riarma
        seen = 0
    if pct < PCT or (seen and pct < seen + STEP):
        if pct < PCT and seen:
            _save(sdir, sfile, 0)
        return
    _save(sdir, sfile, pct)
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "UserPromptSubmit",
        "additionalContext": MSG.format(pct=pct, tok=tok // 1000, win=win // 1000, thr=PCT)
        + _topics(bp)}}))


def _topics(bp):
    if not bp or not bp.get("topics"):
        return ""
    names = bp["topics"]
    return (" Topic della chat: %s (divergenza %s%%). Nel blocco 5 (STATO) tieni solo l'ultimo topic (%s); "
            "i precedenti vanno in SCARTA." % (", ".join(names), bp.get("div", 0), names[-1]))


def _save(sdir, sfile, pct):
    try:
        os.makedirs(sdir, exist_ok=True)
        with open(sfile, "w") as f:
            json.dump({"pct": pct}, f)
    except OSError:
        pass


if __name__ == "__main__":
    main()
