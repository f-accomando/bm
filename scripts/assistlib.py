"""
The development assistant (M30), the PC side: the knowledge base parser, the
text features, the integer network and the BMAI file the kernel embeds.

Everything here is the reference the C code in src/ai/ must match bit for bit
(tests/ai/test_ai.c): the features of a question, the logits of the network.
Only the standard library: `make` builds the assistant without numpy (the
training, scripts/trainassist.py, needs it; its result is committed).
"""
import math
import os
import re
import struct
import zlib

# ---------------------------------------------------------------- text

NBUCKETS = 4096         # hashed features (power of two)
MAX_FEATS = 255         # distinct features per question
MAX_TOKENS = 64
MAX_TOKEN_LEN = 24

# Function words that say nothing about what is asked. Kept short on
# purpose: the network learns which words matter.
STOPWORDS = sorted(set("""
il lo la gli le un uno una di da in con su per tra fra ed che chi cosa come
si mi ti ci vi ne del dello della dei degli delle al allo alla ai agli alle dal
dallo dalla dai dagli dalle nel nello nella nei negli nelle sul sullo sulla sui
sugli sulle col coi sono sei ho hai ha abbiamo avete hanno posso puoi puo vorrei
voglio devo mio mia miei mie tuo tua questo questa quello quella qui qua poi
anche ma se non piu molto
the an of to in on at for with and or how do does you my can is are what
which it this that be by from want would should could me please
""".split()))

# code page 437 (the console font and the keyboard) -> plain letters
_CP437_FOLD = {
    0x80: 'c', 0x81: 'u', 0x82: 'e', 0x83: 'a', 0x84: 'a', 0x85: 'a', 0x86: 'a', 0x87: 'c',
    0x88: 'e', 0x89: 'e', 0x8A: 'e', 0x8B: 'i', 0x8C: 'i', 0x8D: 'i', 0x8E: 'a', 0x8F: 'a',
    0x90: 'e', 0x93: 'o', 0x94: 'o', 0x95: 'o', 0x96: 'u', 0x97: 'u', 0x98: 'y', 0x99: 'o',
    0x9A: 'u', 0xA0: 'a', 0xA1: 'i', 0xA2: 'o', 0xA3: 'u', 0xA4: 'n', 0xA5: 'n',
}
# UTF-8 0xC3 0x80..0xBF (Latin-1 letters) -> plain letters
_UTF8_C3_FOLD = {}
for _r, _c in ((range(0x80, 0x86), 'a'), ([0x87], 'c'), (range(0x88, 0x8C), 'e'),
               (range(0x8C, 0x90), 'i'), ([0x91], 'n'), (range(0x92, 0x97), 'o'),
               (range(0x99, 0x9D), 'u'), (range(0xA0, 0xA6), 'a'), ([0xA7], 'c'),
               (range(0xA8, 0xAC), 'e'), (range(0xAC, 0xB0), 'i'), ([0xB1], 'n'),
               (range(0xB2, 0xB7), 'o'), (range(0xB9, 0xBD), 'u')):
    for _b in _r:
        _UTF8_C3_FOLD[_b] = _c

# text for the console: code page 437, what the font draws
_TO_437 = {'—': '-', '–': '-', '’': "'", '‘': "'", '“': '"',
           '”': '"', '…': '...', '×': 'x', '→': '->', '←': '<-',
           '↑': '^', '↓': 'v', '·': '.', '≤': '<=', '≥': '>=',
           '≠': '~=', 'È': "E'", 'À': "A'", 'Ì': "I'", 'Ò': "O'", 'Ù': "U'"}


def to_cp437(s, where=''):
    out = bytearray()
    for ch in s:
        ch = _TO_437.get(ch, ch)
        try:
            out += ch.encode('cp437')
        except UnicodeEncodeError:
            raise ValueError('%s: %r has no glyph in the console font' % (where, ch))
    return bytes(out)


