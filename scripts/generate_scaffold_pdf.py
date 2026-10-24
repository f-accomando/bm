# generate_scaffold_pdf.py
# Builds the scaffold handbook (handbook + copy-paste starter kit for AI scaffolding):
#   python3 scripts/generate_scaffold_pdf.py [en|it]     (run from the repo root; default en)
#   en -> docs/scaffold.pdf      it -> docs/scaffold-IT.pdf
# The helpers are here. The text of each language is in scripts/scaffold_pdf/<lang>.py.
# In Italian, prose is Italian; names of files, folders, agents, skills and grep headings stay English.
import os
import sys
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Preformatted,
    PageBreak, HRFlowable, KeepTogether,
)
from reportlab.lib import colors
from reportlab.pdfgen import canvas

LANGS = {
    "en": dict(
        header="AI Scaffolding & Agent Architecture Guide — Claude Code & Google Antigravity",
        footer="Context-Aware Scaffolding Handbook & Starter Kit",
        page="Page {n} of {total}",
        warn="WARNING: code line too wide ({n} chars): {text}...",
        out="docs/scaffold.pdf",
        title="AI Scaffolding & Agent Architecture Guide",
        done="PDF successfully generated: {out}",
    ),
    "it": dict(
        header="Guida allo Scaffolding AI e all'Architettura degli Agenti — Claude Code e Google Antigravity",
        footer="Manuale e Starter Kit per lo Scaffolding Context-Aware",
        page="Pagina {n} di {total}",
        warn="ATTENZIONE: riga di codice troppo larga ({n} caratteri): {text}...",
        out="docs/scaffold-IT.pdf",
        title="Guida allo Scaffolding AI e all'Architettura degli Agenti",
        done="PDF generato: {out}",
    ),
}
L = LANGS["en"]        # the language in use; main() sets it before the content runs

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
            self.drawString(54, 755, L["header"])
            self.setStrokeColor(BORDER)
            self.setLineWidth(0.5)
            self.line(54, 747, letter[0] - 54, 747)
        self.drawRightString(letter[0] - 54, 36, L["page"].format(n=self._pageNumber, total=total))
        self.drawString(54, 36, L["footer"])
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
    """Inline code."""
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
    """Code block that can split across pages (one table row per chunk of lines)."""
    lines = text.strip("\n").split("\n")
    for ln in lines:
        if len(ln) > 122:
            print(L["warn"].format(n=len(ln), text=ln[:60]))
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

def main(argv):
    global L
    lang = argv[1] if len(argv) > 1 else "en"
    if len(argv) > 2 or lang not in LANGS:
        sys.exit("usage: generate_scaffold_pdf.py [%s]" % "|".join(LANGS))
    L = LANGS[lang]
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scaffold_pdf", lang + ".py")
    with open(path, encoding="utf-8") as f:
        exec(compile(f.read(), path, "exec"), globals())
    doc = SimpleDocTemplate(os.path.abspath(L["out"]), pagesize=letter,
                            leftMargin=54, rightMargin=54, topMargin=54, bottomMargin=54,
                            title=L["title"], author="bm project")
    doc.build(story, canvasmaker=NumberedCanvas)
    print(L["done"].format(out=L["out"]))


if __name__ == "__main__":
    main(sys.argv)
