#!/usr/bin/env python3
"""
Builds the development assistant's file (M30), embedded in the kernel:
the knowledge base src/ai/kb/*.txt and the trained network
src/ai/assist.weights (scripts/trainassist.py) -> BMAI.

  mkassist.py -o build/assist.bin [--ref build/ai/ref.txt]

An entry added after the last training is still found by its words (API
name, keys), not by the network, until `make ai-model`.
--ref writes, for the test questions, the features and the network's output
as Python computes them: tests/ai/test_ai.c checks the C code against it.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assistlib as al  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--kb', default=os.path.join(ROOT, 'src/ai/kb'))
    ap.add_argument('--weights', default=os.path.join(ROOT, 'src/ai/assist.weights'))
    ap.add_argument('--ref', help='reference features and outputs for the C tests')
    ap.add_argument('--snippets', help='every code example, for tests/ai/check_snippets.lua')
    a = ap.parse_args()

    entries = al.parse_kb(al.kb_paths(a.kb))
    classes, net, hidden = [], None, 64
    if os.path.exists(a.weights):
        classes, net = al.load_weights(a.weights)
        hidden = len(net['b1'])
        ids = {e.id for e in entries}
        new = [e.id for e in entries if e.id not in set(classes)]
        gone = [c for c in classes if c not in ids]
        if new or gone:
            print('mkassist: the network is older than the knowledge base (%d new, %d removed entries):'
                  ' they are found only by their words until `make ai-model`' % (len(new), len(gone)))
    else:
        print('mkassist: no %s: words only, until `make ai-model`' % a.weights)
    blob = al.build_bmai(entries, net, hidden, classes)
    with open(a.out, 'wb') as f:
        f.write(blob)
    print('%s: %d bytes, %d entries, network %d -> %d -> %d' %
          (a.out, len(blob), len(entries), al.NBUCKETS, hidden, len(entries)))

    if a.snippets:
        with open(a.snippets, 'w', encoding='utf-8') as f:
            for e in entries:
                if e.code:
                    f.write('== %s\n%s\n' % (e.id, e.code))

    if a.ref:
        full = al.entry_net(entries, classes, net) if net else None
        tests = al.parse_tests(os.path.join(a.kb, 'tests.txt'))
        os.makedirs(os.path.dirname(os.path.abspath(a.ref)), exist_ok=True)
        with open(a.ref, 'w') as f:
            for q, ids, _ in tests:
                raw = al.to_cp437(q)
                feats = al.features(raw)
                f.write('Q %s\n' % raw.hex())
                f.write('F %s\n' % ' '.join(map(str, feats)))
                if full:
                    f.write('L %s\n' % ' '.join(map(str, al.logits(full, feats))))
                f.write('E %s\n' % ' '.join(ids))
        print('%s: %d questions' % (a.ref, len(tests)))


if __name__ == '__main__':
    main()