def fnv1a(data, h=2166136261):
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def tokens(raw):
    """bytes (code page 437 or UTF-8) -> lower-case ASCII words"""
    if isinstance(raw, str):
        raw = to_cp437(raw)
    words, cur = [], bytearray()
    i, n = 0, len(raw)
    while i < n:
        b = raw[i]
        c = None
        if b == 0xC3 and i + 1 < n and raw[i + 1] in _UTF8_C3_FOLD:
            c = _UTF8_C3_FOLD[raw[i + 1]]
            i += 1
        elif b in _CP437_FOLD:
            c = _CP437_FOLD[b]
        elif 0x41 <= b <= 0x5A:
            c = chr(b + 32)
        elif 0x61 <= b <= 0x7A or 0x30 <= b <= 0x39 or b == 0x5F:
            c = chr(b)
        i += 1
        if c is not None:
            if len(cur) < MAX_TOKEN_LEN:
                cur.append(ord(c))
        elif cur:
            words.append(cur.decode())
            cur = bytearray()
            if len(words) == MAX_TOKENS:
                return words
    if cur and len(words) < MAX_TOKENS:
        words.append(cur.decode())
    return words


def content_words(raw):
    """the words that count: no function words, no single letters"""
    return [w for w in tokens(raw) if len(w) > 1 and w not in _STOPSET]


_STOPSET = set(STOPWORDS)


def features(raw, nbuckets=NBUCKETS):
    """sorted distinct buckets: words, pairs of words, letter triples"""
    ws = content_words(raw)
    out = set()

    def add(kind, s):
        if len(out) < MAX_FEATS:
            out.add(fnv1a(kind + s.encode()) & (nbuckets - 1))

    for w in ws:
        add(b'w', w)
    for a, b in zip(ws, ws[1:]):
        add(b'b', a + ' ' + b)
    for w in ws:
        if len(w) >= 3:
            t = '<' + w + '>'
            for k in range(len(t) - 2):
                add(b't', t[k:k + 3])
    return sorted(out)


def pool_table():
    """65536 / sqrt(n): the bag of features is divided by sqrt(count)"""
    return [0] + [int(round(65536 / math.sqrt(n))) for n in range(1, MAX_FEATS + 1)]


# ---------------------------------------------------------------- knowledge base

FIELDS = ('kind', 'name', 'title', 'ask', 'see', 'text', 'code', 'gen', 'keys')
KINDS = ('api', 'howto', 'error', 'sprite', 'tip', 'none')
# a field is `key:` then a space or the end of the line (so `text:sub(1)` in
# a code block stays code)
_FIELD_RE = re.compile(r'^(kind|name|title|ask|see|text|code|gen|keys):(?: (.*))?$')


class Entry:
    def __init__(self, eid, where):
        self.id, self.where = eid, where
        self.kind = self.name = self.title = self.text = self.code = self.gen = ''
        self.ask, self.see, self.keys = [], [], []

    def phrases(self):
        """questions that lead here: the asks, the title, the name"""
        out = list(self.ask)
        if self.title:
            out.append(self.title)
        if self.name:
            out.append(self.name)
        return out


def parse_kb(paths):
    """src/ai/kb/*.txt -> [Entry]. A block starts with `== id`; fields are
    `key: value` lines; text: and code: go on until the next field."""
    entries, seen = [], {}
    for path in paths:
        cur, field = None, None
        with open(path, encoding='utf-8') as f:
            lines = f.read().split('\n')
        for ln, line in enumerate(lines, 1):
            where = '%s:%d' % (path, ln)
            if line.startswith('== '):
                cur = Entry(line[3:].strip(), where)
                if not re.match(r'^[a-z0-9_.]+$', cur.id):
                    raise ValueError('%s: bad id %r' % (where, cur.id))
                if cur.id in seen:
                    raise ValueError('%s: %s already defined at %s' % (where, cur.id, seen[cur.id]))
                seen[cur.id] = where
                entries.append(cur)
                field = None
                continue
            if cur is None:
                if line.strip() and not line.startswith('#'):
                    raise ValueError('%s: text outside an entry' % where)
                continue
            m = _FIELD_RE.match(line)
            if m:
                key, val = m.group(1), m.group(2) or ''
                field = key
                if key == 'ask':
                    cur.ask += [p.strip() for p in val.split('|') if p.strip()]
                elif key in ('see', 'keys'):
                    getattr(cur, key).extend(v.strip() for v in val.split(',') if v.strip())
                elif key in ('text', 'code'):
                    setattr(cur, key, val + '\n' if val else '')
                else:
                    setattr(cur, key, val.strip())
                continue
            if field in ('text', 'code'):
                setattr(cur, field, getattr(cur, field) + line + '\n')
            elif field == 'ask' and line.startswith('  '):
                cur.ask += [p.strip() for p in line.split('|') if p.strip()]
            elif line.strip() and not line.startswith('#'):
                raise ValueError('%s: line outside a field' % where)
    for e in entries:
        # the text is prose: its lines are joined (the panel wraps it to its
        # width); a blank line starts a new paragraph
        paras = re.split(r'\n\s*\n', e.text.strip('\n'))
        e.text = '\n'.join(' '.join(l.strip() for l in p.split('\n')) for p in paras)
        e.code = e.code.strip('\n')
        if e.kind not in KINDS:
            raise ValueError('%s: kind %r (one of %s)' % (e.where, e.kind, ', '.join(KINDS)))
        if e.kind != 'none' and not e.title:
            raise ValueError('%s: no title' % e.where)
        if e.kind == 'sprite' and not e.gen:
            raise ValueError('%s: a sprite entry needs gen:' % e.where)
        for s in e.see:
            if s not in seen:
                raise ValueError('%s: see %r: no such entry' % (e.where, s))
        for fld in ('title', 'text', 'code'):
            to_cp437(getattr(e, fld), e.where)
        for ln in e.code.split('\n'):
            if len(ln) > 72:
                raise ValueError('%s: code line longer than 72 characters (the panel): %r' % (e.where, ln))
    return entries


