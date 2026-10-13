# generate_scaffold_pdf.py
# Builds docs/scaffold.pdf: handbook + copy-paste starter kit for AI scaffolding.
# Run from the repo root:  python3 scripts/generate_scaffold_pdf.py
import os
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Preformatted,
    PageBreak, HRFlowable, KeepTogether,
)
from reportlab.lib import colors
from reportlab.pdfgen import canvas

HEADER = "AI Scaffolding & Agent Architecture Guide — Claude Code & Google Antigravity"
FOOTER = "Context-Aware Scaffolding Handbook & Starter Kit"

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
        self.drawRightString(letter[0] - 54, 36, f"Page {self._pageNumber} of {total}")
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
            print(f"WARNING: code line too wide ({len(ln)} chars): {ln[:60]}...")
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


# ============================================================ TITLE
story.append(Paragraph("Context-Aware AI Scaffolding &amp; Agent Architecture", S_TITLE))
story.append(Paragraph(
    "Handbook and starter kit for Claude Code &amp; Google Antigravity / Gemini: token economics, readiness gates, "
    "work-order routing, and copy-paste templates to bootstrap any new project. Reference implementation: "
    "github.com/f-accomando/bm (agents prefixed <i>bm-</i>).", S_SUB))
story.append(HRFlowable(width="100%", thickness=1.5, color=PRIMARY, spaceBefore=0, spaceAfter=4))

# ============================================================ PART A
part("PART A — HOW IT WORKS")

h1("1. The Core Principle: Separate Decision-Making from Execution")
p("Frontier models (Claude Opus, Gemini Pro) have a cost bottleneck: <b>output token asymmetry</b>. High-tier models cost "
  "roughly 20x to 40x more per output token than entry-tier models (Haiku, Flash-Lite), so generating raw implementation "
  "code with a top model burns session quotas fast. The fix is structural:")
b("<b>High-tier models (Opus / Gemini Pro)</b> produce <i>few output tokens</i>: they plan, design, settle trade-offs and emit structured Work Orders.")
b("<b>Low-tier models (Haiku / Gemini Flash)</b> produce <i>many output tokens</i>: they write and refactor code strictly driven by deterministic Work Orders.")
b("<b>Readiness routing:</b> a task is never dispatched by its perceived size or complexity, only by <b>how well specified</b> it already is.")

h1("2. Session Workflow: Zero Token Waste")
p("Loading whole documents destroys the context budget and breeds hallucinations. The architecture uses hierarchical, "
  "on-demand, surgical retrieval:")
b("<b>Root guardian</b> (" + c("CLAUDE.md") + " / " + c("GEMINI.md") + "): loaded at start, under 60 lines. Global invariants, branching and license rules, the lookup table, agent routing.")
b("<b>Index map</b> (" + c("docs/progress.md") + "): the single index. Never read whole; it only routes to the right area sheet.")
b("<b>Targeted extraction</b> with grep and offset/limit reads: about 100 tokens instead of 10,000.")
table([
    ["Phase", "What the AI does", "Files involved / output"],
    ["1. Boot", "Loads base rules and quick commands.", c("CLAUDE.md") + " / " + c("GEMINI.md") + " (about 50 lines)"],
    ["2. Routing", "Identifies the target area if not obvious.", c("docs/progress.md") + " (lookup only)"],
    ["3. Lookup", "Extracts open tasks and constraints surgically.", c("grep -n -A 15 '^## Open work' docs/progress/<area>.md")],
    ["4. Execution", "Targeted inspection (symbol index) and selective tests.", "Specific sources, " + c("make test-<area>")],
    ["5. Closing", "Leave-no-trace: update state, run the audit.", c("docs/progress/<area>.md") + ", " + c("docs_audit.py")],
], [62, 210, 232])

