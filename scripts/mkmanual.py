#!/usr/bin/env python3
"""
mkmanual.py - the bm manuals as PDF: the content in one file, the look in another.

  scripts/mkmanual.py CONTENT.md -o OUT.pdf [--theme paper|night|blueprint|FILE.toml]
  scripts/mkmanual.py CONTENT.md --all-themes -o DIR       one PDF per theme (to choose among them)

CONTENT.md (docs/MANUALS.md) is a small Markdown: first lines "key: value" (title, accent = the
highlighted word of the title, kicker, subtitle, footer), then
  # chapter            ## section            ### sub-section
  paragraphs, "- " lists, "1. " steps, | tables |, ``` code ```, ![caption](shots/x.png)
  ::: readme | note | try | warn | copy  ...  :::     boxes
  [TO FILL: what is missing]                           a visible gap, never an invention
Inline: **bold**, *italic*, `code`. The content never names a colour, a font or a size: those
are the theme's (manuals/themes/*.toml). Needs reportlab.
"""
import argparse
import os
import re
import sys
import tomllib

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, Image, KeepTogether, PageBreak, PageTemplate,
                                Paragraph, Preformatted, Spacer, Table, TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
THEMES = os.path.join(ROOT, "manuals", "themes")


# ------------------------------------------------------------------ languages

LABELS = {
    "en": {"readme": "README", "try": "TRY IT", "warn": "BEWARE", "copy": "COPY AND PASTE", "note": "NOTE",
           "contents": "Contents", "open": "Open points", "none": "No open points.", "page": "page",
           "fill": "TO FILL", "gap_in": "in"},
    "it": {"readme": "LEGGIMI", "try": "PROVA", "warn": "ATTENZIONE", "copy": "DA COPIARE", "note": "NOTA",
           "contents": "Indice", "open": "Punti aperti", "none": "Nessun punto aperto.", "page": "pagina",
           "fill": "DA COMPLETARE", "gap_in": "in"},
}
GAP = re.compile(r"\[(?:TO FILL|DA COMPLETARE):([^\]]*)\]")


# ------------------------------------------------------------------ theme

class Theme:
    def __init__(self, path):
        with open(path, "rb") as f:
            t = tomllib.load(f)
        self.name = t["meta"]["name"]
        self.label = t["meta"]["label"]
        self.c = {k: colors.HexColor(v) for k, v in t["colors"].items()}
        self.f, self.s, self.l = t["fonts"], t["sizes"], t["layout"]
        s, f, c = self.s, self.f, self.c
        base = dict(fontName=f["body"], fontSize=s["body"], leading=s["body"] * 1.5, textColor=c["text"])
        self.p = {
            "body": ParagraphStyle("body", spaceAfter=5, **base),
            "h1": ParagraphStyle("h1", fontName=f["heading"], fontSize=s["h1"], leading=s["h1"] * 1.15,
                                 textColor=c["heading"], spaceBefore=4, spaceAfter=8),
            "h2": ParagraphStyle("h2", fontName=f["heading"], fontSize=s["h2"], leading=s["h2"] * 1.25,
                                 textColor=c["heading"], spaceBefore=12, spaceAfter=4),
            "h3": ParagraphStyle("h3", fontName=f["bold"], fontSize=s["h3"], leading=s["h3"] * 1.3,
                                 textColor=c["accent"] if self.name != "paper" else c["heading"], spaceBefore=8,
                                 spaceAfter=2),
            "li": ParagraphStyle("li", leftIndent=14, bulletIndent=2, spaceAfter=2, bulletFontName=f["bold"], bulletColor=c["accent"], **base),
            "step": ParagraphStyle("step", leftIndent=22, bulletIndent=0, spaceAfter=4, **base),
            "cap": ParagraphStyle("cap", fontName=f["italic"], fontSize=s["small"], leading=s["small"] * 1.4,
                                  textColor=c["muted"], spaceAfter=8, alignment=1),
            "cell": ParagraphStyle("cell", fontName=f["body"], fontSize=s["body"] - 1, leading=(s["body"] - 1) * 1.35,
                                   textColor=c["text"]),
            "cellh": ParagraphStyle("cellh", fontName=f["bold"], fontSize=s["body"] - 1, leading=(s["body"] - 1) * 1.35,
                                    textColor=c["table_head_text"]),
            "label": ParagraphStyle("label", fontName=f["bold"], fontSize=s["small"], leading=s["small"] * 1.4,
                                    textColor=c["accent"] if self.name != "paper" else c["accent"], spaceAfter=3),
            "code": ParagraphStyle("code", fontName=f["mono"], fontSize=s["code"], leading=s["code"] * 1.4,
                                   textColor=c["code_text"]),
        }


