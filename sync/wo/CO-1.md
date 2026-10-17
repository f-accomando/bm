# CO-1 - sync_check --closeout: closing state per operator (bm-coder)

Order: first of the CO-1..CO-4 chain (all four touch `tools/sync_check.py`); start only after the CLOSEOUT rule is APPROVED (CO-7). Do not run in parallel with CO-2, CO-3 or CO-4.
Branch: not set by this WO; SPV sets it at assignment.

Objective: `tools/sync_check.py` computes the closing state of every operator and prints `CLOSEOUT-READY` when all of them are closed.

Files: `tools/sync_check.py`: `main()`; new functions `closeout_state(oid, h, text, log, now) -> str` and `closeout_rows(log)`.

Change: implement the states of the table in CLOSEOUT.md §2b and the timeouts of §2d.
- Log lines: the action is `line.split("|")[4]` with backticks removed; it is `SIGNOFF` or `CLOSEOUT`.
- FORCED: a message that starts with `SIGNOFF <ID> FORCED`.
- CLOSED line: in the `## Chiusura` section, regex `^[-*]?\s*CLOSED\s+@\s+\S+@\S+\s+·\s+tree clean\s+·\s+aperti:\s+.+·\s+\d{4}-\d\d-\d\dT\d\d:\d\d`.
- With `--closeout`: a `closeout` column. The last line is `CLOSEOUT-READY` if every active operator other than SPV is SIGNED-OFF or FORCED; `CLOSEOUT-END` if there is also a `CLOSEOUT` line whose message starts with `END` and SPV is DONE; otherwise `CLOSEOUT-NOT-READY: <ID>=<state>,...`.
- `--strict --closeout` exits with 1 when NOT-READY.

Verification command: `python -m unittest tests.tools.test_sync_check_closeout` → returncode 0 (tests come from CO-2).
Designated agent: bm-coder.
