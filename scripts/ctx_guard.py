#!/usr/bin/env python3
"""Hook UserPromptSubmit: avvisa quando il contesto supera una soglia e
chiede a Claude di chiudere la risposta con un `/compact ...` pronto.

Env: CTX_GUARD_WINDOW (token della finestra, default 1000000),
     CTX_GUARD_PCT (soglia %, default 12 = 120k su 1M), CTX_GUARD_STEP (riavviso ogni +N %, default 5 = +50k).
Se esiste .claude/state/better-prompt.json (plugin better-prompt >= 0.4.0) e coincide con questa sessione
(campo "session", oppure token entro il 15% dal transcript) usa i suoi token/finestra e aggiunge topic e divergenza;
altrimenti calcola dal transcript. Il primo avviso porta il template completo, i riavvisi una riga.
Stato in .claude/state/ctx_guard_<session>.json (ignorato da git).
"""
import json, os, sys

WINDOW = int(os.environ.get("CTX_GUARD_WINDOW", "1000000"))
PCT = float(os.environ.get("CTX_GUARD_PCT", "12"))
STEP = float(os.environ.get("CTX_GUARD_STEP", "5"))

MSG = (
    "[ctx_guard] Contesto al {pct:.0f}% ({tok}k/{win}k token), oltre la soglia {thr:.0f}%. Dopo aver risposto, chiudi con UN solo blocco di codice: la riga `/compact` seguita dal testo qui sotto, compilato dal contesto reale, così che un agente nuovo riparta SENZA rileggere la conversazione. Frasi corte e positive (\"fai X\"). Ogni dato volatile (hash, ID agente, stato di branch/CI) porta data e ora `AAAA-MM-GG hh:mm` e il comando che lo rilegge; più di 3 hash = il comando che li elenca. Ometti ciò che i file letti alla ripartenza (CLAUDE.md, memory, SYNC.md) già dicono. Struttura fissa, in quest'ordine:\n"
    "1 RUOLO (3-5 righe): chi sono, progetto/branch/worktree (path completo), cosa non è di mia competenza e a chi va passato, primi file/comandi da leggere alla ripartenza (in ordine).\n"
    "2 REGOLE DELL'UTENTE: solo istruzioni date a parole in chat (stile, fine giornata, impedimenti e tempi alti), ciascuna con data; un'eccezione porta data e portata.\n"
    "3 VINCOLI STABILI: solo permessi e soglie. Merge, push, PR, eliminazioni, CI, hardware: 'serve il via esplicito' o 'libero'; attribuzione nei commit/PR; costi; convenzioni (orari, lingua, lunghezza righe di log).\n"
    "4 STRUMENTI E PROTOCOLLO: comandi con sintassi minima, skill/agenti e limiti dei subagent; in un sync (SYNC.md) anche formati obbligatori, casella sync/inbox e procedura di consegna.\n"
    "5 STATO (max 15 righe): `ID · chi · stato · branch/hash · fonte`, raggruppate per operatori, integrazione, CI, repo pubblici, docs, agenti in corso (ID + cosa devono restituire). Solo lavori aperti e cambiati dall'ultimo compact; un dato superato si SOSTITUISCE. Ultima riga fissa: `PROSSIMO PASSO: ...` (da dove riparte il nuovo agente).\n"
    "6 IN SOSPESO CON L'UTENTE: max 8 voci numerate, una riga ciascuna, solo decisioni e consensi ancora aperti; un elenco lungo (prove hardware, branch) diventa il puntatore al file o comando che lo contiene.\n"
    "7 LEZIONI OPERATIVE: solo errori già avvenuti, una riga con il gesto corretto.\n"
    "SCARTA: output di comandi e letture già usate, ragionamenti intermedi, hash superati, tentativi falliti risolti, cortesia.\n"
    "Controllo finale: blocchi 1-4 e 7 identici al compact precedente salvo modifiche esplicite; durevoli (1-4, 7) max 60 righe, stato+sospesi (5-6) max 30. Nessun altro testo sul compact."
)

# Riavvisi (+STEP %) nella stessa conversazione: il template è già in contesto.
SHORT = (
    "[ctx_guard] Contesto al {pct:.0f}% ({tok}k/{win}k token). Chiudi la risposta con il blocco `/compact` aggiornato, "
    "stesso template a 7 blocchi già dato (compresa la riga PROSSIMO PASSO)."
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


def plugin_state(cwd, sid, tr_tok):
    """Stato esportato da better-prompt >= 0.4.0, oppure None.
    Il file è uno per cartella: se porta un'altra sessione, o token lontani dal transcript, è di un'altra sessione."""
    d = os.path.join(cwd, ".claude", "state")
    for name in ("better-prompt-%s.json" % sid, "better-prompt.json"):
        try:
            with open(os.path.join(d, name), encoding="utf-8") as f:
                s = json.load(f)
            if not (s["tokens"] > 0 and s["window"] > 0):
                continue
            if s.get("session") not in (None, sid):
                continue
            if tr_tok and abs(s["tokens"] - tr_tok) > 0.15 * max(s["tokens"], tr_tok):
                continue
            return s
        except (OSError, ValueError, KeyError, TypeError):
            pass
    return None


def main():
    try:
        d = json.load(sys.stdin)
    except ValueError:
        return
    sid = d.get("session_id", "x")
    tr_tok = context_tokens(d.get("transcript_path", ""))
    bp = plugin_state(d.get("cwd") or ".", sid, tr_tok)
    if bp:  # token e finestra esatti dal plugin
        tok, win = bp["tokens"], bp["window"]
    else:
        tok, win = tr_tok, WINDOW
    pct = 100.0 * tok / win
    sdir = os.path.join(d.get("cwd") or ".", ".claude", "state")
    sfile = os.path.join(sdir, "ctx_guard_%s.json" % sid)
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
    text = (SHORT if seen else MSG).format(pct=pct, tok=tok // 1000, win=win // 1000, thr=PCT)
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "UserPromptSubmit",
        "additionalContext": text + ("" if seen else _topics(bp))}}))


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