def parse_tests(path):
    """`question => id[, id]`: held-out questions, never trained on"""
    out = []
    with open(path, encoding='utf-8') as f:
        for ln, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            q, _, ids = line.partition('=>')
            out.append((q.strip(), [i.strip() for i in ids.split(',') if i.strip()], ln))
    return out


def kb_paths(kbdir):
    return sorted(os.path.join(kbdir, f) for f in os.listdir(kbdir)
                  if f.endswith('.txt') and f != 'tests.txt')


# ---------------------------------------------------------------- weights file

WEIGHTS_MAGIC = b'BMAIQ1\0\0'


def save_weights(path, classes, net):
    """the quantized network (see quantize); classes: the entry ids of the
    output rows, in order"""
    hid = len(net['b1'])
    names = '\n'.join(classes).encode()
    with open(path, 'wb') as f:
        f.write(WEIGHTS_MAGIC)
        f.write(struct.pack('<IIIIiif', NBUCKETS, hid, len(classes), len(names),
                            net['mult'], net['shift'], net['scale']))
        f.write(names)
        f.write(struct.pack('<%db' % len(net['w1']), *net['w1']))
        f.write(struct.pack('<%di' % hid, *net['b1']))
        f.write(struct.pack('<%db' % len(net['w2']), *net['w2']))
        f.write(struct.pack('<%di' % len(classes), *net['b2']))


def load_weights(path):
    """-> (classes, net)"""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != WEIGHTS_MAGIC:
        raise ValueError('%s: not a weights file of this version' % path)
    nb, hid, ncls, nlen, mult, shift, scale = struct.unpack_from('<IIIIiif', data, 8)
    if nb != NBUCKETS:
        raise ValueError('%s: %d features, this version has %d' % (path, nb, NBUCKETS))
    p = 8 + 28
    classes = data[p:p + nlen].decode().split('\n') if ncls else []
    p += nlen
    net = dict(mult=mult, shift=shift, scale=scale)
    for key, fmt, n in (('w1', 'b', nb * hid), ('b1', 'i', hid), ('w2', 'b', ncls * hid), ('b2', 'i', ncls)):
        size = 1 if fmt == 'b' else 4
        net[key] = list(struct.unpack_from('<%d%s' % (n, fmt), data, p))
        p += size * n
    return classes, net


# ---------------------------------------------------------------- integer network

def embed(q1, hid, feats, qb1, pool):
    acc = [0] * hid
    for f in feats:
        row = q1[f * hid:(f + 1) * hid]
        for j in range(hid):
            acc[j] += row[j]
    p = pool[len(feats)]
    return [((a * p) >> 16) + b for a, b in zip(acc, qb1)]


def relu_q(acc, mult, shift):
    half = 1 << (shift - 1)
    return [min(127, (a * mult + half) >> shift) if a > 0 else 0 for a in acc]