h1("3. The Agent Architecture: Six Roles")
table([
    ["Agent", "Claude tier", "Antigravity tier", "Toolset", "Mandate and boundaries"],
    ["<b>planner</b>", "Opus medium", "Gemini Pro", "Read-only (+ read-only shell)", "Turns vague or large tasks into verified Work Orders. <b>Never writes code.</b> Always runs before deep."],
    ["<b>coder</b>", "Haiku high", "Gemini Flash", "Full (edit, write, exec)", "Writes code from a locked Work Order. <b>No design choices, no ambiguity resolution.</b>"],
    ["<b>mechanic</b>", "Haiku low", "Gemini Flash-Lite", "Full (atomic edits)", "Zero-risk tasks: symbol renames, formatting, ticking progress/roadmap items."],
    ["<b>light</b>", "Sonnet medium", "Gemini Flash", "Full (read, write, exec)", "Focused fixes (1-2 files) needing judgment; documentation verified against the real code."],
    ["<b>deep</b>", "Opus xhigh", "Gemini Pro", "Full (isolated repro)", "Strict gate: one isolated bug with a repro, or one hard architectural decision. Rejects generic tasks."],
    ["<b>research</b>", "Haiku low", "Native research subagent", "Read-only (+ read-only shell)", "Wide exploration across many files; returns a 5-10 line summary. Never edits."],
], [52, 62, 72, 88, 230])

h1("4. Readiness Gate, Status Protocol and Escalation")
p("Before a task goes to an execution agent (mechanic, coder, light), the router answers five binary questions:")
n(1, "<b>Target files and symbols:</b> are exact paths and function signatures named?")
n(2, "<b>Diff determinism:</b> is the change specified so strictly that two developers would write the same diff?")
n(3, "<b>Verifiable command:</b> is there an automated check (unit test, build, lint, audit script)?")
n(4, "<b>Constrained scope:</b> does it touch 3 files or fewer?")
n(5, "<b>Zero pending decisions:</b> is it free of unresolved design choices or dependencies?")
p("<b>Rule:</b> 5 of 5 YES means the task is ready; any NO blocks execution and the request goes to <b>planner</b>, which makes it ready or splits it "
  "into atomic Work Orders. Never send an unprepared task straight to <b>deep</b>: it only accepts a symptom, an exact reproduction, a suspect area, and the attempts "
  "already made, and otherwise answers " + c("REJECTED") + " and bounces back to planner. Every agent ends with exactly one status:")
table([
    ["Status", "Meaning", "Next step"],
    [c("DONE"), "Verification command passed; short diff summary.", "Close the task (section 19, rule R9)."],
    [c("BLOCKED: <reason>"), "Ambiguous spec, mismatched signature or missing decision.", "Back to planner."],
    [c("FAILED x2"), "Verification still fails after two fix attempts.", "Escalate one tier (coder to light, light to deep)."],
], [100, 250, 154])

h1("5. Standard Work Order Template")
code("""### WORK ORDER: [WO-ID]
- Objective: [one concise sentence defining the outcome]
- Target Files & Symbols: [explicit paths and functions, e.g. src/module/file.c:init_thing()]
- Required Mutation: [unambiguous diff specification leaving zero design latitude]
- Verification Command: [deterministic command, e.g. make test-<area> or pytest -k <name>]
- Expected Result: [exit code 0 and the exact expected assertion/output]
- Designated Agent: [mechanic | coder | light]""")

h1("6. Three-Tier Repository Structure")
p("To prevent context rot, documentation is split across three lifecycles:")
b("<b>Root guardians</b> (" + c("CLAUDE.md") + ", " + c("GEMINI.md") + "): under 60 lines. Only global invariants, build shortcuts, branching rules, agent routing. No release history.")
b("<b>Living progress</b> (" + c("docs/progress/<area>.md") + "): active sheets of 80-120 lines: current state, open work, binding local rules.")
b("<b>Deep RFCs</b> (" + c("docs/design/<topic>.md") + "): speculative designs and long-term proposals, kept out of the live index so they never inflate token cost.")

# ============================================================ PART B
part("PART B — STARTER KIT FOR A NEW PROJECT (COPY, FILL THE &lt;PLACEHOLDERS&gt;, COMMIT)")

