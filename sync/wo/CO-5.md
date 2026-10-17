# CO-5 - sync_hdr --closed: write the closing line (bm-coder)

Order: independent of CO-1..CO-4 (touches `tools/sync_hdr.py` only); may run after CO-2 is APPROVED.
Branch: not set by this WO; SPV sets it at assignment.

Objective: an operator writes the `## Chiusura` line with one command, instead of editing the file by hand.

Files: `tools/sync_hdr.py`, argparse block at lines 163-183: new option `--closed OPEN` (`nessuno` or the IDs of the open WOs).

Change:
- `--closed` requires `--branch`. The commit is read from `--commit` or, when missing, `git rev-parse --short HEAD`.
- Add to the `## Chiusura` section the CLOSED line in the format of CO-1. If the section is missing, create it before `## Progressi`.
- Then apply `--state DONE --ack auto`.

Verification command: `python -m unittest tests.tools.test_sync_hdr_close -v` → OK, including a new case `test_closed_line`.
Designated agent: bm-coder.
