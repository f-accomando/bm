"""Guards for the skill's documents (SKILL.md, references/*.md).

SKILL.md and references/CLI.md are written by a later executor: until then
test_skill_md_short, test_frontmatter, test_come_chiedere, test_references_exist
and test_cli_doc_covers_subcommands fail. That is expected.
"""
import os
import re
import unittest

import _common as C

SKILL_MD = os.path.join(C.SKILL, "SKILL.md")
REFS = os.path.join(C.SKILL, "references")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def section(text, title, level="##"):
    """body of the first `level title` section, up to the next heading of the same or higher level"""
    m = re.search(r"^%s\s+%s[^\n]*\n" % (re.escape(level), re.escape(title)), text, re.M)
    if not m:
        return None
    rest = text[m.end():]
    stop = re.search(r"^#{1,%d}\s" % len(level), rest, re.M)
    return rest[:stop.start()] if stop else rest


class SkillDocs(unittest.TestCase):
    def test_skill_md_short(self):
        self.assertLess(len(read(SKILL_MD).splitlines()), 150)

    def test_frontmatter(self):
        text = read(SKILL_MD)
        self.assertTrue(text.startswith("---"))
        end = text.find("\n---", 3)
        self.assertGreater(end, 0, "frontmatter not closed")
        front = text[:end]
        self.assertIn("name: bm-forge", front)
        m = re.search(r"^description:[ \t]*(.*)$", front, re.M)
        self.assertIsNotNone(m)
        # inline value, or a block scalar / continuation lines
        after = front[m.end():].strip()
        self.assertTrue(m.group(1).strip(" >|-") or after, "empty description")

    def test_come_chiedere(self):
        body = section(read(SKILL_MD), "Come chiedere")
        self.assertIsNotNone(body, 'section "Come chiedere" missing')
        blocks = re.findall(r"```[^\n]*\n(.*?)```", body, re.S)
        self.assertGreaterEqual(len(blocks), 6)
        for i, b in enumerate(blocks):
            for word in ("COSA", "VINCOLI", "OUTPUT", "VERIFICA"):
                self.assertIn(word, b, "example block %d lacks %s" % (i + 1, word))

    def test_references_exist(self):
        text = read(SKILL_MD)
        names = set(re.findall(r"references/([A-Za-z0-9_.\-]+\.md)", text))
        self.assertTrue(names, "SKILL.md names no references/*.md")
        for n in sorted(names):
            with self.subTest(n):
                self.assertTrue(os.path.isfile(os.path.join(REFS, n)), n)

    def test_lessons_have_cases(self):
        text = read(os.path.join(REFS, "LESSONS.md"))
        parts = re.split(r"^(?=## L)", text, flags=re.M)[1:]
        self.assertTrue(parts, "no ## L sections")
        for p in parts:
            head = p.splitlines()[0]
            with self.subTest(head):
                self.assertIn("**Case**", p)
                self.assertIn("**Rule**", p)

    def test_cli_doc_covers_subcommands(self):
        text = read(os.path.join(REFS, "CLI.md"))
        for sub in ("mesh", "skel", "anim", "export", "check", "preview", "doctor"):
            with self.subTest(sub):
                self.assertRegex(text, r"(?m)^##\s+`?(bm_forge\.py\s+)?%s\b" % sub)


if __name__ == "__main__":
    unittest.main()
