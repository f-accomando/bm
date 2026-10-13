# generate_scaffold_pdf_it.py
# Versione italiana di docs/scaffold.pdf -> docs/scaffold-IT.pdf.
# Prosa in italiano; nomi di file, cartelle, agenti, skill e heading cercati con grep restano in inglese.
# Da lanciare dalla radice del repo:  python3 scripts/generate_scaffold_pdf_it.py
import os
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Preformatted,
    HRFlowable,
)
from reportlab.lib import colors
from reportlab.pdfgen import canvas

HEADER = "Guida allo Scaffolding AI e all'Architettura degli Agenti — Claude Code e Google Antigravity"
FOOTER = "Manuale e Starter Kit per lo Scaffolding Context-Aware"

PRIMARY = colors.HexColor("#1A365D")
SECONDARY = colors.HexColor("#2B6CB0")
DARK = colors.HexColor("#2D3748")
ACCENT = colors.HexColor("#319795")
CODE_BG = colors.HexColor("#F7FAFC")
BORDER = colors.HexColor("#E2E8F0")
MUTED = colors.HexColor("#718096")


class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved = []

    def showPage(self):
        self._saved.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        n = len(self._saved)
        for state in self._saved:
            self.__dict__.update(state)
            self._decorate(n)
            super().showPage()
        super().save()

    def _decorate(self, total):
        self.saveState()
        self.setFont("Helvetica", 8)
        self.setFillColor(MUTED)
        if self._pageNumber > 1:
            self.drawString(54, 755, HEADER)
            self.setStrokeColor(BORDER)
            self.setLineWidth(0.5)
            self.line(54, 747, letter[0] - 54, 747)
        self.drawRightString(letter[0] - 54, 36, f"Pagina {self._pageNumber} di {total}")
        self.drawString(54, 36, FOOTER)
        self.setStrokeColor(BORDER)
        self.setLineWidth(0.5)
        self.line(54, 48, letter[0] - 54, 48)
        self.restoreState()


_base = getSampleStyleSheet()["Normal"]
S_TITLE = ParagraphStyle("T", parent=_base, fontName="Helvetica-Bold", fontSize=18, leading=22, textColor=PRIMARY, spaceAfter=4)
S_SUB = ParagraphStyle("Sub", parent=_base, fontName="Helvetica", fontSize=9.5, leading=13, textColor=colors.HexColor("#4A5568"), spaceAfter=10)
S_PART = ParagraphStyle("Part", parent=_base, fontName="Helvetica-Bold", fontSize=9, leading=12, textColor=colors.white,
                        backColor=SECONDARY, borderPadding=(3, 5, 3, 5), spaceBefore=14, spaceAfter=8, keepWithNext=True)
S_H1 = ParagraphStyle("H1", parent=_base, fontName="Helvetica-Bold", fontSize=11, leading=14, textColor=PRIMARY, spaceBefore=10, spaceAfter=5, keepWithNext=True)
S_LABEL = ParagraphStyle("Lbl", parent=_base, fontName="Courier-Bold", fontSize=7.6, leading=10, textColor=ACCENT, spaceBefore=5, spaceAfter=2, keepWithNext=True)
S_BODY = ParagraphStyle("B", parent=_base, fontName="Helvetica", fontSize=8, leading=11.5, textColor=DARK, spaceAfter=4)
S_BUL = ParagraphStyle("Bu", parent=S_BODY, leftIndent=11, firstLineIndent=-8, spaceAfter=2)
S_CODE = ParagraphStyle("C", parent=_base, fontName="Courier", fontSize=6.6, leading=8.6, textColor=colors.HexColor("#1A202C"))
S_TH = ParagraphStyle("TH", parent=_base, fontName="Helvetica-Bold", fontSize=7.5, leading=9.5, textColor=colors.white)
S_TD = ParagraphStyle("TD", parent=_base, fontName="Helvetica", fontSize=7, leading=9.3, textColor=DARK)

story = []


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def c(s):
    """Codice inline."""
    return f'<font name="Courier" size="7.2">{esc(s)}</font>'


def part(t):
    story.append(Paragraph(t, S_PART))


def h1(t):
    story.append(Paragraph(t, S_H1))


def p(t):
    story.append(Paragraph(t, S_BODY))


def b(t):
    story.append(Paragraph("• " + t, S_BUL))


def n(i, t):
    story.append(Paragraph(f"{i}. " + t, S_BUL))


def label(t):
    story.append(Paragraph(esc(t), S_LABEL))


def code(text, rows=9):
    """Blocco di codice che puo' spezzarsi tra pagine (una riga di tabella per gruppo di righe)."""
    lines = text.strip("\n").split("\n")
    for ln in lines:
        if len(ln) > 122:
            print(f"ATTENZIONE: riga di codice troppo larga ({len(ln)} caratteri): {ln[:60]}...")
    chunks = ["\n".join(lines[i:i + rows]) for i in range(0, len(lines), rows)]
    t = Table([[Preformatted(ch, S_CODE)] for ch in chunks], colWidths=[504])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), CODE_BG),
        ("BOX", (0, 0), (-1, -1), 0.8, BORDER),
        ("LEFTPADDING", (0, 0), (-1, -1), 7),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 1.5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 1.5),
    ]))
    story.append(t)
    story.append(Spacer(1, 4))


