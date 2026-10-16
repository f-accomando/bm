"""Tests for scripts/syllables.py: syllables() and plain()."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from syllables import plain, syllables


class SyllablesTest(unittest.TestCase):
    def test_casa(self):
        self.assertEqual(syllables('casa'), [('c', 'a', ''), ('s', 'a', '')])

    def test_pizza(self):
        self.assertEqual(syllables('pizza'), [('p', 'i', 'z'), ('z', 'a', '')])

    def test_canto(self):
        self.assertEqual(syllables('canto'), [('c', 'a', 'n'), ('t', 'o', '')])

    def test_padre(self):
        self.assertEqual(syllables('padre'), [('p', 'a', ''), ('dr', 'e', '')])

    def test_paese(self):
        self.assertEqual(syllables('paese'), [('p', 'a', ''), ('', 'e', ''), ('s', 'e', '')])

    def test_piano(self):
        self.assertEqual(syllables('piano'), [('p', 'ia', ''), ('n', 'o', '')])

    def test_per(self):
        self.assertEqual(syllables('per'), [('p', 'e', 'r')])

    def test_empty(self):
        self.assertEqual(syllables(''), [('', '', '')])

    def test_plain_accent(self):
        self.assertEqual(plain('città'), 'citta')


if __name__ == '__main__':
    unittest.main()
