#!/usr/bin/env python3
"""How many syllables Italian has, accents left out, and how often each is
written: the texts of the word completion (docs/PREDICT.md), `make syllables`.

  syllables.py [--words src/ai/words]

The syllables of the texts in src/ai/words (written for bm) and of their
lexicon, cut with the rules of Italian spelling: a consonant between two
vowels goes with the second (ca-sa), double consonants split (pi-zza is
piz-za), l m n r before another consonant close the syllable (can-to), the
groups that can start a word stay together (pa-dre, pa-sta), ch gh gn gl sc
and qu are one sound, the i of cia gio scia glio only marks the sound,
a e o next to each other split (pa-e-se), i and u next to a vowel make a
diphthong (pia-no, uo-mo).
"""
import argparse
import collections
import glob
import os
import re
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V = set('aeiou')
START = set('bl br cl cr dr fl fr gl gr pl pr tr vr ch gh gn sc sch scr spr str sp st sb sd sf sg '
            'sl sm sn sv sbr sdr sfr sgr spl scl ps pn'.split())
ITALIAN = 'bcdfghlmnpqrstvz'
WORD = re.compile(r"[a-zàèéìòóù]+", re.I)


def plain(s):
    return ''.join(c for c in unicodedata.normalize('NFD', s) if unicodedata.category(c) != 'Mn')


