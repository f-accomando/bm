# AGY-7 - SCAFFOLD-IT: scaffold-workflow-IT.pdf, la guida in italiano con lo stesso tema
Inizia: subito. Prima di AGY-8.
Repo: C:\Users\Utente\Documents\GitHub\scaffold (remoto f-accomando/scaffold). Base: origin/claude/workflow-template (7bff911); branch agy/scaffold-workflow-it. Push solo di questo branch su origin di scaffold. Mai push su main, mai merge, mai PR.
Perimetro: scripts/build_workflow_pdf_it.py (nuovo), scaffold-workflow-IT.pdf (nuovo). NON toccare: scripts/build_workflow_pdf.py, scaffold-workflow-EN.pdf, scaffold-EN.pdf, scaffold-IT.pdf, tutto il resto.
Obiettivo: stessa guida di scaffold-workflow-EN.pdf (28 pagine) in italiano, stesso tema grafico e stessi font.

## AGY-7a - script italiano (bm-light, 60 min)
- Copiare scripts/build_workflow_pdf.py in scripts/build_workflow_pdf_it.py e tradurre in italiano tutta la prosa: titoli, riquadri TL;DR, spiegazioni, "Practical examples" (Esempi pratici), didascalie, intestazione e pie' di pagina (HEADER e FOOTER), sommario, nomi dei riquadri. Registro: tecnico, chiaro, "tu" o impersonale come fa lo script inglese.
- Restano IDENTICI all'originale: colori (PRIMARY, SECONDARY, DARK, ACCENT, CODE_BG, TLDR_BG, EX_BG), font (Helvetica, Helvetica-Bold, Courier), margini, struttura a sezioni (TL;DR, spiegazione, esempi), numero e ordine delle sezioni, fixture Taskly.
- Restano in inglese, come nell'originale: comandi, nomi di file e di script, nomi di campi, output catturati dagli script reali (blocchi Preformatted), identificatori. Si traducono i commenti dentro i blocchi solo se sono prosa.
- Argomento --out: default scaffold-workflow-IT.pdf nella radice del repo. La docstring in italiano.
- Verifica che gli accenti (è à ì ò ù, maiuscole accentate, apostrofi, virgolette) escano giusti con Helvetica: estrarre il testo con pymupdf e cercare le sequenze '?' o riquadri.

## AGY-7b - generare e controllare (bm-light, 30 min)
- python scripts/build_workflow_pdf_it.py -> scaffold-workflow-IT.pdf.
- Confronti con pymupdf (scrivere lo script di controllo nel messaggio di consegna, non nel repo): stesso numero di pagine di scaffold-workflow-EN.pdf (28, tolleranza +-2 per la lunghezza dell'italiano, motivarla), stessi font (insieme dei nomi dei font identico), stessi colori (insieme dei colori di riempimento e di testo identico), stessa dimensione pagina A4.
- Nessuna parola inglese di prosa rimasta: estrarre il testo, cercare 'the', 'and', 'of', 'to ', 'with' come parole intere fuori dai blocchi di codice, elencare e correggere i casi.
- Verifica: python scripts/build_workflow_pdf_it.py esce con 0; git diff --name-only origin/claude/workflow-template...HEAD -> solo i 2 file del perimetro.
Criterio di fine: PDF generato, confronti OK, nessuna prosa inglese.
Consegna: riga DELIVERED standard con il commit del branch e i risultati dei confronti (pagine, font, colori), poi passare ad AGY-8.