def table(rows, widths, head=True):
    data = []
    for i, r in enumerate(rows):
        st = S_TH if (head and i == 0) else S_TD
        data.append([Paragraph(x, st) for x in r])
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    style = [
        ("ALIGN", (0, 0), (-1, -1), "LEFT"),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("INNERGRID", (0, 0), (-1, -1), 0.5, BORDER),
        ("BOX", (0, 0), (-1, -1), 1, PRIMARY),
        ("TOPPADDING", (0, 0), (-1, -1), 2.5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
    ]
    if head:
        style.append(("BACKGROUND", (0, 0), (-1, 0), PRIMARY))
    t.setStyle(TableStyle(style))
    story.append(t)
    story.append(Spacer(1, 4))


# ============================================================ TITOLO
story.append(Paragraph("Scaffolding AI Context-Aware e Architettura degli Agenti", S_TITLE))
story.append(Paragraph(
    "Manuale e starter kit per Claude Code e Google Antigravity / Gemini: economia dei token, gate di prontezza, "
    "routing tramite Work Order e template da copiare per avviare qualsiasi nuovo progetto. Implementazione di riferimento: "
    "github.com/f-accomando/bm (agenti con prefisso <i>bm-</i>). Nomi di file, cartelle, agenti e skill restano in inglese, "
    "cos&igrave; i template si copiano senza modifiche.", S_SUB))
story.append(HRFlowable(width="100%", thickness=1.5, color=PRIMARY, spaceBefore=0, spaceAfter=4))

# ============================================================ PARTE A
part("PARTE A — COME FUNZIONA")

h1("1. Il principio base: separare il decidere dall'eseguire")
p("I modelli di punta (Claude Opus, Gemini Pro) hanno un collo di bottiglia di costo: l'<b>asimmetria dei token di output</b>. "
  "Un token di output di un modello top costa circa 20-40 volte quello di un modello di fascia bassa (Haiku, Flash-Lite), quindi "
  "far scrivere codice a un modello top consuma in fretta le quote di sessione. La soluzione &egrave; strutturale:")
b("<b>Modelli alti (Opus / Gemini Pro)</b> producono <i>pochi token di output</i>: pianificano, progettano, sciolgono i compromessi ed emettono Work Order strutturati.")
b("<b>Modelli bassi (Haiku / Gemini Flash)</b> producono <i>molti token di output</i>: scrivono e rifattorizzano il codice guidati da Work Order deterministici.")
b("<b>Routing per prontezza:</b> una task non si assegna in base alla sua grandezza o complessit&agrave; apparente, ma a <b>quanto &egrave; gi&agrave; ben specificata</b>.")

h1("2. Workflow di sessione: zero spreco di token")
p("Caricare documenti interi distrugge il budget di contesto e genera allucinazioni. L'architettura usa un recupero gerarchico, "
  "on-demand e chirurgico:")
b("<b>Root guardian</b> (" + c("CLAUDE.md") + " / " + c("GEMINI.md") + "): caricato all'avvio, sotto le 60 righe. Invarianti globali, regole di branch e licenza, tabella di consultazione, routing degli agenti.")
b("<b>Mappa indice</b> (" + c("docs/progress.md") + "): l'unico indice. Non si legge per intero: serve solo a instradare verso la scheda d'area giusta.")
b("<b>Estrazione mirata</b> con grep e letture offset/limit: circa 100 token invece di 10.000.")
table([
    ["Fase", "Cosa fa l'AI", "File coinvolti / output"],
    ["1. Boot", "Carica le regole base e i comandi rapidi.", c("CLAUDE.md") + " / " + c("GEMINI.md") + " (circa 50 righe)"],
    ["2. Routing", "Individua l'area target se non &egrave; ovvia.", c("docs/progress.md") + " (solo consultazione)"],
    ["3. Lookup", "Estrae in modo chirurgico task aperte e vincoli.", c("grep -n -A 15 '^## Open work' docs/progress/<area>.md")],
    ["4. Esecuzione", "Ispezione mirata (indice dei simboli) e test selettivi.", "Sorgenti puntuali, " + c("make test-<area>")],
    ["5. Chiusura", "Leave-no-trace: aggiorna lo stato, lancia l'audit.", c("docs/progress/<area>.md") + ", " + c("docs_audit.py")],
], [62, 210, 232])

h1("3. L'architettura degli agenti: sei ruoli")
table([
    ["Agente", "Livello Claude", "Livello Antigravity", "Strumenti", "Mandato e limiti"],
    ["<b>planner</b>", "Opus medium", "Gemini Pro", "Sola lettura (+ shell in sola lettura)", "Trasforma task vaghe o grosse in Work Order verificati. <b>Non scrive mai codice.</b> Passa sempre prima di deep."],
    ["<b>coder</b>", "Haiku high", "Gemini Flash", "Completi (edit, write, exec)", "Scrive codice da un Work Order bloccato. <b>Nessuna scelta di design, nessuna ambiguit&agrave; da sciogliere.</b>"],
    ["<b>mechanic</b>", "Haiku low", "Gemini Flash-Lite", "Completi (modifiche atomiche)", "Task a rischio zero: rename di simboli, formattazione, spunte di progress/roadmap."],
    ["<b>light</b>", "Sonnet medium", "Gemini Flash", "Completi (read, write, exec)", "Fix mirati (1-2 file) con giudizio tecnico; documentazione verificata sul codice reale."],
    ["<b>deep</b>", "Opus xhigh", "Gemini Pro", "Completi (repro isolata)", "Gate rigoroso: un solo bug isolato con riproduzione, o una decisione architetturale difficile. Rifiuta le task generiche."],
    ["<b>research</b>", "Haiku low", "Subagente research nativo", "Sola lettura (+ shell in sola lettura)", "Esplorazione ampia su molti file; risponde con una sintesi di 5-10 righe. Non modifica mai."],
], [52, 62, 72, 88, 230])

h1("4. Gate di prontezza, stati di chiusura ed escalation")
p("Prima di assegnare una task a un agente esecutivo (mechanic, coder, light), il router risponde a cinque domande s&igrave;/no:")
n(1, "<b>File e simboli:</b> sono nominati percorsi esatti e firme delle funzioni?")
n(2, "<b>Determinismo del diff:</b> la modifica &egrave; specificata cos&igrave; bene che due sviluppatori scriverebbero lo stesso diff?")
n(3, "<b>Comando verificabile:</b> esiste un controllo automatico (unit test, build, lint, script di audit)?")
n(4, "<b>Scope limitato:</b> tocca 3 file o meno?")
n(5, "<b>Nessuna decisione sospesa:</b> &egrave; priva di scelte di design o dipendenze ancora aperte?")
p("<b>Regola:</b> 5 su 5 S&Igrave; = task pronta; anche un solo NO blocca l'esecuzione e la richiesta va a <b>planner</b>, che la rende pronta o la spezza "
  "in Work Order atomici. Mai una task non preparata direttamente a <b>deep</b>: accetta solo sintomo, riproduzione esatta, area sospetta e tentativi "
  "gi&agrave; fatti; altrimenti risponde " + c("REJECTED") + " e rimanda a planner. Ogni agente chiude con uno e un solo stato:")
table([
    ["Stato", "Significato", "Passo successivo"],
    [c("DONE"), "Il comando di verifica &egrave; passato; breve riepilogo del diff.", "Chiudere la task (sezione 19, regola R9)."],
    [c("BLOCKED: <motivo>"), "Specifica ambigua, firma non corrispondente o decisione mancante.", "Torna a planner."],
    [c("FAILED x2"), "La verifica continua a fallire dopo due tentativi di correzione.", "Sale di un livello (da coder a light, da light a deep)."],
], [100, 250, 154])

h1("5. Template standard del Work Order")
code("""### WORK ORDER: [WO-ID]
- Objective: [una frase concisa che definisce il risultato]
- Target Files & Symbols: [percorsi e funzioni espliciti, es. src/module/file.c:init_thing()]
- Required Mutation: [specifica del diff senza ambiguita', zero margine di design]
- Verification Command: [comando deterministico, es. make test-<area> oppure pytest -k <nome>]
- Expected Result: [exit code 0 e l'output/asserzione esatti attesi]
- Designated Agent: [mechanic | coder | light]""")

h1("6. Struttura del repository su tre livelli")
p("Per evitare il degrado del contesto, la documentazione si divide in tre cicli di vita:")
b("<b>Root guardian</b> (" + c("CLAUDE.md") + ", " + c("GEMINI.md") + "): sotto le 60 righe. Solo invarianti globali, scorciatoie di build, regole di branch, routing degli agenti. Nessuna cronologia di rilasci.")
b("<b>Progress vivo</b> (" + c("docs/progress/<area>.md") + "): schede attive di 80-120 righe: stato attuale, lavoro aperto, regole locali non derogabili.")
b("<b>RFC profonde</b> (" + c("docs/design/<topic>.md") + "): design speculativi e proposte a lungo termine, tenuti fuori dall'indice vivo per non gonfiare il costo in token.")

# ============================================================ PARTE B
part("PARTE B — STARTER KIT PER UN NUOVO PROGETTO (COPIA, COMPILA I &lt;PLACEHOLDER&gt;, COMMITTA)")

h1("7. Checklist di avvio e albero dei file")
n(1, "Crea l'albero qui sotto e copia i template delle sezioni 8-14.")
n(2, "Compila " + c("CLAUDE.md") + ": stack, comandi di test, invarianti globali. Resta sotto le 60 righe.")
n(3, "Copia i sei agenti (sezione 10); adatta la lista " + c("tools") + " e i comandi di verifica al tuo stack.")
n(4, "Copia le tre skill (sezione 11) e " + c(".claude/settings.json") + " (sezione 12).")
n(5, "Crea " + c("docs/progress.md") + " e una prima scheda d'area (sezione 13); aggiungi " + c("scripts/docs_audit.py") + " (sezione 14).")
n(6, "Collega l'audit a pre-commit, Makefile e CI (sezione 15). Lancialo una volta e salva il punteggio di base.")
n(7, "Crea " + c("GEMINI.md") + " se usi anche Antigravity (sezione 9). Committa, poi affida la prima task vera a planner per provare il gate.")
code("""<project>/
|-- CLAUDE.md                    root guardian, sotto le 60 righe (caricato a ogni sessione)
|-- GEMINI.md                    stesse sezioni 1,2,4,5 + tabella di routing Antigravity
|-- AGENTS.md                    opzionale: ruoli, gate, formato Work Order (governance condivisa)
|-- .claude/
|   |-- settings.json            skill override, permessi allow/deny
|   |-- agents/                  planner, coder, mechanic, light, deep, research (.md)
|   `-- skills/
|       |-- work-order/SKILL.md
|       |-- docs-map/SKILL.md
|       `-- docs-loop/SKILL.md
|-- .agents/skills/              copia di .claude/skills per Antigravity
|-- .githooks/pre-commit         hook versionato (git config core.hooksPath .githooks)
|-- .github/workflows/ci.yml     include lo step di docs audit
|-- docs/
|   |-- progress.md              l'unico indice
|   |-- progress/<area>.md       schede vive, 80-120 righe ciascuna
|   |-- design/<topic>.md        RFC
|   `-- ROADMAP.md               milestone opzionali
`-- scripts/
    |-- docs_audit.py            punteggio: token + penalita' (sezione 14)
    `-- cindex.py                indice opzionale di simboli/banner per file sorgente grossi""")

h1("8. Template: CLAUDE.md (root guardian)")
p("Claude Code concatena ogni " + c("CLAUDE.md") + " che trova dalla cartella di lavoro verso l'alto e lo carica per intero a ogni sessione. "
  "La linea guida ufficiale &egrave; restare sotto le 200 righe; questo kit &egrave; volutamente pi&ugrave; severo (60) perch&eacute; il file si paga a ogni turno. "
  "&Egrave; contesto, non enforcement: ci&ograve; che non deve <i>mai</i> succedere va in una regola di permesso o in un hook (sezione 12). "
  "I titoli " + c("## Open work") + " e " + c("## Rules") + " restano in inglese perch&eacute; sono il bersaglio dei comandi grep.")
label("CLAUDE.md")
code("""# <Project> - Note Operative di Sessione (Claude & Gemini)
Repo <org>/<repo>, branch principale `main`. Stack: <linguaggi, framework, OS/target>.
Indice unico dello stato: docs/progress.md. Mai leggere un file intero: prima grep, poi Read con offset/limit.

## 1. Recupero mirato (zero spreco di token)
| Esigenza                      | Comando                                                        |
|-------------------------------|----------------------------------------------------------------|
| Cosa resta da fare in un'area | grep -n -A 15 '^## Open work' docs/progress/<area>.md         |
| Regole vincolanti di un'area  | grep -n -A 20 '^## Rules' docs/progress/<area>.md              |
| Trovare un simbolo in un file | python scripts/cindex.py <file> <simbolo> (poi Read offset)    |
| Contesto di design            | docs/design/<topic>.md (solo se richiesto)                     |

## 2. Regole di ingaggio
- Branch: proporre un nome breve (feat/<nome>) e chiedere prima di modificare; partire da main aggiornato.
- Test: lanciare solo i test dell'area toccata (make test-<area>); mai l'intera suite.
- Nessun segreto (chiavi, token, .env) nel repo. Commit solo su richiesta dell'utente.
- <Vincoli di licenza / dipendenze, es. nessun codice GPL copiato nei sorgenti>.

## 3. Routing dei subagent (definizioni in .claude/agents/)
Si sceglie per quanto una task e' ben specificata, non per la sua grandezza.
| Agente    | Modello / effort       | Fa                                           | Non fa                 |
|-----------|------------------------|----------------------------------------------|------------------------|
| planner   | opus medium, sola lett.| task vaga/grossa -> Work Order               | scrivere codice        |
| coder     | haiku high             | codice da un Work Order bloccato             | scelte di design       |
| mechanic  | haiku low              | rename, format, spunte di progress           | qualsiasi decisione    |
| light     | sonnet medium          | fix su 1-2 file, docs verificate sul codice  | lavori lunghi o vaghi  |
| deep      | opus xhigh             | un bug isolato con repro / una decisione     | feature intere         |
| research  | haiku low, sola lett.  | ricerca ampia, sintesi di 5-10 righe         | modificare file        |
Gate di prontezza (5/5 SI' per coder, mechanic, light): file+simboli nominati; diff deterministico; comando
di verifica; <= 3 file; nessuna decisione sospesa. Anche un solo NO -> planner. Mai direttamente a deep.
Ogni agente chiude con: DONE | BLOCKED: <motivo> | FAILED x2 (sale di un livello).

## 4. Invarianti globali
- <Invariante 1, es. l'API pubblica del modulo X e' congelata>
- Le regole di area stanno in docs/progress/<area>.md sotto "## Rules (do not break)".

## 5. Chiusura task (leave no trace)
1. Aggiornare "## Open work" nella scheda d'area (togliere o spuntare il lavoro finito).
2. Nuovo vincolo o pattern? Aggiungerlo sotto "## Rules (do not break)".
3. Lanciare python scripts/docs_audit.py score: nessun link rotto, nessun orfano, punteggio non sopra la base.""", rows=10)

h1("9. Template: GEMINI.md e AGENTS.md (parit&agrave; tra agenti)")
p("Claude Code legge " + c("AGENTS.md") + " da solo <b>solo quando non esiste alcun CLAUDE.md</b> nella cartella di lavoro o sopra; se ci sono entrambi carica solo CLAUDE.md, "
  "a meno che CLAUDE.md lo importi con " + c("@AGENTS.md") + " (gli import vengono espansi all'avvio, quindi non fanno risparmiare token). Non dare per scontato che AGENTS.md "
  "si carichi accanto a un CLAUDE.md. Regola di parit&agrave;: " + c("GEMINI.md") + " ripete alla lettera le sezioni 1, 2, 4 e 5 di CLAUDE.md e cambia solo la tabella di routing con la mappatura Antigravity qui sotto.")
label("GEMINI.md  (sezione 3 sostituita; tutte le altre sezioni identiche a CLAUDE.md)")
code("""# <Project> - Note Operative di Sessione (Google Antigravity / Gemini)
...sezioni 1 e 2 copiate alla lettera da CLAUDE.md...

## 3. Routing dei subagent (Antigravity invoke_subagent)
| Ruolo     | Modello        | Strumenti               | Fa / non fa                               |
|-----------|----------------|-------------------------|-------------------------------------------|
| planner   | "pro"          | sola lettura            | Work Order / non scrive codice            |
| coder     | "flash"        | completi                | codice da Work Order / non fa design      |
| mechanic  | "flash_lite"   | completi, atomici       | modifiche a rischio zero / non decide     |
| light     | "flash"        | completi                | fix su 1-2 file, docs verificate          |
| deep      | "pro"          | completi                | un bug isolato con repro                  |
| research  | nativo         | sola lettura            | ricerca ampia, sintesi breve              |
Gate di prontezza, stato di chiusura ed escalation: identici a CLAUDE.md sezione 3.

...sezioni 4 e 5 copiate alla lettera da CLAUDE.md...""", rows=10)
label("AGENTS.md  (governance condivisa opzionale; si linka, non si duplica altrove)")
code("""# <Project> - Governance degli Agenti
1. Principio: i modelli top decidono (pochi token), quelli economici eseguono (tanti token). Routing per prontezza.
2. Tabella dei ruoli: planner, coder, mechanic, light, deep, research (modello, strumenti, mandato, limiti).
3. Gate di prontezza (5 domande) e regola: anche un solo NO -> planner.
4. Gate di ingresso di deep: sintomo + repro esatta + area sospetta + tentativi fatti, altrimenti REJECTED.
5. Stati di chiusura: DONE | BLOCKED: <motivo> | FAILED x2.
6. Formato Work Order (sezione 5 di questa guida).""", rows=10)

h1("10. Template: i sei agenti  (.claude/agents/&lt;name&gt;.md)")
p("Gli agenti di progetto stanno in " + c(".claude/agents/") + " e prevalgono sugli agenti utente con lo stesso nome. Frontmatter obbligatorio: " + c("name") + ", " + c("description") +
  " (la description &egrave; ci&ograve; che legge il router, quindi dice <i>quando</i> usare l'agente). Campi opzionali utili: " + c("tools") + " (lista consentita; omessa = eredita tutto), " +
  c("disallowedTools") + ", " + c("model") + " (" + c("haiku") + ", " + c("sonnet") + ", " + c("opus") + ", un ID completo o " + c("inherit") + "), " + c("effort") +
  " (da " + c("low") + " a " + c("max") + "), " + c("maxTurns") + ", " + c("skills") + ", " + c("memory") + ". I campi sconosciuti sono ignorati in silenzio. "
  "<b>Importante:</b> " + c("tools") + " &egrave; una lista consentita, quindi un agente che deve lanciare un comando di verifica deve elencare " + c("Bash") + ".")
label(".claude/agents/planner.md")
code("""---
name: planner
description: Per task vaghe, grosse o con decisioni aperte: produce Work Order, mai codice. Chiamalo prima di deep.
model: opus
effort: medium
tools: Read, Grep, Glob, Bash
---
Sei l'agente di pianificazione. Il tuo output e' breve e deciso: piani e decisioni, mai codice.

## Mandato
1. Trasforma richieste vaghe o grosse in Work Order atomici e autosufficienti.
2. Nomina prima i file e i simboli esatti (grep, indice dei simboli, Read con offset/limit).
3. Manca un requisito? Non indovinare: elencalo come domanda esplicita per l'utente.

## Limiti assoluti
- Niente codice di implementazione ne' diff lunghi. Bash solo in sola lettura (grep, git log/diff, cindex).
- Output compatto: un blocco Work Order per ogni passo atomico, nel formato Work Order di AGENTS.md.""", rows=9)
label(".claude/agents/coder.md")
code("""---
name: coder
description: Scrive codice solo da un Work Order preciso ed esaustivo. Nessuna scelta di design, nessuna ipotesi.
model: haiku
effort: high
tools: Read, Edit, Write, Grep, Glob, Bash
---
Scrivi codice da Work Order bloccati e non cambi nient'altro.

## Regole
1. Nessun design aperto: se l'ordine lascia una scelta aperta o manca un parametro, fermati e rispondi
   `BLOCKED: serve decisione - <dettaglio>`. Non inventare mai una soluzione.
2. Diff minimo: tocca solo i file e i simboli indicati nell'ordine. Nessun refactoring accessorio.
3. Verifica: esegui il comando di verifica dell'ordine prima di chiudere.

## Stato di chiusura (uno solo)
- DONE: verifica passata; riepilogo del diff in 2-3 righe.
- BLOCKED: <motivo>: specifica ambigua o decisione mancante; torna a planner.
- FAILED x2: ancora in errore dopo due tentativi; riporta l'errore e passa a light o deep.""", rows=9)
label(".claude/agents/mechanic.md")
code("""---
name: mechanic
description: Modifiche a rischio zero gia' specificate: rename, formattazione, template, spunte di progress. Non decide.
model: haiku
effort: low
tools: Read, Edit, Write, Grep, Glob, Bash
---
Esegui task puramente meccaniche, a rischio zero e completamente specificate.

## Regole
1. Zero decisioni: fai alla lettera cio' che e' stato chiesto. Non cambiare la logica, non inventare task,
   non riordinare le priorita'.
2. Integrita': mantieni la struttura dei doc di progress; dopo un rename lancia build/lint e assicurati
   di non aver introdotto errori di sintassi.
3. Chiudi con DONE (riepilogo in 1-2 righe) o BLOCKED (istruzione ambigua o coordinate esatte mancanti).""", rows=9)
label(".claude/agents/light.md")
code("""---
name: light
description: Task piccole con giudizio tecnico: domande sul codice, fix su 1-2 file, docs verificate sul codice reale.
model: sonnet
effort: medium
tools: Read, Edit, Write, Grep, Glob, Bash
---
Gestisci task mirate e documentazione verificata.

## Codice
- Il cambiamento minimo che risolve il problema; niente refactoring esteso.
- Lancia sempre il test o il comando di verifica specifico per la modifica.
- Fix ancora in errore al secondo tentativo: restituisci `FAILED x2` ed escala a deep.

## Documentazione
- Zero allucinazioni: leggi il codice reale prima di documentare un'API o un comportamento.
- Rispetta stile e formato dei doc esistenti; non toccare i sorgenti salvo commenti richiesti.

## Chiusura: DONE | BLOCKED (serve una decisione architetturale) | FAILED x2""", rows=9)
label(".claude/agents/deep.md")
code("""---
name: deep
description: UN solo problema isolato e difficile con repro certa (o una decisione critica). Mai feature intere.
model: opus
effort: xhigh
tools: Read, Edit, Write, Grep, Glob, Bash
---
Sei l'agente di massima profondita': costoso, quindi intervieni solo su problemi isolati con evidenze oggettive.

## Gate di ingresso - accetta SOLO se la task contiene tutti e quattro gli elementi
1. Sintomo chiaro.  2. Riproduzione esatta e deterministica (comando, input o test che fallisce).
3. Area sospetta (file, modulo, sottosistema).  4. Tentativi gia' fatti ed esclusi.
Altrimenti rispondi: `REJECTED: rimandato a planner - mancano i requisiti di ingresso (repro / problema isolato)`.

## Mandato
- Analisi della causa radice PRIMA di toccare qualsiasi riga; poi un fix minimo e mirato.
- Implementazioni estese o riscritture tornano a coder o light tramite Work Order.
- Spiega l'evidenza che dimostra la causa prima di applicare il fix; non ripetere tentativi falliti.
- Esegui la riproduzione per certificare il fix; chiudi con DONE e un report (sintomo, causa, fix minimo).""", rows=9)
label(".claude/agents/research.md")
code("""---
name: research
description: Esplorazione ampia in sola lettura su molti file; risponde con sintesi compatte. Non modifica mai.
model: haiku
effort: low
tools: Read, Grep, Glob, Bash
---
Sei il subagent di ricerca in sola lettura.

## Regole (zero spreco di token)
1. Sola lettura: nessuna modifica, nessun file nuovo. Bash solo per comandi in sola lettura (grep, git log, cindex).
2. Navigazione efficace: grep -n e indice dei simboli per le posizioni esatte; mai file enormi interi,
   usa Read con offset e limit ridotti.
3. Risposta ultra-compatta: niente blocchi di codice incollati; al massimo 5-10 punti con percorsi file:riga,
   firme delle funzioni o formati dei dati, e una conclusione di una riga per l'agente chiamante.""", rows=9)

h1("11. Template: tre skill  (.claude/skills/&lt;name&gt;/SKILL.md)")
p("Una skill &egrave; una cartella con un " + c("SKILL.md") + ". Solo la sua <b>description</b> resta sempre nel contesto; il corpo si carica quando la skill viene invocata (progressive disclosure), "
  "quindi le description vanno tenute a una o due frasi con il trigger per primo e i corpi brevi (il tetto ufficiale &egrave; 500 righe; questo kit resta sotto le 60). "
  "Claude Code legge " + c(".claude/skills/") + "; Antigravity legge " + c(".agents/skills/") + ", quindi duplica la cartella se usi entrambi.")
label(".claude/skills/work-order/SKILL.md")
code("""---
name: work-order
description: Costruisce Work Order e applica il gate di prontezza prima di delegare a coder, mechanic o light.
---
# Work Order e gate di prontezza
Rispondi alle 5 domande: (1) file e simboli nominati? (2) diff deterministico? (3) esiste il comando di verifica?
(4) 3 file o meno? (5) nessuna decisione sospesa?
- 5/5 SI': task PRONTA -> scegli mechanic, coder o light.
- anche un solo NO: NON PRONTA -> planner indaga ed emette uno o piu' Work Order.

## Template
### WORK ORDER: [ID]
- Objective / Target Files & Symbols / Required Mutation / Verification Command / Expected Result / Designated Agent

## Protocollo di chiusura
DONE (verifica passata) | BLOCKED: <motivo> (torna a planner) | FAILED x2 (sale di un livello)""", rows=9)
label(".claude/skills/docs-map/SKILL.md")
code("""---
name: docs-map
description: Quale file aprire per area e come leggere i file grossi senza sprechi. Usala prima di doc grandi.
---
# Dove leggere e come aggiornare (poco, a sezioni)
Un solo indice: docs/progress.md. Si parte da li'. Mai un file grosso per intero: grep -n sul simbolo o su '^##',
poi Read con offset/limit. Un file gia' letto in sessione non si rilegge.

## Mappa delle aree
| Area   | Apri                      |
|--------|---------------------------|
| <area> | docs/progress/<area>.md   |

## Aggiornare i progressi dopo un cambio
1. grep -n '^##' sulla scheda d'area; leggi solo la sottosezione del modulo toccato.
2. Correggi o togli le righe diventate false. Registra lo stato (file, simboli), mai date o "abbiamo fatto X".
3. Scheda oltre ~120 righe: spezzala per tema (<area>-<topic>.md), linkala dal file madre e dall'indice.
4. Delega a mechanic se le righe sono gia' decise.

## Ricerche larghe
"Dove sta X / chi usa Y" su piu' file -> agente research (torna con la conclusione, non con i file).""", rows=9)
label(".claude/skills/docs-loop/SKILL.md")
code("""---
name: docs-loop
description: Riordina CLAUDE.md e docs/progress* a giri misura-tieni-scarta. Usala dopo modifiche ai doc.
---
# docs-loop
Misura = python scripts/docs_audit.py score (piu' basso e' meglio).

## Ciclo (max 8 giri, una sola ipotesi per giro), scegli in quest'ordine
1. Una penalita' dell'audit: orfano -> linkalo; rotto -> correggi; oltre 120 righe -> spezza; duplicato -> una copia.
2. Una regola di area dentro CLAUDE.md -> spostala nella sezione "Rules" della scheda, lascia una riga.
3. Il file piu' riletto -> spezzalo o aggiungi un comando di ricerca nella skill docs-map.
4. Una riga d'indice che non serve a trovare un file -> toglila.
Ogni giro: copia di backup dei file toccati; modifica; verifica che l'informazione sia SPOSTATA, non persa; lancia score.
Punteggio sceso e nulla perso -> tieni e registra; altrimenti ripristina il backup e registra lo scarto.
Si ferma a 8 giri, dopo 2 scarti di fila, o quando finiscono le ipotesi.

## Workflow ripetuti -> skill
Un prompt o una sequenza di comandi che torna in 3 o piu' sessioni e che nessuna skill copre diventa una skill
(sotto le 60 righe, trigger nella description).

## Chiusura
Riporta punteggio prima -> dopo, giri tenuti/scartati, skill create. Nessun commit se non richiesto.""", rows=9)

h1("12. Template: .claude/settings.json")
p("Due compiti: tenere piccolo il contesto sempre caricato e trasformare le regole che non devono mai essere violate in <b>enforcement</b> invece che in consigli. "
  "Le regole di permesso hanno la forma " + c("Tool(pattern)") + "; per la logica che un pattern non esprime si usa un hook " + c("PreToolUse") + ". "
  "Le impostazioni di progetto stanno in " + c(".claude/settings.json") + " (condiviso); le personali in " + c(".claude/settings.local.json") + " (ignorato da git). "
  "La chiave " + c("skillOverrides") + " viene dal progetto di riferimento e non compare nel riferimento pubblico delle impostazioni che ho controllato: verificala sulla tua versione di Claude Code.")
label(".claude/settings.json")
code("""{
  "$schema": "https://json.schemastore.org/claude-code-settings.json",
  "permissions": {
    "allow": [
      "Bash(make test-*)",
      "Bash(python3 scripts/docs_audit.py *)",
      "Bash(python3 scripts/cindex.py *)"
    ],
    "deny": [
      "Read(./.env)",
      "Read(./.env.*)",
      "Read(./secrets/**)"
    ]
  },
  "skillOverrides": {
    "anthropic-skills:docx": "off",
    "anthropic-skills:pptx": "off",
    "anthropic-skills:xlsx": "off"
  }
}""", rows=9)

h1("13. Template: indice dei progressi e scheda d'area")
label("docs/progress.md  (l'unico indice; link relativi, cos&igrave; l'audit li pu&ograve; seguire)".replace("&igrave;", "i").replace("&ograve;", "o"))
code("""# <Project> - Indice dei Progressi
Stato, non cronaca. Apri UNA scheda d'area, mai tutte.

## Which section to open
| Area   | Scheda                         | Aprila quando                      |
|--------|--------------------------------|------------------------------------|
| <area> | [core](progress/<area>.md)     | <una riga: cosa vive li'>          |

## Open milestones
- M1 <nome>: <una riga> (dettagli: [ROADMAP](ROADMAP.md))

## Reference docs
- [design/<topic>.md](design/<topic>.md): RFC speculative, non stato.""", rows=9)
label("docs/progress/<area>.md  (80-120 righe; tre heading fissi, in inglese perche' sono bersaglio di grep)")
code("""# <Area> - stato attuale
## How it is today
### <modulo>
- <cosa esiste adesso>; simboli da cercare: `file.ext:symbol`

## Open work
- [ ] <task>. Verifica: `<comando>`

## Rules (do not break)
- <vincolo non derogabile>. Perche': <una riga>""", rows=9)

h1("14. Starter: scripts/docs_audit.py")
p("Un audit senza dipendenze (Python 3), provato su un progetto vuoto. Il punteggio &egrave; dato dai token sempre caricati (" + c("CLAUDE.md") + " + indice) pi&ugrave; la dimensione media delle schede pi&ugrave; le penalit&agrave;; "
  "esce con codice diverso da zero se c'&egrave; una penalit&agrave; o se il punteggio sale sopra la base salvata. " + c("score --save") + " scrive la base in " + c(".docs-score") + ". Il codice resta in inglese.")
label("scripts/docs_audit.py")
code('''#!/usr/bin/env python3
"""docs_audit.py score [--save]: one number (lower is better). Tokens = bytes / 4."""
import glob, os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX, SHEET_MAX, ROOT_MAX = "docs/progress.md", 120, 60
BASE = os.path.join(ROOT, ".docs-score")

def rd(p):
    try:
        return open(os.path.join(ROOT, p), encoding="utf8").read()
    except OSError:
        return ""

def tok(p):
    return len(rd(p).encode()) / 4

def links(p):
    for m in re.finditer(r"\\]\\(([^)#\\s]+\\.md)", rd(p)):
        yield os.path.normpath(os.path.join(os.path.dirname(p), m.group(1))).replace(os.sep, "/")

def score(save):
    sheets = sorted(os.path.relpath(f, ROOT).replace(os.sep, "/") for f in glob.glob(ROOT + "/docs/progress/*.md"))
    linked, pen = set(links(INDEX)), []
    if rd("CLAUDE.md").count("\\n") > ROOT_MAX:
        pen.append((400, f"CLAUDE.md over {ROOT_MAX} lines: move area rules to their sheet"))
    if INDEX not in rd("CLAUDE.md"):
        pen.append((2000, "CLAUDE.md does not point to the single index"))
    for s in sheets:
        if s not in linked:
            pen.append((500, f"orphan: {s}"))
        if rd(s).count("\\n") > SHEET_MAX:
            pen.append((200, f"{s} over {SHEET_MAX} lines: split by topic"))
    for f in ["CLAUDE.md", INDEX] + sheets:
        for l in links(f):
            if not os.path.exists(os.path.join(ROOT, l)):
                pen.append((300, f"broken link in {f}: {l}"))
    mean = sum(map(tok, sheets)) / max(1, len(sheets))
    total = round(tok("CLAUDE.md") + tok(INDEX) + mean + sum(p for p, _ in pen))
    print(f"score {total}")
    for _, m in pen:
        print("  !", m)
    bad = bool(pen)
    if os.path.exists(BASE) and not save:
        base = int(open(BASE).read().strip() or 0)
        if total > base:
            print(f"  ! score rose above baseline {base}")
            bad = True
    if save and not bad:
        open(BASE, "w").write(str(total))
    return 1 if bad else 0

if __name__ == "__main__":
    a = sys.argv[1:]
    sys.exit(score("--save" in a) if a[:1] == ["score"] else __doc__)''', rows=11)

# ============================================================ PARTE C
part("PARTE C — AUTOMAZIONE E STRUMENTI")

h1("15. I tre pilastri dell'automazione continua")
p("<b>A. Hook git pre-commit.</b> Gli hook dentro " + c(".git/hooks") + " non sono versionati, quindi l'hook sta nel repo e si indica a git una volta per ogni clone:")
label(".githooks/pre-commit   (chmod +x, poi: git config core.hooksPath .githooks)")
code("""#!/bin/sh
python3 scripts/docs_audit.py score || { echo "docs audit failed: fix links/orphans/size"; exit 1; }""")
p("<b>B. Makefile / build tool.</b> Aggiungi un target per i doc e includilo nel target di test standard:")
code("""test-docs:
    python3 scripts/docs_audit.py score    # le righe di ricetta iniziano con un TAB, non con spazi

test: test-docs   # aggiungilo ai prerequisiti del tuo target di test standard""")
p("<b>C. CI/CD (GitHub Actions).</b> Uno step dedicato dentro il workflow di build/test che gira sulle pull request:")
code("""      - name: Check documentation audit
        run: python3 scripts/docs_audit.py score""")

h1("16. Focus: Google Antigravity e Gemini")
p("Claude si appoggia a " + c("CLAUDE.md") + "; Google Antigravity adotta un'architettura modulare in stile enterprise:")
b("<b>File primario " + c("GEMINI.md") + " (o " + c("AGENTS.md") + "):</b> ha precedenza assoluta. Dispone di un budget di regole dedicato (20.000 token, separato dal contesto generale) con troncamento intelligente e passaggio a puntatori on-demand se si supera il limite.")
b("<b>Progressive disclosure nativa:</b> le skill in " + c(".agents/skills/") + " (o " + c(".claude/skills/") + ") espongono nel prompt solo un nome e una description brevi; il corpo esteso si carica solo se il modello decide di attivarle.")
b("<b>Relay mobile / cloud via GitHub Actions:</b> con " + c(".github/workflows/antigravity-agent.yml") + " l'agente pu&ograve; essere lanciato dallo smartphone sui runner di GitHub Actions, senza usare risorse locali.")
b("<b>Subagent paralleli asincroni</b> con strumenti differenziati (per esempio un agente " + c("research") + " in sola lettura che tiene pulito il contesto principale, ed esecuzioni indipendenti autosufficienti).")
table([
    ["Aspetto", "Claude Code", "Google Antigravity"],
    ["Istruzioni di root", c("CLAUDE.md") + " (AGENTS.md solo se manca CLAUDE.md o se importato)", c("GEMINI.md") + " o " + c("AGENTS.md")],
    ["Cartella delle skill", c(".claude/skills/<name>/SKILL.md"), c(".agents/skills/") + " (legge anche " + c(".claude/skills/") + ")"],
    ["Definizione degli agenti", c(".claude/agents/*.md") + " (alias del modello, effort, tools)", "invoke_subagent con modello \"pro\", \"flash\" o \"flash_lite\""],
    ["Enforcement rigido", "permessi allow/deny, hook", "impostazioni del runtime; tieni le stesse regole in GEMINI.md"],
], [92, 215, 197])

h1("17. Plugin per Claude: crab-ledger e better-prompt")
p("Due plugin open source estendono efficienza e qualit&agrave; delle sessioni Claude:")
b("<b>crab-ledger (monitoraggio di costi e riletture):</b> traccia in tempo reale l'uso dei token, conta le riletture dei file e calcola il costo di ogni sessione. Si abbina a " + c("scripts/docs_audit.py rereads") +
  " (versione completa dell'audit) e scrive " + c("~/.claude/crab-ledger-report.json") + ", usato per individuare i colli di bottiglia e i file riletti cos&igrave; spesso da dover essere spezzati.")
b("<b>better-prompt (prompt engineering):</b> raffina, struttura e ottimizza i prompt inviati a Claude: toglie le ambiguit&agrave;, applica ruoli e formato chiari e riduce il consumo di token alla fonte.")
p("Entrambi sono installabili dai rispettivi repository GitHub (crab-ledger e better-prompt). La skill docs-loop legge il report di crab-ledger per decidere quali file spezzare per primi.")

h1("18. Cosa Claude pu&ograve; ereditare da Antigravity / Gemini")
n(1, "<b>Subagent research separato per le ricerche pesanti:</b> invece di far leggere decine di file alla sessione principale, delega a un agente rapido in sola lettura (Haiku/Sonnet) che restituisce una sintesi di 5 righe.")
n(2, "<b>Progressive disclosure per le istruzioni:</b> sposta le procedure complesse in skill on-demand con frontmatter YAML e tieni " + c("CLAUDE.md") + " al minimo indispensabile.")
n(3, "<b>Deduplicazione tra agenti:</b> tieni " + c("CLAUDE.md") + " e " + c("GEMINI.md") + " allineati sullo stesso indice (" + c("docs/progress.md") + "), cos&igrave; qualsiasi membro del team pu&ograve; usare l'uno o l'altro assistente con le stesse garanzie.")

# ============================================================ PARTE D
part("PARTE D — REGOLE BASE PER MIGLIORARE E IMPLEMENTARE QUESTA GUIDA")
h1("19. Regole base e ciclo di miglioramento")
p("Parti da queste regole in ogni nuovo progetto. Ognuna indica come viene fatta rispettare, cos&igrave; lo scaffolding migliora da solo invece di degradare.")
table([
    ["#", "Regola", "Applicata da"],
    ["R1", "<b>Un solo indice.</b> Tutto lo stato si raggiunge da " + c("docs/progress.md") + "; CLAUDE.md rimanda l&igrave; e non elenca altri doc.", "Penalit&agrave; audit (+2000)"],
    ["R2", "<b>Root sottile.</b> CLAUDE.md sotto le 60 righe; una regola che riguarda una sola area passa nella sezione Rules di quell'area, lasciando una riga.", "Penalit&agrave; audit (+400)"],
    ["R3", "<b>Stato, non cronaca.</b> Le schede dicono com'&egrave; adesso: file e simboli, niente date, niente \"abbiamo fatto X\". Il lavoro chiuso si toglie da Open work.", "Revisione docs-loop"],
    ["R4", "<b>Schede piccole.</b> 80-120 righe ciascuna; quelle pi&ugrave; grandi si spezzano per tema e si linkano da file madre e indice.", "Penalit&agrave; audit (+200)"],
    ["R5", "<b>Letture chirurgiche.</b> Mai un file grosso intero; grep poi offset/limit; mai rileggere un file gi&agrave; letto in sessione; le ricerche larghe vanno a research.", "Report riletture di crab-ledger"],
    ["R6", "<b>Routing per prontezza, non per grandezza.</b> Applica il gate a 5 domande; un NO va a planner; niente arriva a deep senza repro.", "Description agenti, skill work-order"],
    ["R7", "<b>Stato esplicito.</b> Ogni agente chiude con DONE, BLOCKED o FAILED x2; al massimo due tentativi per livello, poi sale di un livello.", "Corpo degli agenti"],
    ["R8", "<b>La verifica &egrave; obbligatoria.</b> Nessun Work Order senza un comando deterministico; gli agenti che devono lanciarlo hanno Bash in " + c("tools") + ".", "Template Work Order, frontmatter agenti"],
    ["R9", "<b>Leave no trace.</b> Chiudi ogni task: aggiorna Open work, aggiungi ogni nuovo vincolo a Rules, lancia l'audit.", "CLAUDE.md sezione 5, hook"],
    ["R10", "<b>Cricchetto.</b> Il punteggio dell'audit non sale mai sopra la base salvata; abbassalo di proposito e risalvalo.", "docs_audit.py, CI"],
    ["R11", "<b>Ciclo di miglioramento.</b> Una ipotesi per giro, misura, tieni o scarta, massimo 8 giri, stop dopo 2 scarti consecutivi.", "Skill docs-loop"],
    ["R12", "<b>Ripetizione = skill.</b> Un workflow visto in 3 o pi&ugrave; sessioni diventa una skill sotto le 60 righe con il trigger nella description.", "Skill docs-loop"],
    ["R13", "<b>Impara dal costo.</b> Un bug o una sorpresa costata pi&ugrave; di una sessione aggiunge una riga alle Rules dell'area, con il motivo.", "Passo di chiusura (R9)"],
    ["R14", "<b>Regola dalle evidenze.</b> Ogni mese conta BLOCKED e FAILED x2 per agente: molti BLOCKED = gate o planner troppo deboli; molti FAILED x2 a un livello = alza il livello o stringi il template del Work Order.", "Revisione manuale"],
    ["R15", "<b>Le regole rigide si impongono, non si chiedono.</b> Segreti, comandi distruttivi e percorsi vietati vanno nelle regole deny dei permessi o in un hook PreToolUse, non solo in CLAUDE.md.", "settings.json"],
    ["R16", "<b>Budget sempre caricato.</b> Spegni le skill integrate che il progetto non usa; tieni ogni description a 1-2 frasi.", "settings.json, revisione"],
    ["R17", "<b>Parit&agrave;.</b> CLAUDE.md e GEMINI.md condividono alla lettera le sezioni 1, 2, 4, 5; cambia solo la tabella di routing.", "Revisione della pull request"],
    ["R18", "<b>Default sicuri.</b> Branch prima di modificare, solo test selettivi, nessun segreto nel repo, commit solo su richiesta.", "CLAUDE.md sezione 2"],
], [26, 372, 106])

doc = SimpleDocTemplate(os.path.abspath("docs/scaffold-IT.pdf"), pagesize=letter,
                        leftMargin=54, rightMargin=54, topMargin=54, bottomMargin=54,
                        title="Guida allo Scaffolding AI e all'Architettura degli Agenti", author="bm project")
doc.build(story, canvasmaker=NumberedCanvas)
print("PDF generato: docs/scaffold-IT.pdf")
