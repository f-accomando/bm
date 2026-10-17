# CO-7 - text of the CLOSEOUT rule (bm-light + SPV, only after the user's go)

Order: after CO-1..CO-6 are APPROVED. Start only when the user gives the go.
Branch: not set by this WO; SPV sets it at assignment.

Objective: one RULE CLOSEOUT replaces the pending SIGNOFF rule, and the rule text is written once and referenced from the other documents.

Files: `SYNC.md` §3b "Chiusura" (lines 129-134, to be replaced) and §5 line 191 (add `CLOSEOUT`); `docs/progress/sync.md` line 71 and its Rules; `CLAUDE.md` §6 paragraph "Sign-off".

Change:
- Opening: CLOSEOUT opens only on the user's request. SPV announces it with one Log line `CLOSEOUT` and runs it in phases (C0 DRAIN, C1 SIGNOFF, C2 READY, C3 DOCS, C4 HANDOFF, C5 CLEAN, C6 END). The next phase starts only when the output of the previous one is verified by a script.
- Phases and the table of CLOSEOUT.md §2b go in `SYNC.md` §3b; the other two documents get one reference line: "closing a session = CLOSEOUT, see SYNC.md §3b".
- Permissions rule, before DRAIN: SPV lists the permissions the operators need from the user (`sync_hdr.py`, push to the sync branch, bridge edits), so the user grants them all at once. The list is written in the Log before the `CLOSEOUT DRAIN` line; CO-4 checks it.
- Then the Log line `RULE CLOSEOUT` with ack required from all operators (written by SPV).

Verification command: `python scripts/docs_audit.py score` with no broken links; `python tools/sync_check.py` → 0 Log lines over 600 characters.
Designated agent: bm-light (SPV reviews the text).