def dense(q2, qb2, y, nout):
    hid = len(y)
    return [qb2[k] + sum(q2[k * hid + j] * y[j] for j in range(hid)) for k in range(nout)]


def logits(net, feats):
    """the integer network, exactly as src/ai/nn.c computes it:
    acc = (sum of the int8 rows of the features) * pool[n] >> 16 + b1
    y   = clamp((max(acc, 0) * mult + 2^(shift-1)) >> shift, 0, 127)
    out = W2 . y + b2            (out * scale: the float logits)"""
    hid = len(net['b1'])
    acc = embed(net['w1'], hid, feats, net['b1'], pool_table())
    y = relu_q(acc, net['mult'], net['shift'])
    return dense(net['w2'], net['b2'], y, len(net['b2']))


# ---------------------------------------------------------------- BMAI file

BMAI_VERSION = 1
# entry string fields, in the order of the ENTR records
ENTRY_FIELDS = ('id', 'kind', 'title', 'name', 'text', 'code', 'gen', 'see', 'keys')
NO_CLASS = -(1 << 30)       # the bias of an entry the network was not trained on


def build_bmai(entries, net, hidden, classes):
    """the file the kernel embeds (src/ai/assist.c reads it)"""
    strs = bytearray(b'\0')
    index = {}

    def s(text):
        b = to_cp437(text)
        if b in index:
            return index[b]
        index[b] = len(strs)
        strs.extend(b + b'\0')
        return index[b]

    # the output rows follow the entries; an entry without trained weights
    # gets a zero row and a bias that never wins (only the words find it)
    nent = len(entries)
    w2 = [0] * (nent * hidden)
    b2 = [NO_CLASS] * nent
    if net:
        row = {c: i for i, c in enumerate(classes)}
        for k, e in enumerate(entries):
            if e.id in row:
                r = row[e.id]
                w2[k * hidden:(k + 1) * hidden] = net['w2'][r * hidden:(r + 1) * hidden]
                b2[k] = net['b2'][r]
    entr = bytearray(struct.pack('<I', nent))
    for e in entries:
        vals = dict(id=e.id, kind=e.kind, title=e.title, name=e.name, text=e.text, code=e.code,
                    gen=e.gen, see=','.join(e.see), keys=','.join(e.keys))
        entr += struct.pack('<%dI' % len(ENTRY_FIELDS), *(s(vals[f]) for f in ENTRY_FIELDS))
    stop = bytearray(struct.pack('<I', len(STOPWORDS)))
    for w in STOPWORDS:
        stop += w.encode() + b'\0'
    text = struct.pack('<III', NBUCKETS, MAX_FEATS, MAX_TOKENS)
    pool = struct.pack('<%di' % (MAX_FEATS + 1), *pool_table())
    if net:
        embd = struct.pack('<II', NBUCKETS, hidden) + struct.pack('<%db' % len(net['w1']), *net['w1']) \
            + struct.pack('<%di' % hidden, *net['b1'])
        relu = struct.pack('<ii', net['mult'], net['shift'])
        scale = net['scale']
    else:
        embd = struct.pack('<II', NBUCKETS, hidden) + bytes(NBUCKETS * hidden) + bytes(4 * hidden)
        relu = struct.pack('<ii', 0, 1)
        scale = 1.0
    dens = struct.pack('<II', hidden, nent) + struct.pack('<%db' % len(w2), *w2) \
        + struct.pack('<%di' % nent, *b2) + struct.pack('<f', scale)
    sections = [(b'TEXT', text), (b'STOP', bytes(stop)), (b'POOL', pool), (b'EMBD', embd),
                (b'RELU', relu), (b'DENS', dens), (b'ENTR', bytes(entr)), (b'STRS', bytes(strs))]
    head = 8 + 12 * len(sections)
    out = bytearray(head)
    struct.pack_into('<4sHH', out, 0, b'BMAI', BMAI_VERSION, len(sections))
    for i, (tag, data) in enumerate(sections):
        while len(out) % 4:
            out.append(0)
        struct.pack_into('<4sII', out, 8 + 12 * i, tag, len(out), len(data))
        out += data
    while len(out) % 4:
        out.append(0)
    crc = zlib.crc32(bytes(out)) & 0xFFFFFFFF
    return bytes(out) + struct.pack('<I', crc)