h1("7. Bootstrap Checklist and File Tree")
n(1, "Create the tree below and copy the templates of sections 8-14 into it.")
n(2, "Fill " + c("CLAUDE.md") + ": stack, test commands, global invariants. Keep it under 60 lines.")
n(3, "Copy the six agents (section 10); adjust the " + c("tools") + " list and the verification commands to your stack.")
n(4, "Copy the three skills (section 11) and " + c(".claude/settings.json") + " (section 12).")
n(5, "Create " + c("docs/progress.md") + " and one first area sheet (section 13); add " + c("scripts/docs_audit.py") + " (section 14).")
n(6, "Wire the audit into the pre-commit hook, the Makefile and CI (section 15). Run it once and save the baseline score.")
n(7, "Create " + c("GEMINI.md") + " if you also use Antigravity (section 9). Commit, then hand the first real task to planner to test the gate.")
code("""<project>/
|-- CLAUDE.md                    root guardian, under 60 lines (loaded every session)
|-- GEMINI.md                    same sections 1,2,4,5 + Antigravity routing table
|-- AGENTS.md                    optional: roles, gate, Work Order format (shared governance)
|-- .claude/
|   |-- settings.json            skill overrides, permissions allow/deny
|   |-- agents/                  planner, coder, mechanic, light, deep, research (.md)
|   `-- skills/
|       |-- work-order/SKILL.md
|       |-- docs-map/SKILL.md
|       `-- docs-loop/SKILL.md
|-- .agents/skills/              mirror of .claude/skills for Antigravity
|-- .githooks/pre-commit         versioned hook (git config core.hooksPath .githooks)
|-- .github/workflows/ci.yml     includes the docs-audit step
|-- docs/
|   |-- progress.md              the single index
|   |-- progress/<area>.md       living sheets, 80-120 lines each
|   |-- design/<topic>.md        RFCs
|   `-- ROADMAP.md               optional milestones
`-- scripts/
    |-- docs_audit.py            score: tokens + penalties (section 14)
    `-- cindex.py                optional symbol/banner index for big source files""")

h1("8. Template: CLAUDE.md (root guardian)")
p("Claude Code concatenates every " + c("CLAUDE.md") + " it finds from the working directory upward and loads it in full each session. "
  "The official guidance is to stay under 200 lines; this kit is deliberately stricter (60) because the file is paid for on every turn. "
  "Treat it as context, not enforcement: anything that must <i>never</i> happen belongs in a permission rule or a hook (section 12).")
label("CLAUDE.md")
code("""# <Project> - Session Notes (Claude & Gemini)
Repo <org>/<repo>, main branch `main`. Stack: <languages, framework, OS/targets>.
Single state index: docs/progress.md. Never read a file whole: grep first, then Read with offset/limit.

## 1. Targeted lookup (zero token waste)
| Need                          | Command                                                        |
|-------------------------------|----------------------------------------------------------------|
| What is left in an area       | grep -n -A 15 '^## Open work' docs/progress/<area>.md         |
| Binding rules of an area      | grep -n -A 20 '^## Rules' docs/progress/<area>.md              |
| Find a symbol in a big file   | python scripts/cindex.py <file> <symbol>  (then Read offset)   |
| Design background             | docs/design/<topic>.md (only when asked)                       |

## 2. Rules of engagement
- Branch: propose a short name (feat/<name>) and ask before editing; start from up-to-date main.
- Tests: run only the tests of the touched area (make test-<area>); never the whole suite.
- No secrets (keys, tokens, .env) in the repo. Commits only when the user asks.
- <License / dependency constraints, e.g. no GPL code copied into the sources>.

## 3. Subagent routing (definitions in .claude/agents/)
Route by how well a task is specified, not by its size.
| Agent     | Model / effort        | Does                                         | Never                  |
|-----------|-----------------------|----------------------------------------------|------------------------|
| planner   | opus medium, read-only| vague/large task -> Work Orders              | writes code            |
| coder     | haiku high            | code from a locked Work Order                | design decisions       |
| mechanic  | haiku low             | renames, format, progress ticks              | any decision           |
| light     | sonnet medium         | 1-2 file fixes, docs checked against code    | long or vague work     |
| deep      | opus xhigh            | one isolated bug with repro / one decision   | whole features         |
| research  | haiku low, read-only  | wide search, 5-10 line summary               | edit files             |
Readiness gate (5/5 YES for coder, mechanic, light): files+symbols named; diff deterministic; verify
command exists; <= 3 files; no pending decisions. Any NO -> planner. Never straight to deep.
Every agent ends with: DONE | BLOCKED: <reason> | FAILED x2 (escalate one tier).

## 4. Global invariants
- <Invariant 1, e.g. the public API of module X is frozen>
- Area-specific rules live in docs/progress/<area>.md under "## Rules (do not break)".

## 5. Closing a task (leave no trace)
1. Update "## Open work" in the area sheet (remove or tick finished items).
2. New constraint or pattern? Add it under "## Rules (do not break)".
3. Run python scripts/docs_audit.py score: no broken links, no orphans, score not higher than baseline.""", rows=10)

