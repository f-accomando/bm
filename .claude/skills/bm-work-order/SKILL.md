---
name: bm-work-order
description: Generatore e validatore di Ordini di Lavoro (Work Order) per bm. Applica il Gate di Prontezza (Readiness Gate a 5 domande) per determinare se una task è pronta per essere eseguita da bm-mechanic, bm-coder, bm-light o se deve essere spezzata da bm-planner.
---

# bm Work Order & Readiness Gate Protocol

Questa skill guida la generazione e validazione di ordini di lavoro (Work Order) per garantire **massima efficienza dei token**, determinismo del codice e zero decisioni non pianificate durante la scrittura.

## 1. Il Gate di Prontezza (5 Domande)
Prima di assegnare una task all'esecuzione, rispondi a queste 5 domande:

1. **File/Funzioni espliciti**: Sono nominati i percorsi dei file e i nomi delle funzioni/simboli esatti da toccare?
2. **Determinismo del Diff**: La modifica è descritta in modo tale che due programmatori farebbero lo stesso diff (nessuna scelta di design aperta)?
3. **Criterio Verificabile**: Esiste un comando deterministico di verifica (es. `python tests/qemu_test.py -k <nome>`, compilazione, asserzione)?
4. **Scope Contenuto**: Riguarda indicativamente 3 file o meno?
5. **Zero Dipendenze Sospese**: Non dipende da una decisione architetturale o di prodotto ancora da prendere?

### Esito del Gate
- **5 su 5 SÌ** $\to$ Task **PRONTA**. Seleziona l'agente esecutivo (`bm-mechanic`, `bm-coder`, `bm-light`).
- **Almeno 1 NO** $\to$ Task **NON PRONTA**. Invocare `bm-planner` (Gemini Pro / Opus) per investigare, sciogliere le ambiguità e produrre uno o più Work Order.

---

## 2. Template Ufficiale del Work Order

```markdown
### WORK ORDER: [ID-BREVE]
- **Obiettivo**: [1 frase chiara]
- **File/Funzioni da toccare**: [Es: src/bm/runtime.c:bm_init_cart()]
- **Modifica richiesta**: [Dettaglio del diff atteso, parametri, logica senza ambiguità]
- **Comando di verifica**: [Es: python tests/qemu_test.py -k titan]
- **Esito atteso**: [Exit code 0 / output specifico]
- **Agente designato**: [bm-mechanic | bm-coder | bm-light]
```

---

## 3. Protocollo di Chiusura
L'agente esecutore restituisce uno dei seguenti stati:
- **`DONE`**: Comando di verifica eseguito e passato con successo. Breve diff summary.
- **`BLOCKED: <motivo>`**: Specifica incompleta, firma non corrispondente o decisione mancante $\to$ ritorno a `bm-planner`.
- **`FAILED x2`**: Due tentativi consecutivi di fix falliti $\to$ escalation a `bm-light` o `bm-deep`.
