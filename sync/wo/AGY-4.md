# AGY-4 — AUDIT: agent_audit.py completo (R0) e baseline del ledger (D-R2) (SYS-4)
Inizia dopo: DELIVERED AGY-3.
Base: bm-core aggiornato; branch agy/agent-audit-baseline. Mai push su bm-core.
Perimetro: scripts/agent_audit.py, tests/tools/test_agent_audit.py, docs/agents/baseline.txt (nuovo, generato). NON toccare: docs/agents/ledger.txt (solo append, di SPV), scripts/docs_audit.py.
Obiettivo: lo script tiene e riporta tutti i campi del ledger e confronta lo stato attuale con una baseline salvata.

## AGY-4a — R0 (bm-coder, 30 min)
- _typed() (scripts/agent_audit.py:45) aggiunge "date": row["date"][:10], "model", "task", "area".
- report(rows, agent=None, area=None, since=None): filtra per area (uguaglianza) e per since (date >= since); dopo la tabella necessity, una riga vuota e _share_table("model", rows, "model"), poi una riga vuota e _share_table("area", rows, "area").
- main(): --area NAME e --since YYYY-MM-DD (formato non valido → stderr "bad --since", return 2).
- Test nuovi in tests/tools/test_agent_audit.py: test_model_and_area_tables (stdout contiene "claude-haiku-5-5" e "tools" nelle nuove tabelle), test_area_filter, test_since_filter, test_bad_since_exit_2.
- Verifica: python tests/tools/test_agent_audit.py → OK (vecchi e nuovi).

## AGY-4b — baseline D-R2 (bm-coder, 40 min)
- --save-baseline PATH scrive: "# baseline of <ledger>, rows <N>, last date <YYYY-MM-DD>", poi per agente in ordine "agent|runs|pctDONE(1 decimale)|tok_med|secs_med".
- --baseline PATH legge quel file (riga malformata → stderr "baseline line N: <motivo>", return 2) e aggiunge al report una sezione "vs baseline" con agent, Δruns, Δ%DONE, Δtok_med, Δsecs_med (valore attuale meno baseline); un agente presente solo in uno dei due appare con "new" oppure "gone".
- Aggiornare la docstring: "Writes a file only with --save-baseline".
- Generare docs/agents/baseline.txt dal ledger di bm-core al momento del branch.
- Test: test_save_and_compare_baseline, test_bad_baseline_exit_2.
- Verifica: python tests/tools/test_agent_audit.py → OK; python scripts/agent_audit.py → exit 0 con le tabelle model e area; python scripts/agent_audit.py --baseline docs/agents/baseline.txt → exit 0 con la sezione "vs baseline".
Criterio di fine: tutte le verifiche OK.
Consegna: riga DELIVERED standard, poi FREE.