def load_theme(name):
    path = name if name.endswith(".toml") else os.path.join(THEMES, name + ".toml")
    if not os.path.exists(path):
        raise SystemExit(f"{path}: no such theme")
    return Theme(path)


# ------------------------------------------------------------------ inline text

def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def inline(th, t):
    t = esc(t)
    t = re.sub(r"`([^`]+)`", lambda m: f'<font name="{th.f["mono"]}" backColor="{th.c["code_bg"].hexval().replace("0x", "#")}">'
                                       f'&nbsp;{m.group(1)}&nbsp;</font>', t)
    t = re.sub(r"\[(TO FILL|DA COMPLETARE):([^\]]*)\]", lambda m: f'<font name="{th.f["bold"]}" '
               f'backColor="{th.c["accent_soft"].hexval().replace("0x", "#")}" '
               f'color="{th.c["warn"].hexval().replace("0x", "#")}">&nbsp;[{m.group(1)}:{m.group(2)}]&nbsp;</font>', t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", t)
    t = re.sub(r"(?<![\w*])\*([^*]+)\*(?![\w*])", r"<i>\1</i>", t)
    return t


# ------------------------------------------------------------------ flowables

class Box(Flowable):
    """a bordered box (with the theme's shadow) around other flowables, drawn together"""

    def __init__(self, th, inner, kind):
        super().__init__()
        self.th, self.inner, self.kind = th, inner, kind

    def wrap(self, aw, ah):
        pad = self.th.l["box_pad"]
        self.w = aw - self.th.l["shadow_offset"]
        self.hs = []
        h = pad * 2
        for fl in self.inner:
            _, fh = fl.wrap(self.w - 2 * pad, ah)
            self.hs.append(fh)
            h += fh
        self.h = h
        return aw, h + self.th.l["shadow_offset"]

    def draw(self):
        th, c, cv = self.th, self.th.c, self.canv
        so, pad, r = th.l["shadow_offset"], th.l["box_pad"], th.l["box_radius"]
        edge = {"warn": c["warn"], "try": c["accent"]}.get(self.kind, c["box_border"])
        if so:
            cv.setFillColor(c["shadow"])
            cv.roundRect(so, 0, self.w, self.h, r, stroke=0, fill=1)
        cv.setFillColor(c["accent_soft"] if self.kind == "try" else c["box_bg"])
        cv.setStrokeColor(edge)
        cv.setLineWidth(th.l["box_line"])
        cv.roundRect(0, so, self.w, self.h, r, stroke=1, fill=1)
        if self.kind in ("try", "warn"):                     # a bar on the left
            cv.setFillColor(edge)
            cv.rect(0, so, 4, self.h, stroke=0, fill=1)
        y = so + self.h - pad
        for fl, fh in zip(self.inner, self.hs):
            y -= fh
            fl.drawOn(cv, pad + (3 if self.kind in ("try", "warn") else 0), y)


class Head(Paragraph):
    """a heading that goes into the contents and the PDF's outline"""

    def __init__(self, text, style, level, plain):
        super().__init__(text, style)
        self.toc = (level, plain)


class Shot(Flowable):
    """a picture with a thin frame, scaled to the width"""

    def __init__(self, th, path, maxw):
        super().__init__()
        from reportlab.lib.utils import ImageReader
        iw, ih = ImageReader(path).getSize()
        self.path, self.th = path, th
        self.w = min(maxw, iw * 1.0)
        self.w = min(self.w, 130 * mm)
        self.h = self.w * ih / iw

    def wrap(self, aw, ah):
        return self.w, self.h

    def draw(self):
        self.canv.drawImage(self.path, 0, 0, self.w, self.h)
        self.canv.setStrokeColor(self.th.c["rule"])
        self.canv.setLineWidth(0.8)
        self.canv.rect(0, 0, self.w, self.h, stroke=1, fill=0)


# ------------------------------------------------------------------ content

def parse_head(lines):
    meta, i = {}, 0
    while i < len(lines) and re.match(r"^[a-z_]+: ", lines[i]):
        k, v = lines[i].split(": ", 1)
        meta[k] = v.strip()
        i += 1
    return meta, lines[i:]


def build_story(th, meta, lines, base_dir, width, lang):
    p, c, lab = th.p, th.c, LABELS[lang]
    story, i, para = [], 0, []

    def flush():
        if para:
            story.append(Paragraph(inline(th, " ".join(para)), p["body"]))
            para.clear()

    def box(kind, body_lines):
        inner = []
        if kind in ("try", "warn", "copy", "note"):
            inner.append(Paragraph(lab[kind], p["label"]))
        if kind == "copy":
            inner.append(Preformatted("\n".join(body_lines), p["code"]))
        else:
            for chunk in re.split(r"\n\s*\n", "\n".join(body_lines).strip()):
                if chunk.strip():
                    inner.append(Paragraph(inline(th, " ".join(chunk.split("\n"))),
                                           p["body"] if kind != "readme" else ParagraphStyle("r", parent=p["body"],
                                                                                              fontSize=th.s["body"] - 0.5)))
        if kind == "readme":
            inner.insert(0, Paragraph(lab["readme"], p["label"]))
        story.append(Spacer(1, 4))
        story.append(Box(th, inner, kind))
        story.append(Spacer(1, 10))

    first_chapter = True
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("::: "):
            flush()
            kind, body = ln[4:].strip(), []
            i += 1
            while i < len(lines) and lines[i].strip() != ":::":
                body.append(lines[i])
                i += 1
            box(kind, body)
        elif ln.startswith("```"):
            flush()
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                body.append(lines[i])
                i += 1
            t = Table([[Preformatted("\n".join(body), p["code"])]], colWidths=[width])
            t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), c["code_bg"]), ("LEFTPADDING", (0, 0), (-1, -1), 8),
                                   ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
                                   ("LINEBEFORE", (0, 0), (0, -1), 2.5, c["accent"])]))
            story.extend([Spacer(1, 2), t, Spacer(1, 8)])
        elif ln.startswith("# "):
            flush()
            if not first_chapter:
                story.append(PageBreak())
            first_chapter = False
            story.append(Head(inline(th, ln[2:]), p["h1"], 0, ln[2:]))
        elif ln.startswith("## "):
            flush()
            story.append(Head(inline(th, ln[3:]), p["h2"], 1, ln[3:]))
        elif ln.startswith("### "):
            flush()
            story.append(Paragraph(inline(th, ln[4:]), p["h3"]))
        elif ln.startswith("|"):
            flush()
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = [x.strip() for x in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-+:?", x) for x in cells):
                    rows.append(cells)
                i += 1
            i -= 1
            n = len(rows[0])
            data = [[Paragraph(inline(th, x), p["cellh"] if r == 0 else p["cell"]) for x in row]
                    for r, row in enumerate(rows)]
            t = Table(data, colWidths=[width / n] * n, repeatRows=1)
            style = [("BACKGROUND", (0, 0), (-1, 0), c["table_head_bg"]), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                     ("LEFTPADDING", (0, 0), (-1, -1), 7), ("RIGHTPADDING", (0, 0), (-1, -1), 7),
                     ("TOPPADDING", (0, 0), (-1, -1), 5), ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
                     ("LINEBELOW", (0, 0), (-1, -1), 0.4, c["rule"])]
            for r in range(2, len(rows), 2):
                style.append(("BACKGROUND", (0, r), (-1, r), c["table_stripe"]))
            t.setStyle(TableStyle(style))
            story.extend([Spacer(1, 3), t, Spacer(1, 8)])
        elif re.match(r"^- ", ln):
            flush()
            story.append(Paragraph(inline(th, ln[2:]), p["li"], bulletText="•"))
        elif re.match(r"^\d+\. ", ln):
            flush()
            n, rest = ln.split(". ", 1)
            story.append(Paragraph(inline(th, rest), p["step"], bulletText=f"{n}."))
        elif ln.startswith("!["):
            flush()
            m = re.match(r"!\[(.*)\]\((.*)\)", ln)
            path = os.path.join(base_dir, m.group(2))
            if not os.path.exists(path):
                story.append(Paragraph(inline(th, "[" + lab["fill"] + ": " + m.group(2) + "]"), p["body"]))
            else:
                story.append(KeepTogether([Spacer(1, 4), Shot(th, path, width), Spacer(1, 3),
                                           Paragraph(inline(th, m.group(1)), p["cap"])]))
        elif not ln.strip():
            flush()
        else:
            para.append(ln.strip())
        i += 1
    flush()
    return story


# ------------------------------------------------------------------ pages

def accent_title(th, meta):
    t, a = meta.get("title", ""), meta.get("accent", "")
    if a and a in t:
        col = th.c["accent"].hexval().replace("0x", "#")
        t = esc(t).replace(esc(a), f'<font name="{th.f["accent"]}" color="{col}">{esc(a)}</font>')
        return t
    return esc(t)


def draw_cover(cv, th, meta, pw, ph):
    c, m = th.c, th.l["margin_mm"] * mm
    cv.setFillColor(c["cover_bg"])
    cv.rect(0, 0, pw, ph, stroke=0, fill=1)
    band = th.l["cover_style"] == "band"
    if band:
        cv.setFillColor(c["accent"])
        cv.rect(0, ph - 38 * mm, pw, 38 * mm, stroke=0, fill=1)
        cv.setFillColor(c["accent_text"])
        cv.setFont(th.f["mono_bold"], 11)
        cv.drawString(m, ph - 22 * mm, "bm  /  BareMetal")
    else:
        cv.setFillColor(c["accent"])
        cv.rect(m, ph - 38 * mm, 14 * mm, 14 * mm, stroke=0, fill=1)       # a pixel, the bm mark
    kick = meta.get("kicker", "")
    y = ph - 95 * mm
    cv.setFillColor(c["accent"])
    cv.setFont(th.f["bold"], 10)
    cv.drawString(m, y + 28 * mm, kick.upper())
    st = ParagraphStyle("ct", fontName=th.f["heading"], fontSize=th.s["cover_title"], leading=th.s["cover_title"] * 1.1,
                        textColor=c["cover_text"])
    para = Paragraph(accent_title(th, meta), st)
    w, h = para.wrap(pw - 2 * m, ph)
    para.drawOn(cv, m, y - h + 22 * mm)
    ss = ParagraphStyle("cs", fontName=th.f["body"], fontSize=th.s["cover_sub"], leading=th.s["cover_sub"] * 1.45,
                        textColor=c["muted"])
    sp = Paragraph(esc(meta.get("subtitle", "")), ss)
    sw, sh = sp.wrap(pw - 2 * m - 30 * mm, ph)
    sp.drawOn(cv, m, y - h + 22 * mm - sh - 8 * mm)
    cv.setFillColor(c["muted"])
    cv.setFont(th.f["body"], 8)
    cv.drawString(m, 14 * mm, meta.get("footer", "").upper())


def draw_page(cv, doc, th, meta):
    c, m = th.c, th.l["margin_mm"] * mm
    pw, ph = A4
    cv.saveState()
    cv.setFillColor(c["page"])
    cv.rect(0, 0, pw, ph, stroke=0, fill=1)
    if th.l["header"]:
        cv.setStrokeColor(c["rule"])
        cv.setLineWidth(0.6)
        cv.line(m, ph - 14 * mm, pw - m, ph - 14 * mm)
        cv.setFillColor(c["muted"])
        cv.setFont(th.f["mono"] if th.name == "night" else th.f["body"], 7.5)
        cv.drawString(m, ph - 11 * mm, meta.get("title", "").upper())
    cv.setFillColor(c["muted"])
    cv.setFont(th.f["body"], 7.5)
    cv.drawString(m, 11 * mm, meta.get("footer", "").upper())
    cv.setFillColor(c["accent"] if th.name != "blueprint" else c["muted"])
    cv.setFont(th.f["bold"], 8)
    cv.drawRightString(pw - m, 11 * mm, str(doc.page))
    cv.restoreState()


class Doc(BaseDocTemplate):
    def beforeDocument(self):
        self.hseq = 0                       # (every pass of multiBuild numbers the headings again)

    def afterFlowable(self, fl):
        if isinstance(fl, Head):
            level, text = fl.toc
            key = f"h{self.hseq}"
            self.hseq += 1
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(text, key, level)
            self.notify("TOCEntry", (level, text, self.page, key))


def gaps_of(lines):
    """[(chapter, text)] of the gaps marked in the content"""
    out, chap = [], ""
    for ln in lines:
        if ln.startswith("# "):
            chap = ln[2:].strip()
        for m in GAP.finditer(ln):
            out.append((chap, m.group(1).strip()))
    return out


def build(content_path, out, th):
    from reportlab.platypus import NextPageTemplate
    from reportlab.platypus.tableofcontents import TableOfContents
    lines = open(content_path, encoding="utf-8").read().split("\n")
    meta, body = parse_head(lines)
    lang = meta.get("lang", "en")
    if lang not in LABELS:
        raise SystemExit(f"{content_path}: lang {lang} is not one of {', '.join(LABELS)}")
    lab = LABELS[lang]
    pw, ph = A4
    m = th.l["margin_mm"] * mm
    width = pw - 2 * m
    frame = Frame(m, 20 * mm, width, ph - 20 * mm - 22 * mm, id="f", leftPadding=0, rightPadding=0, topPadding=0,
                  bottomPadding=0)
    doc = Doc(out, pagesize=A4, title=meta.get("title", ""), author="bm", subject=f"theme {th.name}, {lang}")
    doc.hseq = 0
    cover = PageTemplate("cover", [Frame(0, 0, 1, 1)], onPage=lambda cv, d: draw_cover(cv, th, meta, pw, ph))
    page = PageTemplate("page", [frame], onPage=lambda cv, d: draw_page(cv, d, th, meta))
    doc.addPageTemplates([cover, page])
    p = th.p
    toc = TableOfContents()
    toc.levelStyles = [
        ParagraphStyle("t0", parent=p["body"], fontName=th.f["bold"], leftIndent=0, spaceBefore=6, spaceAfter=1),
        ParagraphStyle("t1", parent=p["body"], leftIndent=14, textColor=th.c["muted"], spaceAfter=0),
    ]
    toc.dotsMinLevel = 0
    story = [NextPageTemplate("page"), PageBreak(),
             Paragraph(esc(lab["contents"]), p["h1"]), toc, PageBreak()]
    story += build_story(th, meta, body, os.path.join(os.path.dirname(os.path.abspath(content_path)), "..", ".."),
                         width, lang)
    gaps = gaps_of(body)
    story += [PageBreak(), Head(esc(lab["open"]), p["h1"], 0, lab["open"])]
    if not gaps:
        story.append(Paragraph(esc(lab["none"]), p["body"]))
    for chap, text in gaps:
        story.append(Paragraph(inline(th, f"**{chap}**: {text}" if chap else text), p["li"], bulletText="\u2022"))
    doc.multiBuild(story)
    return len(gaps)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("content")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--theme", default="paper", help="paper (the chosen one), night, blueprint or a .toml file")
    ap.add_argument("--all-themes", action="store_true")
    a = ap.parse_args()
    names = sorted(f[:-5] for f in os.listdir(THEMES) if f.endswith(".toml")) if a.all_themes else [a.theme]
    stem = os.path.splitext(os.path.basename(a.content))[0]
    for n in names:
        th = load_theme(n)
        out = os.path.join(a.output, f"{stem}.{th.name}.pdf") if a.all_themes else a.output
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        n = build(a.content, out, th)
        print(f"{out}  ({th.label}; {n} open points)")


if __name__ == "__main__":
    sys.exit(main())