h1("9. Templates: GEMINI.md and AGENTS.md (cross-agent parity)")
p("Claude Code reads " + c("AGENTS.md") + " on its own <b>only when no CLAUDE.md exists</b> in the working directory or above; with both present it loads CLAUDE.md alone, "
  "unless CLAUDE.md imports it with " + c("@AGENTS.md") + " (imports are expanded at launch, so they do not save tokens). Do not assume AGENTS.md is auto-loaded next to a CLAUDE.md. "
  "Parity rule: " + c("GEMINI.md") + " repeats sections 1, 2, 4 and 5 of CLAUDE.md verbatim and swaps only the routing table for the Antigravity mapping below.")
label("GEMINI.md  (section 3 replaced; all other sections identical to CLAUDE.md)")
code("""# <Project> - Session Notes (Google Antigravity / Gemini)
...sections 1 and 2 copied verbatim from CLAUDE.md...

## 3. Subagent routing (Antigravity invoke_subagent)
| Role      | Model          | Toolset                 | Does / never                              |
|-----------|----------------|-------------------------|-------------------------------------------|
| planner   | "pro"          | read-only               | Work Orders / never writes code           |
| coder     | "flash"        | full                    | code from Work Order / never designs      |
| mechanic  | "flash_lite"   | full, atomic edits      | zero-risk edits / never decides           |
| light     | "flash"        | full                    | 1-2 file fixes, verified docs             |
| deep      | "pro"          | full                    | one isolated bug with repro               |
| research  | native         | read-only               | wide search, short summary                |
Readiness gate, status line and escalation: identical to CLAUDE.md section 3.

...sections 4 and 5 copied verbatim from CLAUDE.md...""", rows=10)
label("AGENTS.md  (optional shared governance; link it, do not duplicate it elsewhere)")
code("""# <Project> - Agent Governance
1. Principle: top models decide (few tokens), cheap models execute (many tokens). Route by readiness.
2. Roles table: planner, coder, mechanic, light, deep, research (model, toolset, mandate, limits).
3. Readiness gate (5 questions) and the rule: any NO -> planner.
4. deep entry gate: symptom + exact repro + suspect area + attempts already made, else REJECTED.
5. Closing statuses: DONE | BLOCKED: <reason> | FAILED x2.
6. Work Order format (section 5 of this guide).""", rows=10)

h1("10. Templates: the six agents  (.claude/agents/&lt;name&gt;.md)")
p("Project agents live in " + c(".claude/agents/") + " and override user-level agents of the same name. Required frontmatter: " + c("name") + ", " + c("description") +
  " (the description is what the router reads, so state <i>when</i> to use the agent). Useful optional fields: " + c("tools") + " (allow-list; omit to inherit all), " +
  c("disallowedTools") + ", " + c("model") + " (" + c("haiku") + ", " + c("sonnet") + ", " + c("opus") + ", a full model ID, or " + c("inherit") + "), " + c("effort") +
  " (" + c("low") + " to " + c("max") + "), " + c("maxTurns") + ", " + c("skills") + ", " + c("memory") + ". Unknown fields are silently ignored. "
  "<b>Important:</b> " + c("tools") + " is an allow-list, so an agent told to run a verification command must list " + c("Bash") + ".")
