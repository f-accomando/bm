# generate_guide_pdf.py
import os
import sys
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, KeepTogether, PageBreak, HRFlowable
)
from reportlab.lib import colors
from reportlab.pdfgen import canvas

class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_page_decorations(num_pages)
            super().showPage()
        super().save()

    def draw_page_decorations(self, page_count):
        self.saveState()
        self.setFont("Helvetica", 8)
        self.setFillColor(colors.HexColor("#718096"))
        
        # Header (pages > 1)
        if self._pageNumber > 1:
            self.drawString(54, 755, "Guida Scaffolding & Context Engineering per Claude e Google Antigravity / Gemini")
            self.setStrokeColor(colors.HexColor("#E2E8F0"))
            self.setLineWidth(0.5)
            self.line(54, 747, letter[0] - 54, 747)
            
        # Footer
        footer_text = f"Pagina {self._pageNumber} di {page_count}"
        self.drawRightString(letter[0] - 54, 36, footer_text)
        self.drawString(54, 36, "Modello Standard di Automazione Scaffolding — Repositories & Agentic Systems")
        self.setStrokeColor(colors.HexColor("#E2E8F0"))
        self.setLineWidth(0.5)
        self.line(54, 48, letter[0] - 54, 48)
        
        self.restoreState()

def build_pdf(filename):
    doc = SimpleDocTemplate(
        filename,
        pagesize=letter,
        leftMargin=54,
        rightMargin=54,
        topMargin=54,
        bottomMargin=54
    )
    
    styles = getSampleStyleSheet()
    
    primary = colors.HexColor("#1A365D")   # Deep navy
    secondary = colors.HexColor("#2B6CB0") # Medium blue
    dark_text = colors.HexColor("#2D3748") # Charcoal
    accent = colors.HexColor("#319795")    # Teal accent
    gemini_accent = colors.HexColor("#4285F4") # Google blue
    code_bg = colors.HexColor("#F7FAFC")
    border_color = colors.HexColor("#E2E8F0")

    title_style = ParagraphStyle(
        'DocTitle',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=18,
        leading=22,
        textColor=primary,
        spaceAfter=5
    )
    
    subtitle_style = ParagraphStyle(
        'DocSubtitle',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=10,
        leading=14,
        textColor=colors.HexColor("#4A5568"),
        spaceAfter=12
    )
    
    h1_style = ParagraphStyle(
        'Heading1_Custom',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=12,
        leading=15,
        textColor=primary,
        spaceBefore=12,
        spaceAfter=6,
        keepWithNext=True
    )
    
    h2_style = ParagraphStyle(
        'Heading2_Custom',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=9.5,
        leading=13,
        textColor=secondary,
        spaceBefore=8,
        spaceAfter=3,
        keepWithNext=True
    )
    
    body_style = ParagraphStyle(
        'Body_Custom',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=8.5,
        leading=12,
        textColor=dark_text,
        spaceAfter=5
    )
    
    bullet_style = ParagraphStyle(
        'Bullet_Custom',
        parent=body_style,
        leftIndent=12,
        firstLineIndent=-8,
        spaceAfter=2.5
    )
    
    code_style = ParagraphStyle(
        'CodeBlock',
        parent=styles['Normal'],
        fontName='Courier',
        fontSize=7,
        leading=9.5,
        textColor=colors.HexColor("#1A202C")
    )
    
    table_header_style = ParagraphStyle(
        'TableHeader',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=8,
        leading=10,
        textColor=colors.white
    )
    
    table_cell_style = ParagraphStyle(
        'TableCell',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7.5,
        leading=10.5,
        textColor=dark_text
    )

    story = []

    # Title & Header Banner
    story.append(Paragraph("Architettura dello Scaffolding Context-Aware per AI", title_style))
    story.append(Paragraph("Guida operativa per Claude e Google Antigravity / Gemini: regole, progressive disclosure, automazione e sinergie.", subtitle_style))
    story.append(HRFlowable(width="100%", thickness=1.5, color=primary, spaceBefore=0, spaceAfter=10))

    # Sezione 1: Il Workflow di Sessione
    story.append(Paragraph("1. Come Funziona il Workflow di Sessione (Zero Token Waste)", h1_style))
    story.append(Paragraph(
        "Caricare documentazione intera distrugge il budget di contesto e genera allucinazioni. "
        "L'architettura adottata utilizza una struttura gerarchica a <b>recupero chirurgico on-demand</b>:",
        body_style
    ))
    story.append(Paragraph("• <b>Root Guard (<code>CLAUDE.md</code> e <code>GEMINI.md</code>):</b> Caricati come regole di root all'avvio (< 60 righe). Contengono invarianti globali, regole di branching/licenza e la tabella di consultazione.", bullet_style))
    story.append(Paragraph("• <b>Index Map (<code>docs/progress.md</code>):</b> L'unico file indice. Non viene letto per intero ogni volta: serve solo per instradare verso la scheda d'area corretta.", bullet_style))
    story.append(Paragraph("• <b>Estrazione Mirata via Grep/Offset:</b> L'AI consulta direttamente i task (<code>grep -n -A 15 '^## Open work' docs/progress/&lt;area&gt;.md</code>) o le regole (<code>grep -n -A 20 '^## Rules' ...</code>) consumando ~100 token invece di 10.000.", bullet_style))

    # Tabella flussi
    table_data = [
        [Paragraph("Fase", table_header_style), Paragraph("Azione dell'AI", table_header_style), Paragraph("File Coinvolti & Output", table_header_style)],
        [
            Paragraph("<b>1. Boot</b>", table_cell_style),
            Paragraph("Caricamento regole base e comandi rapidi.", table_cell_style),
            Paragraph("<code>CLAUDE.md</code> / <code>GEMINI.md</code> (~50 righe)", table_cell_style)
        ],
        [
            Paragraph("<b>2. Routing</b>", table_cell_style),
            Paragraph("Identificazione dell'area target se non ovvia.", table_cell_style),
            Paragraph("<code>docs/progress.md</code> (solo lookup)", table_cell_style)
        ],
        [
            Paragraph("<b>3. Lookup</b>", table_cell_style),
            Paragraph("Estrazione chirurgica di task aperti e vincoli.", table_cell_style),
            Paragraph("<code>grep -n -A 15 '^## Open' docs/progress/&lt;area&gt;.md</code>", table_cell_style)
        ],
        [
            Paragraph("<b>4. Esecuzione</b>", table_cell_style),
            Paragraph("Ispezione mirata (AST/cindex) e test selettivi.", table_cell_style),
            Paragraph("Sorgenti puntuali, <code>make test-&lt;area&gt;</code>", table_cell_style)
        ],
        [
            Paragraph("<b>5. Chiusura</b>", table_cell_style),
            Paragraph("Protocollo Leave-No-Trace: update stato e audit.", table_cell_style),
            Paragraph("<code>docs/progress/&lt;area&gt;.md</code>, <code>docs_audit.py</code>", table_cell_style)
        ]
    ]
    t = Table(table_data, colWidths=[65, 245, 194])
    t.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), primary),
        ('ALIGN', (0,0), (-1,-1), 'LEFT'),
        ('VALIGN', (0,0), (-1,-1), 'TOP'),
        ('INNERGRID', (0,0), (-1,-1), 0.5, border_color),
        ('BOX', (0,0), (-1,-1), 1, primary),
        ('TOPPADDING', (0,0), (-1,-1), 3),
        ('BOTTOMPADDING', (0,0), (-1,-1), 3),
    ]))
    story.append(t)

    story.append(Spacer(1, 6))

    # Sezione 2: La tripla separazione documentale
    story.append(Paragraph("2. Struttura dei File da Creare in un Nuovo Progetto", h1_style))
    story.append(Paragraph(
        "Per evitare il degrado nel tempo, la documentazione deve essere ripartita in 3 livelli a ciclo di vita separato:",
        body_style
    ))
    story.append(Paragraph("1. <b>File Operativo Globale (<code>CLAUDE.md</code> e <code>GEMINI.md</code>):</b> Massimo 60 righe. Solo comandi, test, guardrail e indice scorciatoie. Zero cronologia o log di release.", bullet_style))
    story.append(Paragraph("2. <b>Living Progress (<code>docs/progress/&lt;area&gt;.md</code>):</b> Schede di lavoro attive da 80–120 righe. Contengono solo lo stato attuale del codice, task aperti (<code>## Open work</code>) e regole fisse (<code>## Rules</code>).", bullet_style))
    story.append(Paragraph("3. <b>Design & RFC (<code>docs/design/&lt;topic&gt;.md</code>):</b> Idee, proposte evolutive, architettura a lungo termine. Essendo testi lunghi e speculativi, <u>non devono stare in progress</u> per non inquinare l'audit dei token.", bullet_style))

    story.append(Spacer(1, 6))

    # Sezione 3: Specifiche Google Antigravity / Gemini
    story.append(Paragraph("3. Focus Google Antigravity & Gemini: Differenze e Punti di Forza", h1_style))
    story.append(Paragraph(
        "Mentre Claude si affida a <code>CLAUDE.md</code>, Google Antigravity adotta un'architettura enterprise modulare:",
        body_style
    ))
    story.append(Paragraph("• <b>File Primario <code>GEMINI.md</code> (o <code>AGENTS.md</code>):</b> Ha precedenza assoluta. Dispone di un budget di regole dedicato (20.000 token separato dal contesto generale) con troncamento intelligente e switch a puntatori on-demand se si supera il limite.", bullet_style))
    story.append(Paragraph("• <b>Progressive Disclosure Nativa:</b> Le skill in <code>.agents/skills/</code> o <code>.claude/skills/</code> espongono solo metadati brevi (name e description) nel prompt; il corpo esteso viene caricato solo se il modello decide di attivarle.", bullet_style))
    story.append(Paragraph("• <b>Mobile / Cloud Relay tramite GitHub Actions:</b> Grazie a <code>.github/workflows/antigravity-agent.yml</code>, l'agente può essere lanciato dallo smartphone tramite i runner di GitHub Actions senza consumare risorse locali.", bullet_style))
    story.append(Paragraph("• <b>Subagent Paralleli Asincroni:</b> Antigravity supporta nativamente subagent con tool differenziati (es. <code>research</code> in sola lettura per non inquinare il contesto, <code>self</code> per esecuzioni indipendenti).", bullet_style))

    story.append(Spacer(1, 6))

    # Sezione 4: Plugin Ecosistema: crab-ledger & better-prompt
    story.append(Paragraph("4. Plugin per Claude: crab-ledger e better-prompt", h1_style))
    story.append(Paragraph(
        "Per estendere ulteriormente l'efficienza e la qualità dei prompt nelle sessioni Claude, sono disponibili due plugin open-source universali:",
        body_style
    ))
    story.append(Paragraph("• <b>crab-ledger (Monitoraggio Costi & Riletture):</b> Plugin che traccia in tempo reale l'uso dei token, le riletture dei file (<i>rereads</i>) e calcola il costo di ogni singola sessione. Si interfaccia con <code>scripts/docs_audit.py rereads</code> producendo il report <code>~/.claude/crab-ledger-report.json</code> per identificare i colli di bottiglia e i file troppo riletti che necessitano di spezzamento.", bullet_style))
    story.append(Paragraph("• <b>better-prompt (Prompt Engineering & Ottimizzazione):</b> Plugin che raffina, struttura e ottimizza automaticamente i prompt inviati a Claude. Applica pattern di prompt engineering avanzati, rimuove ambiguità, formatta le istruzioni con ruoli chiari e riduce il consumo di token alla sorgente.", bullet_style))
    story.append(Paragraph("<i>Entrambi i plugin sono disponibili e installabili pubblicamente dai repository di GitHub (<code>/crab-ledger</code> e <code>/better-prompt</code>).</i>", body_style))

    story.append(Spacer(1, 6))

    # Sezione 5: Cosa può ereditare Claude da Antigravity/Gemini
    story.append(Paragraph("5. Cosa Claude Può Ereditare da Antigravity / Gemini", h1_style))
    story.append(Paragraph(
        "Possiamo arricchire l'ambiente Claude integrando i seguenti pattern mutuati da Antigravity:",
        body_style
    ))
    story.append(Paragraph("1. <b>Subagent 'Research' Separato per Ricerche Pesanti:</b> Invece di far leggere decine di file a Claude nella sessione principale, delegare a un subagent rapido (es. Haiku/Sonnet in sola lettura) che restituisce solo un riassunto finale da 5 righe.", bullet_style))
    story.append(Paragraph("2. <b>Progressive Disclosure per le Istruzioni:</b> Spostare procedure complesse in skill on-demand con YAML frontmatter, mantenendo <code>CLAUDE.md</code> asciutto al minimo indispensabile.", bullet_style))
    story.append(Paragraph("3. <b>Deduplicazione Cross-Agent:</b> Mantenere <code>CLAUDE.md</code> e <code>GEMINI.md</code> perfettamente allineati e sincronizzati sullo stesso indice (<code>docs/progress.md</code>), così che qualsiasi membro del team possa usare l'uno o l'altro assistente con le medesime garanzie.", bullet_style))

    story.append(Spacer(1, 6))

    # Sezione 6: Template di Avvio
    story.append(Paragraph("6. Template Universale di Avvio: CLAUDE.md & GEMINI.md", h1_style))
    story.append(Paragraph("Utilizza questa struttura alla radice del repository per garantire compatibilità con entrambi gli agenti:", body_style))

    claude_template_code = """# [Nome Progetto] — Note Operative di Sessione (Claude & Gemini)
Repository [org/repo], branch `main`. Stack: [linguaggi, architettura, OS].

## 1. Recupero Mirato (Zero Spreco Token)
Non leggere file per intero. Lo stato attuale e l'indice sono in `docs/progress.md`.
- Trovare cosa c'è da fare: `grep -n -A 15 '^## Open work' docs/progress/<area>.md`
- Verificare vincoli/regole: `grep -n -A 20 '^## Rules' docs/progress/<area>.md`
- Ricerca definizioni/funzioni: `python scripts/cindex.py <file> <simbolo>` o `grep -n`

## 2. Regole di Ingaggio
- Branching: Creare branch `feat/<nome>` prima di sviluppare. Mai lavorare su `main`.
- Test Selettivi: Lanciare solo i test d'area toccati (es. `make test-<area>`).
- Nessun segreto (chiavi API, token) committato nei sorgenti.

## 3. Subagent Specializzati
- `bm-research`: Esplorazione read-only e ricerca documentazione.
- `bm-coder` / `bm-light`: Implementazione e test da Work Order (più file solo se già specificati).
- `bm-mechanic`: Aggiornamento spunte e task in `docs/progress/`.

## 4. Chiusura Task & Protocollo Leave-No-Trace
Prima di terminare o dare la risposta finale:
1. Aggiorna `## Open work` nella scheda d'area (rimuovi o spunta compiti finiti).
2. Se hai introdotto un vincolo o pattern, aggiungilo in `## Rules (do not break)`.
3. Esegui `python scripts/docs_audit.py score` per validare link e assenza di orfani."""

    t_code = Table([[Paragraph(claude_template_code.replace("\n", "<br/>").replace(" ", "&nbsp;"), code_style)]], colWidths=[504])
    t_code.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), code_bg),
        ('BOX', (0,0), (-1,-1), 1, border_color),
        ('LEFTPADDING', (0,0), (-1,-1), 8),
        ('RIGHTPADDING', (0,0), (-1,-1), 8),
        ('TOPPADDING', (0,0), (-1,-1), 5),
        ('BOTTOMPADDING', (0,0), (-1,-1), 5),
    ]))
    story.append(t_code)

    story.append(Spacer(1, 6))

    # Sezione 7: Automazione Continua a Tripla Barriera
    story.append(Paragraph("7. I Tre Pilastri dell'Automazione Continua", h1_style))
    story.append(Paragraph("<b>A. Git Pre-Commit Hook (<code>.git/hooks/pre-commit</code>):</b> Esegue <code>python scripts/docs_audit.py score</code> e blocca il commit locale se ci sono link rotti o file orfani.", body_style))
    story.append(Paragraph("<b>B. Target Makefile / Build Tool:</b> Inserire il target <code>test-docs</code> nella suite dei test standard (<code>make test</code> o <code>make test-host</code>).", body_style))
    story.append(Paragraph("<b>C. CI/CD GitHub Actions:</b> Uno step dedicato <code>Check documentation audit</code> (<code>python3 scripts/docs_audit.py score</code>) all'interno del workflow di build/test su PR.", body_style))

    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"PDF generato con successo: {filename}")

if __name__ == '__main__':
    out = os.path.abspath("docs/Scaffolding_Context_Engineering_Guide.pdf")
    build_pdf(out)
