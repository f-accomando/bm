"""bm Write's files after make test-write: the .BMD saved, the exports (PDF,
HTML, Markdown, text) and the logs of the two runs.
    check.py DOCS_DIR LOG_DIR"""
import os
import re
import shutil
import subprocess
import sys

docs, logs = sys.argv[1], sys.argv[2]
fails = checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("write: FAIL", what)


def read(name, mode="r"):
    with open(os.path.join(docs, name), mode, **({"encoding": "utf-8"} if mode == "r" else {})) as f:
        return f.read()


bmd = read("LETTERA.BMD")
check(bmd.startswith("bmwrite 1\ntitle=Una lettera\n\n"), "BMD: the header")
check("title center |Una lettera\n" in bmd, "BMD: the title, centred")
check("body left 31:9:1,47:12:4|Questo testo" in bmd, "BMD: bold and underlined letters")
check("h1 left |Un titoletto" in bmd and "bullet left |uno" in bmd and "number left |primo" in bmd,
      "BMD: heading, bullets, numbers")
check(re.search(r"^body justify \S*199:7:2\S*\|Un paragrafo", bmd, re.M) is not None, "BMD: justified, an italic word")
check("è la fine" in bmd, "BMD: UTF-8 (è)")
check(len(bmd.strip().split("\n")) - 3 == 38, "BMD: 38 paragraphs")

md = read("LETTERA.MD")
for want in ("# Una lettera", "**grassetto**", "<u>sottolineata</u>", "## Un titoletto", "- uno\n- due\n- tre",
             "> Una citazione", "1. primo\n2. secondo\n3. terzo", "*Corsivo*", "è la fine", "Paragrafo 26:"):
    check(want in md, "Markdown: " + repr(want))

html = read("LETTERA.HTM")
for want in ('<meta charset="utf-8">', '<h1 class="title">Una lettera</h1>', "<b>grassetto</b>",
             "<u>sottolineata</u>", "<h1>Un titoletto</h1>", "<ul>\n<li>uno</li>", "<blockquote>Una citazione",
             "<ol>\n<li>primo</li>", '<p class="justify">', "<i>Corsivo</i>", "è la fine", "</body></html>"):
    check(want in html, "HTML: " + repr(want))

txt = read("LETTERA.TXT")
check(txt.startswith("Una lettera\n===========\n\n") and "- uno\n- due\n- tre\n" in txt, "text: title, list")

pdf = read("LETTERA.PDF", "rb")
check(pdf.startswith(b"%PDF-1.4\n") and pdf.rstrip().endswith(b"%%EOF"), "PDF: header and end")
m = re.search(rb"startxref\n(\d+)\n%%EOF", pdf)
check(m is not None and pdf[int(m.group(1)):].startswith(b"xref\n"), "PDF: startxref points to the xref")
if m:
    xref = pdf[int(m.group(1)):].split(b"trailer")[0].split(b"\n")
    n = int(xref[1].split()[1])
    good = all(pdf[int(xref[2 + i][:10]):].startswith(b"%d 0 obj" % i) for i in range(1, n))
    check(good and all(len(xref[2 + i]) == 19 for i in range(n)), "PDF: every object where the xref says")
pages = re.search(rb"/Type /Pages /Kids \[[^\]]*\] /Count (\d+)", pdf)
check(pages is not None and int(pages.group(1)) == 3, "PDF: 3 pages (%s)" % (pages and pages.group(1)))
check(b"/F2 29.38 Tf" in pdf and b"(Una lettera) Tj" in pdf, "PDF: the title in Courier-Bold, 29.4 pt")
check(b"/F3 11.02 Tf" in pdf and b"(Corsivo) Tj" in pdf, "PDF: an italic word in Courier-Oblique")
check(b"(\\350)" in pdf or b" \\350" in pdf, "PDF: WinAnsi (è as \\350)")
if shutil.which("pdftotext"):
    text = subprocess.run(["pdftotext", "-layout", os.path.join(docs, "LETTERA.PDF"), "-"],
                          capture_output=True, text=True).stdout
    check("Una lettera" in text and "Paragrafo 26:" in text and "la fine." in text, "PDF: pdftotext reads it")

run1 = open(os.path.join(logs, "run1.log")).read()
run2 = open(os.path.join(logs, "run2.log")).read()
for want in ("write: saved /docs/LETTERA.BMD (7 paragraphs)", "write: exported /docs/LETTERA.HTM",
             "write: saved /docs/LETTERA.BMD (38 paragraphs)", "write: exported /docs/LETTERA.PDF"):
    check(want in run1, "run 1: " + want)
check("write: opened /docs/LETTERA.BMD (38 paragraphs)" in run2, "run 2: the session opens the document")
# (the empty paragraph at the end is not in the Markdown)
check("write: opened /docs/LETTERA.MD (37 paragraphs)" in run2, "run 2: the Markdown comes back the same")
print("write: %d/%d checks passed" % (checks - fails, checks))
sys.exit(1 if fails else 0)