label(".claude/agents/planner.md")
code("""---
name: planner
description: Use for vague, large or under-specified tasks: produces Work Orders, never code. Call before deep.
model: opus
effort: medium
tools: Read, Grep, Glob, Bash
---
You are the planning agent. Your output is short and decisive: plans and decisions, never code.

## Mandate
1. Turn vague or large requests into atomic, self-contained Work Orders.
2. Name the exact files and symbols first (grep, symbol index, Read with offset/limit).
3. A requirement is missing? Do not guess: list it as an explicit question for the user.

## Hard limits
- No implementation code and no long diffs. Bash only for read-only commands (grep, git log/diff, cindex).
- Compact output: one Work Order block per atomic step, in the Work Order format of AGENTS.md.""", rows=9)
label(".claude/agents/coder.md")
code("""---
name: coder
description: Writes code only from a precise, complete Work Order. No design decisions, no guessing.
model: haiku
effort: high
tools: Read, Edit, Write, Grep, Glob, Bash
---
You write code from locked Work Orders and change nothing else.

## Rules
1. No open design: a choice left open or a missing parameter means stop and reply
   `BLOCKED: decision needed - <detail>`. Never invent a solution.
2. Minimal diff: touch only the files and symbols named in the order. No side refactors.
3. Verify: run the order's verification command before finishing.

## Closing status (exactly one)
- DONE: verification passed; 2-3 line diff summary.
- BLOCKED: <reason>: ambiguous spec or missing decision; back to planner.
- FAILED x2: still failing after two fix attempts; report the error, escalate to light or deep.""", rows=9)
label(".claude/agents/mechanic.md")
code("""---
name: mechanic
description: Fully specified zero-risk edits: renames, formatting, templates, progress ticks. No decisions.
model: haiku
effort: low
tools: Read, Edit, Write, Grep, Glob, Bash
---
You execute zero-risk, fully specified, purely mechanical tasks.

## Rules
1. Zero decisions: do exactly what was asked. No business-logic changes, no invented tasks, no reprioritising.
2. Integrity: keep the existing structure of progress docs; after a rename run the build/lint command
   and make sure no syntax error was introduced.
3. Close with DONE (1-2 line summary) or BLOCKED (instruction ambiguous or exact coordinates missing).""", rows=9)
label(".claude/agents/light.md")
code("""---
name: light
description: Small tasks needing judgment: code questions, 1-2 file fixes, docs verified against real code.
model: sonnet
effort: medium
tools: Read, Edit, Write, Grep, Glob, Bash
---
You handle focused tasks and verified documentation.

## Code
- Smallest change that fixes the problem; no wide refactors.
- Always run the specific test or verification command for the change.
- Fix still failing after two attempts: return `FAILED x2` and escalate to deep.

## Documentation
- Zero hallucination: read the real code before documenting an API or a behaviour.
- Match the style and format of the existing docs; do not touch sources except requested comments.

## Closing: DONE | BLOCKED (needs an architectural decision) | FAILED x2""", rows=9)
label(".claude/agents/deep.md")
code("""---
name: deep
description: ONE isolated hard problem with a sure repro (or one critical design decision). Never whole features.
model: opus
effort: xhigh
tools: Read, Edit, Write, Grep, Glob, Bash
---
You are the highest-depth agent: costly, so you act only on isolated problems with objective evidence.

## Entry gate - accept ONLY if the task contains all four
1. Clear symptom.  2. Exact deterministic reproduction (command, input or failing test).
3. Suspect area (file, module, subsystem).  4. Attempts already made and ruled out.
Otherwise reply: `REJECTED: back to planner - missing entry requirements (repro / isolated problem)`.

## Mandate
- Root-cause analysis BEFORE touching any line; then a minimal, targeted fix.
- Large implementation or rewrites go back to coder or light through a Work Order.
- Explain the factual evidence for the cause before applying the fix; never repeat failed attempts.
- Run the reproduction to certify the fix; close with DONE and a report (symptom, cause, minimal fix).""", rows=9)
label(".claude/agents/research.md")
code("""---
name: research
description: Read-only wide exploration of many files; answers with compact summaries. Never edits.
model: haiku
effort: low
tools: Read, Grep, Glob, Bash
---
You are the read-only research subagent.

## Rules (zero token waste)
1. Read-only: no edits, no new files. Bash only for read-only commands (grep, git log, cindex).
2. Navigate efficiently: grep -n and the symbol index for exact positions; never read huge files whole,
   use Read with small offset and limit.
3. Ultra-compact answer: no pasted code blocks; at most 5-10 bullets giving file:line paths,
   function signatures or data layouts, and a one-line conclusion for the calling agent.""", rows=9)

h1("11. Templates: three skills  (.claude/skills/&lt;name&gt;/SKILL.md)")
p("A skill is a folder with a " + c("SKILL.md") + ". Only its <b>description</b> sits in context permanently; the body loads when the skill is invoked (progressive disclosure), "
  "so keep descriptions to one or two sentences with the trigger first, and bodies short (the official ceiling is 500 lines; this kit uses under 60). "
  "Claude Code reads " + c(".claude/skills/") + "; Antigravity reads " + c(".agents/skills/") + ", so mirror the folder if you use both.")
