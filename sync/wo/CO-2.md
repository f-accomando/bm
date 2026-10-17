# CO-2 - tests for the closing states (bm-coder)

Order: after CO-1 is APPROVED (same file chain: CO-1..CO-4 all touch `tools/sync_check.py`).
Branch: not set by this WO; SPV sets it at assignment.

Objective: a test module covers every closing state and the READY / END / NOT-READY outputs.

Files: new `tests/tools/test_sync_check_closeout.py`, modelled on `tests/tools/test_spv_brief.py` (temporary directory with `SYNC.md`, `sync/operators.toml` and `sync/<ID>.md`; `sc.ROOT` pointed at the temporary directory).

Change: one test for each state of CLOSEOUT.md §2b (ACTIVE, DRAINING, SIGNOFF-SENT, SIGNOFF-ACK, SIGNED-OFF, UNRESPONSIVE, FORCED); one test for READY, one for END, one for NOT-READY with exit 1 under `--strict`; one test where a handshake after the SIGNOFF brings the operator back to ACTIVE.

Verification command: `python -m unittest tests.tools.test_sync_check_closeout -v` → all OK.
Designated agent: bm-coder.
