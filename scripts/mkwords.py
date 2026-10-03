#!/usr/bin/env python3
"""The dictionaries of the word completion (src/ai/predict.lua): how often
each word is written and which words follow it.

  it, en   from the texts in src/ai/words (written for bm: stories, messages,
           games, technique, letters; en: comments, games, messages), plus
           the Italian lexicon (it_lessico.txt: forms that the texts lack)
  lua      from the Lua of the games (comments and strings left out), the
           code of the assistant's knowledge base, its API names and their
           signatures (src/ai/kb/api_*.txt)
  ask      the questions to the assistant: the ask:, title: and keys: lines
           of its knowledge base (src/ai/kb, not tests.txt), mixed with "it"
           where one talks to the assistant

Output: a Lua module (require "words") built into the kernel, text in
code page 437 like the console's:

  uni  "word count[ f]" per line, sorted by key (lowercase, no accents):
       the console finds a prefix with a binary search; " f" = a function
       (code: the completion adds "(")
  big  "prev total next count next count ..." per line: the words that
       follow prev ("^" = the start of a sentence, or of an indented line
       of code; "^0" = a line of code that is not indented)
  api  "name signature" per line (code)

  mkwords.py -o build/words.lua
"""
import argparse
import collections
import glob
import os
import re
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CP437 = {'à': 0x85, 'è': 0x8A, 'é': 0x82, 'ì': 0x8D, 'ò': 0x95, 'ù': 0x97,
         'á': 0xA0, 'í': 0xA1, 'ó': 0xA2, 'ú': 0xA3}
PROSE = re.compile(r"[a-zA-Zàèéìòóùáíú]+'?|[.?!\n]")
CODE = re.compile(r"--\[(=*)\[.*?\]\1\]|--[^\n]*|\[(=*)\[.*?\]\2\]|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|"
                  r"0[xX][0-9a-fA-F]+|[0-9][0-9.]*(?:[eE][-+]?[0-9]+)?|[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*|\n|\(|[^\sA-Za-z_(]", re.S)
KEYWORDS = set("and break do else elseif end false for function goto if in local nil not or repeat "
               "return then true until while".split())
MAX_NEXT = 24           # followers kept per word


def key(w):
    return ''.join(c for c in unicodedata.normalize('NFD', w.lower()) if unicodedata.category(c) != 'Mn')


def cp437(s):
    out = bytearray()
    for ch in s:
        if ch in CP437:
            out.append(CP437[ch])
        elif ord(ch) < 128:
            out.append(ord(ch))
        else:
            raise ValueError('not in code page 437: %r' % ch)
    return bytes(out)


def prose_counts(texts, lexicon=()):
    """Words lowercase, except the names (capital inside a sentence more
    often than not): the followers of each word, keyed lowercase."""
    uni, big = collections.Counter(), collections.defaultdict(collections.Counter)
    upper, inside = collections.Counter(), collections.Counter()
    seq = []
    for t in texts:
        t = t.replace('\u2019', "'").replace('È', 'è')
        prev = '^'
        for tok in PROSE.findall(t):
            if tok in '.?!\n':
                prev = '^'
                seq.append(None)
                continue
            w = tok.lower()
            if prev != '^':
                inside[w] += 1
                upper[w] += tok[0].isupper()
            seq.append((prev, w))
            prev = w
    form = {w: (w[0].upper() + w[1:] if upper[w] * 2 > inside[w] else w) for w in inside}
    for item in seq:
        if item:
            prev, w = item
            uni[form.get(w, w)] += 1
            big[prev][form.get(w, w)] += 1
    for w in lexicon:
        w = w.strip().lower()
        if w and w not in uni and form.get(w, w) not in uni:
            uni[w] = 0
    return uni, big, set()


def code_counts(sources):
    uni, big = collections.Counter(), collections.defaultdict(collections.Counter)
    calls = collections.Counter()
    for src in sources:
        prev, last, start = '^', None, 0
        for m in CODE.finditer(src):
            tok = m.group(0)
            if tok == '\n':
                prev, start = '^', m.end()
                continue
            if tok == '(':
                if last:
                    calls[last] += 1
                continue
            last = None
            if not (tok[0].isalpha() or tok[0] == '_'):
                continue                    # comments, strings, numbers, symbols
            uni[tok] += 1
            big['^0' if prev == '^' and m.start() == start else prev][tok] += 1
            prev = last = tok
    funcs = {w for w, c in uni.items() if w not in KEYWORDS and calls[w] * 2 > c}
    return uni, big, funcs