label(".claude/skills/work-order/SKILL.md")
code("""---
name: work-order
description: Builds Work Orders and applies the readiness gate before delegating to coder, mechanic or light.
---
# Work Order and readiness gate
Answer the 5 questions: (1) files and symbols named? (2) diff deterministic? (3) verify command exists?
(4) 3 files or fewer? (5) no pending decisions?
- 5/5 YES: task READY -> choose mechanic, coder or light.
- any NO: NOT READY -> planner investigates and emits one or more Work Orders.

## Template
### WORK ORDER: [ID]
- Objective / Target Files & Symbols / Required Mutation / Verification Command / Expected Result / Designated Agent

## Closing protocol
DONE (verification passed) | BLOCKED: <reason> (back to planner) | FAILED x2 (escalate one tier)""", rows=9)
label(".claude/skills/docs-map/SKILL.md")
code("""---
name: docs-map
description: Which file to open per area and how to read big files cheaply. Use before reading docs or large files.
---
# Where to read and how to update (little, in sections)
One index: docs/progress.md. Start there. Never read a big file whole: grep -n the symbol or '^##',
then Read with offset/limit. A file already read in this session is not read again.

## Area map
| Area   | Open                      |
|--------|---------------------------|
| <area> | docs/progress/<area>.md   |

## Updating progress after a change
1. grep -n '^##' the area sheet; read only the subsection of the module you touched.
2. Fix or delete lines that became false. Record state (files, symbols), never dates or "we did X".
3. Sheet over ~120 lines: split by topic (<area>-<topic>.md), link it from the parent and the index.
4. Delegate to mechanic when the lines are already decided.

## Wide searches
"Where is X / who uses Y" across many files -> research agent (returns the conclusion, not the files).""", rows=9)
label(".claude/skills/docs-loop/SKILL.md")
code("""---
name: docs-loop
description: Tidies CLAUDE.md and docs/progress* in measure-keep-discard rounds. Use after doc edits or to cut tokens.
---
# docs-loop
Measure = python scripts/docs_audit.py score (lower is better).

## Loop (max 8 rounds, one hypothesis per round), pick in this order
1. An audit penalty: orphan -> link it; broken -> fix; over 120 lines -> split; duplicate -> keep one copy.
2. An area-specific rule sitting in CLAUDE.md -> move it to the area sheet "Rules", leave one line.
3. The most re-read file -> split it or add a search command for it to the docs-map skill.
4. An index row that does not help find a file -> remove it.
Each round: back up touched files; edit; check information was MOVED, not lost; run score.
Lower and nothing lost -> keep and log it; otherwise restore the backup and log a discard.
Stop at 8 rounds, after 2 discards in a row, or when out of hypotheses.

## Repeated workflows become skills
A prompt or command sequence seen in 3 or more sessions, covered by no skill, becomes a skill
(under 60 lines, trigger in the description).

## Close
Report score before -> after, rounds kept/discarded, skills created. No commit unless asked.""", rows=9)

h1("12. Template: .claude/settings.json")
p("Two jobs: keep the always-loaded context small, and turn the rules that must never be broken into <b>enforcement</b> rather than advice. "
  "Permission rules use the form " + c("Tool(pattern)") + "; for logic a pattern cannot express, use a " + c("PreToolUse") + " hook. "
  "Project settings go in " + c(".claude/settings.json") + " (shared); personal overrides in " + c(".claude/settings.local.json") + " (git-ignored). "
  "The " + c("skillOverrides") + " key comes from the reference project and is not in the public settings reference I checked: verify it on your Claude Code version.")
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

h1("13. Templates: progress index and area sheet")
label("docs/progress.md  (the only index; relative links, so the audit can follow them)")
code("""# <Project> - Progress Index
State, not history. Open ONE area sheet, never all of them.

## Which section to open
| Area   | Sheet                          | Open it when                       |
|--------|--------------------------------|------------------------------------|
| <area> | [core](progress/<area>.md)     | <one line: what lives there>       |

## Open milestones
- M1 <name>: <one line> (details: [ROADMAP](ROADMAP.md))

## Reference docs
- [design/<topic>.md](design/<topic>.md): speculative RFCs, not state.""", rows=9)
label("docs/progress/<area>.md  (80-120 lines; three fixed headings)")
code("""# <Area> - current state
## How it is today
### <module>
- <what exists now>; symbols to search: `file.ext:symbol`

## Open work
- [ ] <task>. Verify: `<command>`

## Rules (do not break)
- <binding constraint>. Why: <one line>""", rows=9)

