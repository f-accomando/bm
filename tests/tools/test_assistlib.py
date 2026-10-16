"""Tests for scripts/assistlib.py: fnv1a, tokens, content_words, to_cp437, features."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from assistlib import content_words, features, fnv1a, to_cp437, tokens


class AssistlibTest(unittest.TestCase):
    def test_fnv1a(self):
        self.assertEqual(fnv1a(b''), 2166136261)
        self.assertEqual(fnv1a(b'a'), 0xE40C292C)

    def test_tokens_basic(self):
        self.assertEqual(tokens('Ciao, Mondo! x_1'), ['ciao', 'mondo', 'x_1'])

    def test_tokens_accent(self):
        self.assertEqual(tokens('città'), ['citta'])

    def test_tokens_max_len(self):
        self.assertEqual(tokens('a' * 30), ['a' * 24])

    def test_tokens_max_count(self):
        self.assertEqual(len(tokens(' '.join(['ab'] * 100))), 64)

    def test_content_words(self):
        self.assertEqual(content_words('il gatto e la volpe'), ['gatto', 'volpe'])

    def test_to_cp437_missing_glyph(self):
        with self.assertRaises(ValueError):
            to_cp437('\u2603')

    def test_features(self):
        f = features('gatto volpe')
        self.assertEqual(f, sorted(f))
        self.assertTrue(0 < len(f) <= 255)
        self.assertTrue(all(x in range(4096) for x in f))


if __name__ == '__main__':
    unittest.main()
