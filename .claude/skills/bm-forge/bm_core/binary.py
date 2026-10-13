"""Little-endian helpers and fixed-size names shared by the section codecs."""
import math
import struct

from .errors import ValidationError, need

NAME_FIELD = 16          # BM_MODEL_NAME, src/bm/bm.h:174 (also layer/sprite/box/bone/clip names)
NAME_MAX = 15            # bm-forge rule: 15 bytes + NUL (scripts/bmmesh.py:37 encode)


def f32(x):
    """x rounded to an IEEE single, as the console stores it"""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def finite_in(x, limit):
    """-limit < x < limit and not NaN (src/bm/format.c:157 for 1e6, :241 fin32 for 1e9)"""
    return isinstance(x, (int, float)) and not math.isnan(x) and -limit < x < limit


class Reader:
    """bounded reads of a byte string; any read past the end is a ValidationError"""

    def __init__(self, data, where):
        self.data = data
        self.off = 0
        self.where = where

    def take(self, n, what):
        if n < 0 or self.off + n > len(self.data):
            raise ValidationError(f"truncated: {what} needs {n} bytes at offset {self.off}, "
                                  f"{len(self.data) - self.off} left", self.where)
        out = self.data[self.off:self.off + n]
        self.off += n
        return out

    def unpack(self, fmt, what):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt), what))

    def left(self):
        return len(self.data) - self.off


def read_name(raw, where, what="name"):
    """a char[16] field -> str. Accepts 16 bytes with no NUL (the C reader does,
    src/bm/format.c:347-349; bm3d.lua writes such names, src/script/bm3d.lua:135-138).
    Bytes after the first NUL must be zero (writers pad with zeros), else the
    file would not round-trip: refused. UTF-8 must be valid: refused otherwise
    (NON VERIFICATO that every tool writes valid UTF-8; strict choice, see
    references/OPEN_QUESTIONS.md)."""
    cut = raw.split(b"\0", 1)
    name = cut[0]
    if len(cut) > 1:
        need(not cut[1].strip(b"\0"), f"{what}: bytes after the NUL are not zero", where)
    try:
        return name.decode("utf-8")
    except UnicodeDecodeError:
        raise ValidationError(f"{what} {name!r} is not valid UTF-8", where)


def name_bytes(name, where, what="name", allow_full=False):
    """str -> the char[16] field. Writer rule: 1..15 bytes + NUL (bmmesh.encode
    cuts at 15, scripts/bmmesh.py:37; bm-forge refuses instead of cutting). With
    allow_full a 16-byte name (no NUL) is kept, for rewriting a file a Lua tool
    wrote without changing it."""
    need(isinstance(name, str), f"{what} must be a string", where)
    b = name.encode("utf-8")
    need(b, f"{what} is empty", where)
    need(b"\0" not in b, f"{what} {name!r} holds a NUL", where)
    limit = NAME_FIELD if allow_full else NAME_MAX
    need(len(b) <= limit, f"{what} {name!r} is {len(b)} bytes, at most {limit}", where)
    return b.ljust(NAME_FIELD, b"\0")


def pad4(n):
    """zero bytes up to the next multiple of 4 (src/bm/format.c:939-952)"""
    return b"\0" * ((-n) % 4)
