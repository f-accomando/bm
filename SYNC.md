# Dual-Agent Synchronization Protocol (`SYNC.md`)

> **Repository**: `f-accomando/bm`  
> **Base Branch**: `bm-core`  
> **Partecipanti**:
> - **Antigravity** (Machine A / Windows Local Runtime)
> - **Claude Code Max** (Machine B / Local Runtime)
> **Governance di riferimento**: vedi [`AGENTS.md`](AGENTS.md)

---

## 1. Protocollo di Collaborazione

Questo file è il canale di coordinamento e sincronizzazione asincrona tra **Claude Code** e **Antigravity**.

### Regole Operative
1. **Branching**:
   - Claude Code lavora su: `feat/claude-<feature>`
   - Antigravity lavora su: `feat/antigravity-<feature>`
   - Branch comune di base e integrazione finale: `bm-core`
2. **Ciclo di Lavoro**:
   - **Step 1 (Architettura)**: Claude Code riceve i requisiti dall'utente e compila le Sezioni 2 e 3 di questo file (`SYNC.md`), definendo contratti condivisi, interfacce e suddivisione task.
   - **Step 2 (Push specifiche)**: Claude fa commit e push di `SYNC.md` su `bm-core` (o sul suo branch per allineamento).
   - **Step 3 (Sviluppo Parallelo)**: Antigravity e Claude lavorano in parallelo ciascuno sul rispettivo branch, implementando i Work Order assegnati.
   - **Step 4 (Cross-Review)**: Al completamento di ogni milestone:
     - Si fa push del branch.
     - Si aggiorna il **Log di Handover** (Sezione 5).
     - L'altro agente fa `git fetch origin <branch>`, verifica i test (`make test`, build o script dedicato) ed esegue la code review.
   - **Step 5 (Merge)**: Solo dopo reciproco via libera (o risoluzione dei commenti) si effettua il merge concordato su `bm-core`.

---

## 2. Definizione Architettura & Contratti Condivisi

*(Questa sezione viene compilata da Claude Code all'avvio dello sviluppo)*

- **Obiettivo Globale**: `[DA DEFINIRE DA CLAUDE CODE]`
- **Contratti / Header / Interfacce Condivise**:
  - `[Definire header C, struct, API Lua o formati file condivisi]`
- **Branch Assegnati**:
  - Claude Code: `feat/claude-...`
  - Antigravity: `feat/antigravity-...`

---

## 3. Task Board: Antigravity

*(Claude Code compila qui i Work Order per Antigravity secondo il formato di AGENTS.md)*

### WORK ORDER: AGY-01
- **Obiettivo**: `[In attesa di specifica da Claude Code]`
- **File / Funzioni da toccare**: `[...]`
- **Modifica richiesta**: `[...]`
- **Comando di verifica**: `[...]`
- **Stato**: `PENDING` *(PENDING | IN_PROGRESS | DONE | BLOCKED | FAILED)*

---

## 4. Task Board: Claude Code

*(Claude Code elenca qui i propri compiti per trasparenza e tracciamento)*

### WORK ORDER: CLD-01
- **Obiettivo**: `[In attesa]`
- **File / Funzioni da toccare**: `[...]`
- **Comando di verifica**: `[...]`
- **Stato**: `PENDING`

---

## 5. Log di Handover & Cross-Review

Utilizzare questa tabella come registro delle comunicazioni e dei passaggi di consegne.

| Timestamp | Agente | Branch / Commit | Stato / Azione | Note / Messaggio per l'altro agente |
|---|---|---|---|---|
| 2026-10-13 | Antigravity | `bm-core` | `INIT` | Protocollo `SYNC.md` inizializzato su `bm-core`. Pronto a ricevere architettura e task da Claude Code. |
