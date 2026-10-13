# bm (BareMetal) — Specifiche e Routing Agenti

Questo file definisce la governance, il routing e i vincoli operativi per tutti gli agenti AI che operano sul repository `bm`.

---

## 1. Il Principio Fondamentale: Separare Decidere da Eseguire

- **I modelli di punta (Opus / Gemini Pro)**: Hanno un costo elevato sui token di output, ma eccellono nel ragionamento logico e architetturale. Producono **pochi token**: decisioni strategiche, schemi e Ordini di Lavoro sintetici. **Non scrivono il codice.**
- **I modelli rapidi ed economici (Haiku / Gemini Flash / Flash-Lite)**: Producono **tanti token**: implementazione del codice, refactoring vincolati, rename, formattazione. Ricevono specifiche blindate ed eseguono.
- **La regola del Router**: La selezione dell'agente NON dipende dalla grandezza apparente della task, ma da quanto è già rigorosamente specificata.

---

## 2. Tabella dei Ruoli

| Agente | Claude Engine | Antigravity Engine | Toolset | Responsabilità Primaria | Limiti Assoluti |
|---|---|---|---|---|---|
| **`bm-planner`** | Opus medium | Gemini Pro (`"pro"`) | **Sola Lettura** (Read, Grep, Glob) | Trasforma task vaghe in Ordini di Lavoro (Work Order) verificabili | **Non scrive codice** |
| **`bm-coder`** | Haiku high | Gemini Flash (`"flash"`) | Full (Read/Write/Exec) | Scrive codice da un Work Order preciso | Nessun design, nessuna scelta aperta |
| **`bm-mechanic`** | Haiku low | Gemini Flash-Lite (`"flash_lite"`) | Full (Atomico) | Rename, formattazione, template, spunte in `docs/progress/` | Zero decisioni di business |
| **`bm-light`** | Sonnet medium | Gemini Flash (`"flash"`) | Full (Read/Write/Exec) | Task con giudizio tecnico, docs verificate leggendo il codice C, fix 1-2 file | No task lunghe o vaghe |
| **`bm-deep`** | Opus xhigh | Gemini Pro (`"pro"`) | Full (Read/Write/Exec) | Un solo problema critico isolato con riproduzione certa | Rifiuta task senza riproduzione |
| **`bm-research`** | Haiku low | Subagente `research` | Sola Lettura | Ispezione simboli, call flow su molti file, sintesi 5-10 righe | Non tocca file |

---

## 3. Il Gate di Prontezza (Readiness Gate)

Prima di affidare una task a un modello esecutivo (`bm-mechanic`, `bm-coder`, `bm-light`), applica rigorosamente questa checklist:

1. [ ] **File e Funzioni**: I file e le firme/funzioni da toccare sono esplicitamente nominati?
2. [ ] **Determinismo**: La modifica è descritta in modo tale che due sviluppatori scriverebbero lo stesso diff (zero scelte di design aperte)?
3. [ ] **Verifica Determinica**: Esiste un comando verificabile (test, build, lint, output atteso)?
4. [ ] **Scope Contenuto**: Riguarda 3 file o meno?
5. [ ] **Zero Dipendenze Sospese**: Non dipende da una decisione ancora da prendere?

### Regola Aurea
- **5 su 5 Sì** $\to$ La task è **PRONTA**: assegnala a `bm-mechanic`, `bm-coder` o `bm-light`.
- **Anche solo 1 No** $\to$ La task **NON È PRONTA**: assegnala a **`bm-planner`** per renderla pronta o spezzarla.
- **Mai direttamente a `bm-deep`** a meno che non soddisfi i criteri specifici di debug isolato.

---

## 4. Regole Vincolanti per `bm-deep`

`bm-deep` interviene solo su un problem statement stretto e provvisto di:
1. Sintomo chiaro documentato.
2. Procedura di riproduzione esatta (comando/test che fallisce).
3. Area sospetta isolata.
4. Tentativi già effettuati (es. fallimento `FAILED x2`).

Se manca la riproduzione o la richiesta è generica (*"implementa X"*), `bm-deep` rifiuta la task e la rimanda a `bm-planner`.

---

## 5. Protocollo di Chiusura ed Escalation

Ogni agente termina il proprio intervento comunicando inequivocabilmente il proprio stato:
- **`DONE`**: Il comando di verifica è passato con successo. Include riepilogo sintetico del diff.
- **`BLOCKED: <motivo>`**: Specifica ambigua o decisione mancante. La task ritorna a `bm-planner`.
- **`FAILED x2`**: Il test continua a fallire dopo 2 tentativi. La task escala di livello (es. da `bm-coder` a `bm-light`, o a `bm-deep`).

---

## 6. Formato Standard dell'Ordine di Lavoro (Work Order)

Ogni Work Order emesso da `bm-planner` rispetta questo schema:

```markdown
### WORK ORDER: [ID]
- **Obiettivo**: [1 frase chiara]
- **File/Funzioni da toccare**: [Percorsi esatti, es. src/bm/runtime.c:bm_init_cart()]
- **Modifica richiesta**: [Dettaglio univoco senza scelte di design aperte]
- **Comando di verifica**: [Comando e return code/output atteso, es. `python tests/qemu_test.py -k titan`]
- **Agente designato**: [bm-mechanic | bm-coder | bm-light]
```