def kb_api():
    api = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'src/ai/kb/api_*.txt'))):
        name = None
        for line in open(f, encoding='utf-8'):
            if line.startswith('== '):
                name = None
            elif line.startswith('name: '):
                name = line[6:].strip()
            elif line.startswith('title: ') and name:
                api[name] = line[7:].split(' - ')[0].strip()
    return api


def kb_questions():
    """The questions of the knowledge base, one per line, as prose."""
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, 'src/ai/kb/*.txt'))):
        if f.endswith('tests.txt'):
            continue
        for line in open(f, encoding='utf-8'):
            if line.startswith('ask:'):
                out += [q.strip() for q in line[4:].split('|')]
            elif line.startswith('title:'):
                out.append(line[6:].split(' - ')[-1].strip())
            elif line.startswith('keys:'):
                out += [k.strip() for k in line[5:].split(',')]
    return '\n'.join(q for q in out if q) + '\n'


def kb_code():
    out, inside = [], False
    for f in sorted(glob.glob(os.path.join(ROOT, 'src/ai/kb/*.txt'))):
        for line in open(f, encoding='utf-8'):
            if line.startswith('code:'):
                inside = True
            elif line.startswith('== ') or re.match(r'^[a-z]+:', line):
                inside = False
            elif inside:
                out.append(line)
    return ''.join(out)


def table(uni, big, funcs, min_total=2):
    words = sorted(uni, key=lambda w: (key(w), w))
    lines = ['%s %d%s' % (w, uni[w], ' f' if w in funcs else '') for w in words]
    blines = []
    for prev in sorted(big):
        nxt = big[prev]
        total = sum(nxt.values())
        if total < min_total:
            continue
        top = sorted(nxt.items(), key=lambda x: (-x[1], x[0]))[:MAX_NEXT]
        blines.append(prev + ' %d ' % total + ' '.join('%s %d' % (w, c) for w, c in top))
    return '\n'.join(lines) + '\n', '\n'.join(blines) + '\n'


def long_string(s):
    assert ']==]' not in s
    return '[==[\n' + s + ']==]'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--stats', action='store_true', help='print the sizes')
    ap.add_argument('--skip', help='leave out the texts whose name starts with this (benchmark)')
    a = ap.parse_args()
    words = os.path.join(ROOT, 'src/ai/words')
    it_texts = [open(f, encoding='utf-8').read() for f in sorted(glob.glob(os.path.join(words, 'it_*.txt')))
                if not f.endswith('it_lessico.txt') and not (a.skip and os.path.basename(f).startswith(a.skip))]
    lexicon = open(os.path.join(words, 'it_lessico.txt'), encoding='utf-8').read().split('\n')
    en_texts = [open(f, encoding='utf-8').read() for f in sorted(glob.glob(os.path.join(words, 'en_*.txt')))]
    games = ['pong', 'snake', 'shooter', 'astrowing', 'hunt', 'texroom', 'village', 'demo']
    lua = [open(os.path.join(ROOT, 'carts', g, 'main.lua'), encoding='utf-8').read() for g in games]
    for d in ('kitchen', 'titan'):
        lua += [open(f, encoding='utf-8').read() for f in sorted(glob.glob(os.path.join(ROOT, 'carts', d, 'src', '*.lua')))]
    lua.append(kb_code())
    api = kb_api()

    parts = ['-- The dictionaries of the word completion, made by scripts/mkwords.py',
             '-- from src/ai/words and the Lua of the games: do not edit.', 'return {']
    for lang, (uni, big, funcs) in (('it', prose_counts(it_texts, lexicon)), ('en', prose_counts(en_texts)),
                                    ('lua', code_counts(lua)), ('ask', prose_counts([kb_questions()]))):
        if lang == 'lua':
            for name in api:                # every API name, used or not
                uni.setdefault(name, 0)
                funcs.add(name)
        u, b = table(uni, big, funcs)
        parts.append('%s = {' % lang)
        parts.append('uni = ' + long_string(u) + ',')
        parts.append('big = ' + long_string(b) + ',')
        if lang == 'lua':
            parts.append('api = ' + long_string(''.join('%s %s\n' % (n, s) for n, s in sorted(api.items()))) + ',')
        parts.append('},')
        if a.stats:
            print('%s: %d words, %d with followers, %d KB' % (lang, len(uni), b.count('\n'), (len(u) + len(b)) // 1024))
    parts.append('}\n')
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, 'wb') as f:
        f.write(cp437('\n'.join(parts)))


if __name__ == '__main__':
    main()