h1("14. Starter: scripts/docs_audit.py")
p("A dependency-free audit (Python 3). The score is the tokens always loaded (" + c("CLAUDE.md") + " + index) plus the mean area-sheet size plus penalties; "
  "it exits non-zero on any penalty or if the score rises above the saved baseline. " + c("score --save") + " writes the baseline to " + c(".docs-score") + ".")
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

# ============================================================ PART C
part("PART C — AUTOMATION AND TOOLING")

h1("15. The Three Pillars of Continuous Automation")
p("<b>A. Git pre-commit hook.</b> Hooks inside " + c(".git/hooks") + " are not versioned, so keep the hook in the repo and point git at it once per clone:")
label(".githooks/pre-commit   (chmod +x, then: git config core.hooksPath .githooks)")
code("""#!/bin/sh
python3 scripts/docs_audit.py score || { echo "docs audit failed: fix links/orphans/size"; exit 1; }""")
p("<b>B. Makefile / build tool.</b> Add a docs target and include it in the standard test target:")
code("""test-docs:
    python3 scripts/docs_audit.py score    # recipe lines must start with a TAB, not spaces

test: test-docs   # add to the existing prerequisites of your standard test target""")
p("<b>C. CI/CD (GitHub Actions).</b> A dedicated step inside the build/test workflow that runs on pull requests:")
code("""      - name: Check documentation audit
        run: python3 scripts/docs_audit.py score""")

h1("16. Focus: Google Antigravity &amp; Gemini")
p("Claude relies on " + c("CLAUDE.md") + "; Google Antigravity uses a modular, enterprise-style architecture:")
b("<b>Primary file " + c("GEMINI.md") + " (or " + c("AGENTS.md") + "):</b> takes absolute precedence. It has a dedicated rules budget (20,000 tokens, separate from general context) with smart truncation and a switch to on-demand pointers when exceeded.")
b("<b>Native progressive disclosure:</b> skills in " + c(".agents/skills/") + " (or " + c(".claude/skills/") + ") expose only a short name and description in the prompt; the long body loads only when the model decides to activate the skill.")
b("<b>Mobile / cloud relay via GitHub Actions:</b> with " + c(".github/workflows/antigravity-agent.yml") + " the agent can be launched from a smartphone on GitHub Actions runners, using no local resources.")
b("<b>Parallel asynchronous subagents</b> with differentiated tools (for example a read-only " + c("research") + " agent that keeps the main context clean, and independent self-contained runs).")
table([
    ["Aspect", "Claude Code", "Google Antigravity"],
    ["Root instructions", c("CLAUDE.md") + " (AGENTS.md only if CLAUDE.md is absent or imported)", c("GEMINI.md") + " or " + c("AGENTS.md")],
    ["Skills folder", c(".claude/skills/<name>/SKILL.md"), c(".agents/skills/") + " (also reads " + c(".claude/skills/") + ")"],
    ["Agent definitions", c(".claude/agents/*.md") + " (model alias, effort, tools)", "invoke_subagent with model \"pro\", \"flash\" or \"flash_lite\""],
    ["Hard enforcement", "permissions allow/deny, hooks", "per-runtime settings; keep the same rules in GEMINI.md"],
], [92, 215, 197])

h1("17. Claude Plugins: crab-ledger and better-prompt")
p("Two open-source plugins extend the efficiency and quality of Claude sessions:")
b("<b>crab-ledger (cost and re-read monitoring):</b> tracks token use in real time, counts file re-reads and computes the cost of each session. It pairs with " + c("scripts/docs_audit.py rereads") +
  " (full audit version) and writes " + c("~/.claude/crab-ledger-report.json") + ", used to spot bottlenecks and files re-read so often that they need splitting.")
b("<b>better-prompt (prompt engineering):</b> refines, structures and optimises the prompts sent to Claude: removes ambiguity, applies clear roles and format, and cuts token use at the source.")
p("Both can be installed from their GitHub repositories (crab-ledger and better-prompt). The docs-loop skill reads the crab-ledger report to decide which files to split first.")

h1("18. What Claude Can Inherit from Antigravity / Gemini")
n(1, "<b>Separate research subagent for heavy searches:</b> instead of reading dozens of files in the main session, delegate to a fast read-only agent (Haiku/Sonnet) that returns a 5-line summary.")
n(2, "<b>Progressive disclosure for instructions:</b> move complex procedures into on-demand skills with YAML frontmatter and keep " + c("CLAUDE.md") + " to the bare minimum.")
n(3, "<b>Cross-agent deduplication:</b> keep " + c("CLAUDE.md") + " and " + c("GEMINI.md") + " aligned on the same index (" + c("docs/progress.md") + ") so any team member can use either assistant with the same guarantees.")

