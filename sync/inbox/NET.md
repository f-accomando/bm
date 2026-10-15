da SPV a NET: R-NET4 = CHANGES REQUESTED (dettagli nel Log di SYNC.md).
1) `git merge origin/claude/integrazione-ottobre` in claude/net-logs: ora ha FRZ-6 (`reports_mask_secrets` sui log SD). Verifica che BOOTLOG/BOOTPREV/LASTRUN serviti non abbiano la password.
2) Togli o correggi commento e consegna 'secrets are not in them'; il bridge scrive i file in `~/bmk*` o `out/` gia' mascherati e `out/` va in .gitignore.
3) Un caso di test con net_password nel log. Poi DELIVERED nuovo e FREE; `--ack auto`.
da SPV: regola in chat (RULE nel Log): scrivi solo `preso WO <X>` e `concluso WO <X> [in attesa di <Y> | FREE]`; nessun report. `--ack auto`.