def syllables(w):
    """(onset, nucleus, coda) of each syllable of a word, lowercase, plain."""
    n = len(w)
    kind = ['V' if c in V else 'C' for c in w]
    for k in range(1, n - 1):
        if w[k] == 'u' and w[k - 1] in 'qg' and w[k + 1] in V:
            kind[k] = 'C'                          # qua, guerra: u in the onset
        if w[k] == 'i' and w[k - 1] in 'cg' and w[k + 1] in 'aou':
            kind[k] = 'C'                          # cia, gio: the i only marks the sound
    groups = []
    for k in range(n):
        if groups and groups[-1][0] == kind[k]:
            groups[-1][1] += w[k]
        else:
            groups.append([kind[k], w[k]])
    seq = []
    for kd, s in groups:
        if kd == 'C':
            seq.append(('C', s))
            continue
        parts = [s[0]]
        for c in s[1:]:
            if c in 'aeo' and parts[-1][-1] in 'aeo':
                parts.append(c)                    # pa-e-se: a hiatus
            else:
                parts[-1] += c
        for j, p in enumerate(parts):
            if j:
                seq.append(('C', ''))
            seq.append(('V', p))
    out, onset, i = [], '', 0
    if seq and seq[0][0] == 'C':
        onset, i = seq[0][1], 1
    while i < len(seq):
        nucleus, coda, nxt = seq[i][1], '', ''
        if i + 1 < len(seq):
            cl = seq[i + 1][1]
            if i + 2 >= len(seq):
                coda = cl                          # consonants at the end of the word
            elif len(cl) <= 1:
                nxt = cl
            elif cl[0] == cl[1] or cl[:2] == 'cq':
                coda, nxt = cl[0], cl[1:]
            elif cl in START or cl[:2] in ('ch', 'gh', 'gn', 'sc') or (cl[0] == 's' and cl[1:] in START) \
                    or cl[:3] == 'gli':
                nxt = cl
            else:
                coda, nxt = cl[0], cl[1:]
        out.append((onset, nucleus, coda))
        onset = nxt
        i += 2
    return out or [(w, '', '')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--words', default=os.path.join(ROOT, 'src/ai/words'))
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.words, 'it_*.txt')))
    texts = [open(f, encoding='utf-8').read().replace('’', "'") for f in files if not f.endswith('lessico.txt')]
    lexicon = open(os.path.join(a.words, 'it_lessico.txt'), encoding='utf-8').read().split()
    words = [plain(w.lower()) for t in texts for w in WORD.findall(t.replace("'", ''))]
    vocab = set(words) | {plain(w.lower().replace("'", '')) for w in lexicon}

    print("The syllables of Italian, accents excluded")
    print('\nIn theory (spelling):')
    simple = len(ITALIAN) * len(V)
    digraph = {'che chi ghe ghi': 4, 'gna gne gni gno gnu': 5, 'glia glie gli glio gliu': 5,
               'sce sci scia scio sciu sche schi': 7, 'cia cio ciu gia gio giu': 6, 'qua que qui quo': 4}
    print('  %d Italian consonants (%s) times 5 vowels: %d consonant + vowel syllables, %d with the vowels alone'
          % (len(ITALIAN), ' '.join(ITALIAN), simple, simple + 5))
    print('  plus %d written with two or three letters for one sound: %s'
          % (sum(digraph.values()), '; '.join(digraph)))
    print('  (q only goes with u, h almost only in ha, ho, hanno and inside ch and gh)')

    tok, onset, nuc, coda = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    for w in words:
        for o, n, c in syllables(w):
            tok[o + n + c] += 1
            onset[o] += 1
            nuc[n] += 1
            coda[c] += 1
    types = collections.Counter()
    for w in vocab:
        for o, n, c in syllables(w):
            types[o + n + c] += 1
    total = sum(tok.values())
    print('\nIn the texts of src/ai/words (%d words, %d distinct; with the lexicon %d distinct words):'
          % (len(words), len(set(words)), len(vocab)))
    print('  %d syllables, %.2f per word; %d distinct syllables in texts, %d with the lexicon'
          % (total, total / len(words), len(tok), len(types)))
    acc, marks = 0, [0.5, 0.8, 0.9, 0.95, 0.99]
    for k, (_, c) in enumerate(tok.most_common()):
        acc += c
        while marks and acc >= marks[0] * total:
            print('  the %d most frequent make up %d%% of the written syllables' % (k + 1, marks.pop(0) * 100))
    cv = sum(c for s, c in tok.items() if len(s) == 2 and s[0] not in V and s[1] in V)
    v1 = sum(c for s, c in tok.items() if len(s) == 1 and s in V)
    print('  consonant + vowel: %.1f%%; a single vowel: %.1f%%; together %.1f%%'
          % (100 * cv / total, 100 * v1 / total, 100 * (cv + v1) / total))
    print('  top 40:', ' '.join('%s %.1f%%' % (s, 100 * c / total) for s, c in tok.most_common(40)))
    on_total = sum(onset.values())
    print('\nSyllable onset (%d distinct):' % len(onset))
    print('  ' + ' '.join('%s %.1f%%' % (o or '(vowel)', 100 * c / on_total) for o, c in onset.most_common(32)))
    print('Vowels and diphthongs (%d distinct):' % len(nuc))
    print('  ' + ' '.join('%s %.1f%%' % (n or '-', 100 * c / total) for n, c in nuc.most_common(14)))
    print('Syllable coda (%d distinct):' % len(coda))
    print('  ' + ' '.join('%s %.1f%%' % (c or '(open)', 100 * k / total) for c, k in coda.most_common(12)))

    # the most frequent, in groups of four
    cv_tok = collections.Counter({s: c for s, c in tok.items() if len(s) == 2 and s[0] not in V and s[1] in V})
    first = collections.Counter()
    for o, c in onset.items():
        if o:
            first[o[0]] += c
    for title, cnt, of in (('Syllables', tok, total), ('Consonant + vowel', cv_tok, total),
                           ('Initial consonant (on syllables starting with a consonant)',
                            first, sum(first.values()))):
        print('\n%s, in groups of 4 by frequency:' % title)
        items, acc = cnt.most_common(16), 0
        for g in range(4):
            part = items[4 * g:4 * g + 4]
            share = 100 * sum(c for _, c in part) / of
            acc += share
            print('  %d) %s   = %.1f%% (in all %.1f%%)'
                  % (g + 1, '  '.join('%s %.1f%%' % (x, 100 * c / of) for x, c in part), share, acc))


if __name__ == '__main__':
    main()