# ============================================================ PART D
part("PART D — BASELINE RULES TO IMPROVE AND IMPLEMENT THIS GUIDE")
h1("19. Baseline Rules and the Improvement Loop")
p("Start every new project with these rules. Each one names how it is enforced, so the scaffolding improves itself instead of decaying.")
table([
    ["#", "Rule", "Enforced by"],
    ["R1", "<b>One index.</b> All state is reached from " + c("docs/progress.md") + "; CLAUDE.md points there and lists no other doc.", "Audit penalty (+2000)"],
    ["R2", "<b>Thin root.</b> CLAUDE.md under 60 lines; a rule that concerns one area moves to that area's Rules section, leaving one line.", "Audit penalty (+400)"],
    ["R3", "<b>State, not chronicle.</b> Sheets say how things are now: files and symbols, no dates, no \"we did X\". Closed work is removed from Open work.", "docs-loop review"],
    ["R4", "<b>Small sheets.</b> 80-120 lines each; larger ones are split by topic and linked from parent and index.", "Audit penalty (+200)"],
    ["R5", "<b>Surgical reads.</b> Never read a big file whole; grep then offset/limit; never re-read a file already read in the session; wide searches go to research.", "crab-ledger re-read report"],
    ["R6", "<b>Route by readiness, not size.</b> Apply the 5-question gate; any NO goes to planner; nothing reaches deep without a repro.", "Agent descriptions, work-order skill"],
    ["R7", "<b>Explicit status.</b> Every agent ends with DONE, BLOCKED or FAILED x2; at most two attempts per tier, then escalate one tier.", "Agent bodies"],
    ["R8", "<b>Verification is mandatory.</b> No Work Order without a deterministic command; agents that must run it get Bash in " + c("tools") + ".", "Work Order template, agent frontmatter"],
    ["R9", "<b>Leave no trace.</b> Close each task: update Open work, add any new constraint to Rules, run the audit.", "CLAUDE.md section 5, hook"],
    ["R10", "<b>Ratchet.</b> The audit score never rises above the saved baseline; lower it deliberately and re-save.", "docs_audit.py, CI"],
    ["R11", "<b>Improvement loop.</b> One hypothesis per round, measure, keep or discard, maximum 8 rounds, stop after 2 consecutive discards.", "docs-loop skill"],
    ["R12", "<b>Repeat means skill.</b> A workflow seen in 3 or more sessions becomes a skill under 60 lines with the trigger in its description.", "docs-loop skill"],
    ["R13", "<b>Learn from cost.</b> A bug or surprise that cost more than one session adds one line to the area's Rules, with the reason.", "Closing step (R9)"],
    ["R14", "<b>Tune from evidence.</b> Monthly, count BLOCKED and FAILED x2 per agent: many BLOCKED means the gate or planner is too weak; many FAILED x2 at one tier means raise the tier or tighten the Work Order template.", "Manual review"],
    ["R15", "<b>Hard rules are enforced, not requested.</b> Secrets, destructive commands and forbidden paths go in permission deny rules or a PreToolUse hook, not only in CLAUDE.md.", "settings.json"],
    ["R16", "<b>Always-loaded budget.</b> Turn off built-in skills the project never uses; keep every skill description to 1-2 sentences.", "settings.json, review"],
    ["R17", "<b>Parity.</b> CLAUDE.md and GEMINI.md share sections 1, 2, 4, 5 verbatim; only the routing table differs.", "Pull-request review"],
    ["R18", "<b>Safe defaults.</b> Branch before editing, selective tests only, no secrets in the repo, commits only on request.", "CLAUDE.md section 2"],
], [26, 372, 106])

doc = SimpleDocTemplate(os.path.abspath("docs/scaffold.pdf"), pagesize=letter,
                        leftMargin=54, rightMargin=54, topMargin=54, bottomMargin=54,
                        title="AI Scaffolding & Agent Architecture Guide", author="bm project")
doc.build(story, canvasmaker=NumberedCanvas)
print("PDF successfully generated: docs/scaffold.pdf")
