# CO-6 - operator template: the Chiusura section (bm-mechanic)

Order: after CO-5 is APPROVED (the placeholder uses the `--closed` option of CO-5).
Branch: not set by this WO; SPV sets it at assignment.

Objective: every new operator file from `sync/_template.md` has a `## Chiusura` section with the placeholder command.

Files: `sync/_template.md`.

Change: between `## Consegne` and `## Progressi`, add the section `## Chiusura` with the placeholder `(python tools/sync_hdr.py {ID} --closed nessuno --branch {branch})`.

Verification command: `python tools/sync_check.py` → no new problems on the files created from the template.
Designated agent: bm-mechanic.
