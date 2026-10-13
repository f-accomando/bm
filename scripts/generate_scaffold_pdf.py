# generate_scaffold_pdf.py
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
            self.drawString(54, 755, "AI Scaffolding & Agent Architecture Guide — Claude Code & Google Antigravity")
            self.setStrokeColor(colors.HexColor("#E2E8F0"))
            self.setLineWidth(0.5)
            self.line(54, 747, letter[0] - 54, 747)
            
        # Footer
        footer_text = f"Page {self._pageNumber} of {page_count}"
        self.drawRightString(letter[0] - 54, 36, footer_text)
        self.drawString(54, 36, "Standard Context-Aware Scaffolding & Multi-Agent Engine")
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
    code_bg = colors.HexColor("#F7FAFC")
    border_color = colors.HexColor("#E2E8F0")

    title_style = ParagraphStyle(
        'DocTitle',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=18,
        leading=22,
        textColor=primary,
        spaceAfter=4
    )
    
    subtitle_style = ParagraphStyle(
        'DocSubtitle',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=9.5,
        leading=13,
        textColor=colors.HexColor("#4A5568"),
        spaceAfter=10
    )
    
    h1_style = ParagraphStyle(
        'Heading1_Custom',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=11,
        leading=14,
        textColor=primary,
        spaceBefore=10,
        spaceAfter=5,
        keepWithNext=True
    )
    
    body_style = ParagraphStyle(
        'Body_Custom',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=8,
        leading=11.5,
        textColor=dark_text,
        spaceAfter=4
    )
    
    bullet_style = ParagraphStyle(
        'Bullet_Custom',
        parent=body_style,
        leftIndent=10,
        firstLineIndent=-7,
        spaceAfter=2
    )
    
    code_style = ParagraphStyle(
        'CodeBlock',
        parent=styles['Normal'],
        fontName='Courier',
        fontSize=6.8,
        leading=9,
        textColor=colors.HexColor("#1A202C")
    )
    
    table_header_style = ParagraphStyle(
        'TableHeader',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=7.5,
        leading=9.5,
        textColor=colors.white
    )

    table_cell_style = ParagraphStyle(
        'TableCell',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7,
        leading=9.5,
        textColor=dark_text
    )

    story = []

    # Title & Banner
    story.append(Paragraph("Context-Aware AI Scaffolding & Agent Architecture", title_style))
    story.append(Paragraph("Operational handbook for Claude Code & Google Antigravity: token economics, readiness gates, and work-order routing.", subtitle_style))
    story.append(HRFlowable(width="100%", thickness=1.5, color=primary, spaceBefore=0, spaceAfter=8))

    # Section 1: The Core Principle
    story.append(Paragraph("1. The Core Principle: Separate Decision-Making from Execution", h1_style))
    story.append(Paragraph(
        "Modern frontier models (Claude Opus, Gemini Pro) suffer from a critical cost bottleneck: <b>output token asymmetry</b>. "
        "High-tier models cost 20x to 40x more per output token than entry-tier models (Haiku, Flash-Lite). "
        "Generating raw implementation code with Opus burns session quotas rapidly. "
        "The solution is structural separation:",
        body_style
    ))
    story.append(Paragraph("• <b>High-Tier Models (Opus / Gemini Pro):</b> Produce <i>few output tokens</i>. They plan, design, resolve architectural trade-offs, and generate structured Work Orders.", bullet_style))
    story.append(Paragraph("• <b>Low-Tier Models (Haiku / Gemini Flash):</b> Produce <i>many output tokens</i>. They write and refactor code strictly driven by deterministic Work Orders.", bullet_style))
    story.append(Paragraph("• <b>Readiness Routing:</b> Tasks are never dispatched based on perceived 'size' or 'complexity'. They are routed strictly by <b>how well-specified</b> they already are.", bullet_style))

    # Section 2: The 5-Agent Model Hierarchy
    story.append(Paragraph("2. The 5-Tier Agent Architecture", h1_style))
    agent_table = [
        [Paragraph("Agent Name", table_header_style), Paragraph("Claude Tier", table_header_style), Paragraph("Antigravity Tier", table_header_style), Paragraph("Toolset", table_header_style), Paragraph("Mandate & Boundaries", table_header_style)],
        [
            Paragraph("<b>bm-planner</b>", table_cell_style),
            Paragraph("Opus medium", table_cell_style),
            Paragraph("Gemini Pro", table_cell_style),
            Paragraph("Read-only (grep/glob/read)", table_cell_style),
            Paragraph("Transforms vague/large tasks into verified Work Orders. <b>Never writes code.</b>", table_cell_style)
        ],
        [
            Paragraph("<b>bm-coder</b>", table_cell_style),
            Paragraph("Haiku high", table_cell_style),
            Paragraph("Gemini Flash", table_cell_style),
            Paragraph("Full (edit/write/exec)", table_cell_style),
            Paragraph("Writes code from a locked Work Order. <b>No design choices or ambiguity resolution.</b>", table_cell_style)
        ],
        [
            Paragraph("<b>bm-mechanic</b>", table_cell_style),
            Paragraph("Haiku low", table_cell_style),
            Paragraph("Gemini Flash-Lite", table_cell_style),
            Paragraph("Full (atomic edits)", table_cell_style),
            Paragraph("Zero-risk tasks: symbol renames, syntax formatting, progress/roadmap updates.", table_cell_style)
        ],
        [
            Paragraph("<b>bm-light</b>", table_cell_style),
            Paragraph("Sonnet medium", table_cell_style),
            Paragraph("Gemini Flash", table_cell_style),
            Paragraph("Full (read/write/exec)", table_cell_style),
            Paragraph("Focused fixes (1–2 files), technical judgment, code-verified documentation.", table_cell_style)
        ],
        [
            Paragraph("<b>bm-deep</b>", table_cell_style),
            Paragraph("Opus xhigh", table_cell_style),
            Paragraph("Gemini Pro", table_cell_style),
            Paragraph("Full (isolated repro)", table_cell_style),
            Paragraph("Strict gate: single isolated bug with repro or hard architectural decision. Rejects generic tasks.", table_cell_style)
        ]
    ]
    t_agent = Table(agent_table, colWidths=[65, 60, 65, 80, 234])
    t_agent.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), primary),
        ('ALIGN', (0,0), (-1,-1), 'LEFT'),
        ('VALIGN', (0,0), (-1,-1), 'TOP'),
        ('INNERGRID', (0,0), (-1,-1), 0.5, border_color),
        ('BOX', (0,0), (-1,-1), 1, primary),
        ('TOPPADDING', (0,0), (-1,-1), 2.5),
        ('BOTTOMPADDING', (0,0), (-1,-1), 2.5),
    ]))
    story.append(t_agent)

    # Section 3: The Readiness Gate & Escalation Protocol
    story.append(Paragraph("3. The 5-Point Readiness Gate & Escalation Protocol", h1_style))
    story.append(Paragraph(
        "Before any task is routed to an execution model (mechanic, coder, light), the router evaluates a binary checklist:",
        body_style
    ))
    story.append(Paragraph("1. <b>Target Files & Symbols:</b> Are specific file paths and function signatures explicitly identified?", bullet_style))
    story.append(Paragraph("2. <b>Diff Determinism:</b> Is the logic defined so strictly that two independent developers would produce identical diffs?", bullet_style))
    story.append(Paragraph("3. <b>Verifiable Command:</b> Is there an automated check (unit test, QEMU runner, compiler check, audit script)?", bullet_style))
    story.append(Paragraph("4. <b>Constrained Scope:</b> Does the change touch 3 files or fewer?", bullet_style))
    story.append(Paragraph("5. <b>Zero Pending Decisions:</b> Is the work completely free of unresolved design choices?", bullet_style))
    story.append(Paragraph(
        "<b>Rule:</b> If any answer is <b>NO</b>, execution is blocked. The request routes to <code>bm-planner</code>. "
        "Agents terminate execution strictly with three possible statuses: "
        "<b>DONE</b> (verification passes), <b>BLOCKED</b> (ambiguous spec; returns to planner), or <b>FAILED x2</b> (test fails after 2 iterations; escalates to a higher tier).",
        body_style
    ))

    # Section 4: Standard Work Order Format
    story.append(Paragraph("4. Standard Work Order Template", h1_style))
    wo_sample = """### WORK ORDER: [WO-ID]
- Objective: [Single concise sentence defining the outcome]
- Target Files & Symbols: [Explicit paths and functions, e.g., src/bm/runtime.c:bm_init_cart()]
- Required Mutation: [Unambiguous diff specification leaving zero design latitude]
- Verification Command: [Deterministic execution, e.g., python tests/qemu_test.py -k <test_name>]
- Expected Result: [Exit code 0 and exact expected assertion output]
- Designated Agent: [bm-mechanic | bm-coder | bm-light]"""
    t_wo = Table([[Paragraph(wo_sample.replace("\n", "<br/>").replace(" ", "&nbsp;"), code_style)]], colWidths=[504])
    t_wo.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), code_bg),
        ('BOX', (0,0), (-1,-1), 1, border_color),
        ('LEFTPADDING', (0,0), (-1,-1), 7),
        ('RIGHTPADDING', (0,0), (-1,-1), 7),
        ('TOPPADDING', (0,0), (-1,-1), 4),
        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
    ]))
    story.append(t_wo)

    # Section 5: Repository Scaffolding Structure
    story.append(Paragraph("5. Three-Tier Repository Scaffolding Structure", h1_style))
    story.append(Paragraph("To prevent context rot, documentation must be divided across three distinct lifecycles:", body_style))
    story.append(Paragraph("• <b>Root Guardians (<code>CLAUDE.md</code> & <code>GEMINI.md</code>):</b> Kept under 60 lines. Contains only global invariants, build shortcuts, branching rules, and subagent routing. No release history.", bullet_style))
    story.append(Paragraph("• <b>Living Progress (<code>docs/progress/<area>.md</code>):</b> Modular active sheets (80–120 lines). Tracks current state, open work, and unbreachable local rules.", bullet_style))
    story.append(Paragraph("• <b>Deep RFCs (<code>docs/design/<topic>.md</code>):</b> Speculative designs and long-term proposals kept outside the live progress index to prevent token overhead.", bullet_style))

    # Section 6: Leave-No-Trace & Continuous Guardrails
    story.append(Paragraph("6. Continuous Guardrails & Quality Assurance", h1_style))
    story.append(Paragraph("<b>1. Surgical Read Policy:</b> Never read full 300KB+ files. Use AST indexes (<code>cindex.py</code>) or targeted grep with offset/limit windows.", bullet_style))
    story.append(Paragraph("<b>2. Pre-Commit Verification:</b> Automated hooks trigger <code>python scripts/docs_audit.py score</code> to block commits if broken links or orphan docs are detected.", bullet_style))
    story.append(Paragraph("<b>3. Dual-Runtime Parity:</b> <code>CLAUDE.md</code> and <code>GEMINI.md</code> share the exact same state index, ensuring frictionless handoffs between Claude Code and Google Antigravity.", bullet_style))

    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"PDF successfully generated: {filename}")

if __name__ == '__main__':
    out = os.path.abspath("docs/scaffold.pdf")
    build_pdf(out)
