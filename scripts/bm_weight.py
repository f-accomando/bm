#!/usr/bin/env python3
"""
bm_weight.py - dev kit: weight and performance plan of a .bm cartridge or a .bme project, and the compat policy.
Standard library only (Python 3.11+, tomllib).

  bm_weight.py FILE [--json] [--weight-only] [--policy docs/spec/compat-policy.toml] [--kernel ID ...]

FILE is a .bm, .b16 or .bme (one container, docs/BM-FORMAT.md). The report has three parts:

1. Weight: bytes per section and per asset (models one by one), biggest first, with the total and the percent.
2. Performance plan: data, not a measurement. The weight is combined with costs that the docs record from the
   benchmarks (SD speed, triangle limits per path). Every number says its source and "estimate". Hardware
   numbers come from the benchmarks (3D Bench, Dev > GPU test, Stack bench, Video bench): the report prints
   the command to run for the next measurement.
3. Compat policy: for each kernel, show, grey or hide, from the editable table docs/spec/compat-policy.toml.

Use as a module: parse(data), weight(info), plan(info, w), load_policy(path), classify(facts, kernel, policy).
"""
import argparse
import json
import os
import struct
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import bmmesh  # noqa: E402  (cart_sections reads the container; decode_anim reads the rigs)

POLICY_PATH = os.path.join(ROOT, "docs", "spec", "compat-policy.toml")

# section number -> (name, category); numbers from src/bm/bm.h (BM_SEC_*), bmmesh.cart_sections maps the old ones
SECTIONS = {
    1: ("LUA", "lua"), 2: ("SHEET", "sprites"), 3: ("MAP", "maps"), 4: ("COVER", "sprites"),
    5: ("SHEET8", "sprites"), 6: ("AUDIO", "audio"), 8: ("MESH", "models"), 9: ("ANIM", "models"),
    10: ("INFO", "meta"), 11: ("SPRITES", "sprites"), 12: ("LAYERS", "maps"), 13: ("FLAGS", "tiles"),
    14: ("BOXES", "sprites"),
}
CATEGORIES = ["sprites", "tiles", "maps", "audio", "models", "lua", "meta", "container"]

HEADER, ENTRY = 128, 16
MARKET_MAX = 100 << 20          # src/net/catalog.h CATALOG_MAX_FILE
SHEET_MAX = 4096                # docs/BM-FORMAT.md, SHEET w, h 1..4096
LOAD_WARN_S = 2.0               # kit default for "slow to load"; not a measured limit

# Costs the docs record. (value, source). The numbers are measurements of the doc, the results here are estimates.
COSTS = {
    "sd_read_mb_s": (5.14, "docs/progress/video.md, VID-S2, Pi Zero W, 22 Oct (measured)"),
    "sd_write_mb_s": (2.50, "docs/progress/video.md, VID-S1, Pi Zero W, 22 Oct (measured)"),
    "memcpy_mb_s": (100.0, "docs/PERFORMANCE.md, RAM bandwidth (measured)"),
    "jpeg_320x180_ms": (53, "docs/progress/video.md, VID-S2 (measured); for pictures and clips, not a cart section"),
    "jpeg_480x270_ms": (116, "docs/progress/video.md, VID-S2 (measured); for pictures and clips, not a cart section"),
    "tris60_low": (77780, "docs/design/triangle-throughput.md 1.1, row spheres, GPU+VS+FS2, queue off (measured)"),
    "tris60_high": (128282, "docs/design/triangle-throughput.md 1.1, row big A8 (measured)"),
    "tris60_arm": (1195, "docs/PERFORMANCE.md, Sept 2026 ARM rasterizer baseline (measured, old)"),
    "model_faces_max_measured": (10080, "docs/BENCH3D.md, scene big (largest model measured)"),
    "audio_pcm_max": (8388608, "src/audio/player.h AU_PCM_MAX (16-bit values of a bank's samples)"),
}

BENCHMARKS = {
    "models": ('3D Bench (Dev tab, or the monitor line): ./easy_install.sh line "gpu; b3d tests=herotris; send" '
               "(docs/BENCH3D.md, herotris)"),
    "stacks": "Stack bench (Dev tab, or the monitor line :stack): docs/progress/v3d.md",
    "maps": 'Scenes: ./easy_install.sh line "gpu; b3d tests=map8,sprites,stack,fade,light; send" (docs/BENCH3D.md)',
    "gpu": "Dev > GPU test (RGB30 variant probe: docs/progress/graphics-mali.md)",
    "storage": "Video bench (Dev tab, or the monitor line :video): SD read and write speed of the card, docs/progress/video.md",
}


def mb(n):
    return f"{n / (1 << 20):.2f} MiB"


def pct(part, whole):
    return round(100.0 * part / whole, 2) if whole else 0.0


def compat_of(data, flags):
    """the targets of a header, as bm_compat_read does: (declared, targets); an old .bm = Pi, an old .b16 = Pi + RGB30"""
    if flags & 16 and data[120] & 3:
        return True, data[120] & 3
    return False, 3 if flags & 2 else 1


def parse(data, name=""):
    """a .bm/.b16/.bme file -> {size, title, author, res, flags, edition, sections: [(type, body)], overhead}"""
    if len(data) < HEADER:
        raise ValueError("not a bm container: shorter than the 128-byte header")
    secs = bmmesh.cart_sections(data)        # raises ValueError for a wrong magic
    flags = struct.unpack_from("<H", data, 18)[0]
    w, h = struct.unpack_from("<HH", data, 12)
    body_sum = sum(len(b) for _, b in secs)
    edition = "project" if flags & 1 else ("b16" if flags & 2 else "bm")
    ext = os.path.splitext(name)[1].lower()       # the kernel tells the kind by the extension (docs/BM-FORMAT.md)
    edition = {".bme": "project", ".b16": "b16", ".bm": "bm"}.get(ext, edition)
    return {
        "size": len(data), "title": data[24:72].split(b"\0")[0].decode(errors="replace"),
        "author": data[72:104].split(b"\0")[0].decode(errors="replace"), "res": (w, h), "flags": flags,
        "edition": edition, "sections": secs, "overhead": len(data) - body_sum,
        "table_bytes": ENTRY * len(secs), "compat": compat_of(data, flags),
    }


def _u16(body, off):
    return struct.unpack_from("<H", body, off)[0] if len(body) >= off + 2 else 0


def mesh_models(body):
    """MESH body -> [(name, vertices, faces, bytes)]; only the counts are read"""
    out = []
    if len(body) < 8:
        return out
    count, off = _u16(body, 0), 8
    for _ in range(count):
        if off + bmmesh.NAME_LEN + 8 > len(body):
            break
        name = body[off:off + bmmesh.NAME_LEN].split(b"\0")[0].decode(errors="replace")
        nv, nf = struct.unpack_from("<HH", body, off + bmmesh.NAME_LEN)
        size = bmmesh.NAME_LEN + 8 + nv * 12 + nf * 24
        out.append((name, nv, nf, size))
        off += size
    return out


def _detail(typ, body):
    """a short text and a dict of facts for one section"""
    facts = {}
    if typ == 1:
        facts["lines"] = body.count(b"\n") + (1 if body and not body.endswith(b"\n") else 0)
        return f"{facts['lines']} lines", facts
    if typ in (2, 5):
        w, h = _u16(body, 0), _u16(body, 2)
        facts.update(w=w, h=h, ram_bytes=w * h * 2, raw_rgba_bytes=w * h * 4)
        txt = f"{w}x{h}, RGB565 in RAM {mb(w * h * 2)}"
        if typ == 5:
            facts["colours"] = _u16(body, 4)
            txt = f"{w}x{h}, {facts['colours']} colours, RGBA raw would be {mb(w * h * 4)}; RGB565 in RAM {mb(w * h * 2)}"
        return txt, facts
    if typ == 3:
        w, h = _u16(body, 0), _u16(body, 2)
        facts.update(w=w, h=h)
        return f"{w}x{h} cells", facts
    if typ == 12:
        facts.update(w=_u16(body, 0), h=_u16(body, 2), layers=_u16(body, 4))
        return f"{facts['w']}x{facts['h']}, {facts['layers']} layers (map counted)", facts
    if typ == 6:
        facts["version"] = _u16(body, 4)
        return f"sound bank BMAU, {len(body)} bytes", facts
    if typ == 8:
        models = mesh_models(body)
        facts["models"] = len(models)
        facts["faces"] = sum(m[2] for m in models)
        return f"{len(models)} models, {facts['faces']} faces", facts
    if typ == 9:
        try:
            rigs = bmmesh.decode_anim(body)
            facts["rigs"] = [{"name": r[0], "bones": len(r[1]), "clips": len(r[3])} for r in rigs]
            return f"{len(rigs)} rigs, {sum(len(r[3]) for r in rigs)} clips", facts
        except (struct.error, IndexError, ValueError):
            return "rigs not readable", facts
    if typ == 11:
        n = _u16(body, 0)
        stacks = 0
        for i in range(n):
            o = 4 + i * 28 + 26
            if o < len(body) and body[o] >= 2:
                stacks += 1
        facts.update(zones=n, stacks=stacks)
        return f"{n} zones, {stacks} stacks", facts
    if typ == 14:
        facts["boxes"] = _u16(body, 0)
        return f"{facts['boxes']} boxes", facts
    if typ == 13:
        return f"{_u16(body, 2)} rows", facts
    if typ == 4:
        return f"{_u16(body, 0)}x{_u16(body, 2)} cover", facts
    return "", facts


def weight(info):
    """-> dict: total, sections [{name, type, category, bytes, percent, detail}], categories, assets (all biggest first)"""
    total = info["size"]
    secs, assets = [], []
    for typ, body in info["sections"]:
        name, cat = SECTIONS.get(typ, (f"TYPE{typ}", "meta"))
        text, facts = _detail(typ, body)
        secs.append({"name": name, "type": typ, "category": cat, "bytes": len(body), "percent": pct(len(body), total),
                     "detail": text, "facts": facts})
        if typ == 8:
            for mname, nv, nf, size in mesh_models(body):
                assets.append({"section": name, "name": mname, "category": cat, "bytes": size,
                               "detail": f"{nv} vertices, {nf} faces", "faces": nf})
        else:
            assets.append({"section": name, "name": name.lower(), "category": cat, "bytes": len(body), "detail": text})
    pad = info["overhead"] - info["table_bytes"] - HEADER
    secs.append({"name": "(header+table+padding)", "type": None, "category": "container", "bytes": info["overhead"],
                 "percent": pct(info["overhead"], total), "detail": f"header {HEADER}, table {info['table_bytes']}, "
                 f"padding {max(pad, 0)}", "facts": {}})
    secs.sort(key=lambda s: -s["bytes"])
    assets.sort(key=lambda a: -a["bytes"])
    for a in assets:
        a["percent"] = pct(a["bytes"], total)
    cats = {c: 0 for c in CATEGORIES}
    for s in secs:
        cats[s["category"]] += s["bytes"]
    cat_list = sorted(({"category": c, "bytes": b, "percent": pct(b, total)} for c, b in cats.items() if b),
                      key=lambda x: -x["bytes"])
    return {"total": total, "sections": secs, "categories": cat_list, "assets": assets}


def plan(info, w):
    """the performance plan: estimates from the weight and the costs of the docs -> dict"""
    total = w["total"]
    est, flags = [], []

    def add(what, value, unit, basis, src):
        est.append({"what": what, "value": value, "unit": unit, "basis": basis, "source": src, "kind": "estimate"})

    rd, rd_src = COSTS["sd_read_mb_s"]
    wr, wr_src = COSTS["sd_write_mb_s"]
    cp, cp_src = COSTS["memcpy_mb_s"]
    read_s = total / (rd * 1e6)
    add("load: read the file from the SD (Pi Zero W)", round(read_s, 3), "s", f"{total} bytes / {rd} MB/s", rd_src)
    add("save or build: write the file to the SD (Pi Zero W)", round(total / (wr * 1e6), 3), "s",
        f"{total} bytes / {wr} MB/s", wr_src)
    sheets = [s for s in w["sections"] if s["name"] in ("SHEET", "SHEET8")]
    ram = total + sum(s["facts"].get("ram_bytes", 0) for s in sheets)
    add("RAM: whole file plus the sheet decoded to RGB565", ram, "bytes", "file loaded whole, sheet 2 bytes a pixel",
        "docs/design/single-bm-and-adaptive-screen.md 2.1 (whole file in RAM)")
    for s in sheets:
        add("one full copy of the decoded sheet", round(s["facts"].get("ram_bytes", 0) / (cp * 1e6) * 1000, 2), "ms",
            f"{s['facts'].get('ram_bytes', 0)} bytes / {cp} MB/s", cp_src)
    lo, lo_src = COSTS["tris60_low"]
    hi, hi_src = COSTS["tris60_high"]
    arm, arm_src = COSTS["tris60_arm"]
    mx = COSTS["model_faces_max_measured"][0]
    models = [a for a in w["assets"] if a["section"] == "MESH"]
    ranked = sorted(models, key=lambda a: -a["faces"])
    for a in ranked:
        if a["faces"] <= 0:
            continue
        if a is not ranked[0] and a is not ranked[1] and a is not ranked[2]:
            if a["faces"] > mx:
                flags.append(f"model {a['name']} has {a['faces']} faces, above the largest model measured ({mx})")
            continue
        add(f"model {a['name']}: copies per frame at 60 fps, GPU path (no game logic)",
            f"{lo // a['faces']} to {hi // a['faces']}", "copies", f"{a['faces']} faces, {lo} to {hi} triangles at 60 fps",
            lo_src)
        add(f"model {a['name']}: copies per frame at 60 fps, ARM rasterizer", arm // a["faces"], "copies",
            f"{a['faces']} faces, {arm} triangles at 60 fps", arm_src)
        if a["faces"] > mx:
            flags.append(f"model {a['name']} has {a['faces']} faces, above the largest model measured ({mx}): "
                         f"run 3D Bench with it first [{COSTS['model_faces_max_measured'][1]}]")
    if read_s > LOAD_WARN_S:
        flags.append(f"estimated load read {read_s:.1f} s is above {LOAD_WARN_S} s (kit default, not a measured limit)")
    if total > MARKET_MAX:
        flags.append(f"file is {mb(total)}, above the Market ceiling of {mb(MARKET_MAX)} (src/net/catalog.h)")
    for s in sheets:
        f = s["facts"]
        if f.get("w", 0) > SHEET_MAX or f.get("h", 0) > SHEET_MAX:
            flags.append(f"sheet {f.get('w')}x{f.get('h')} is above {SHEET_MAX} (the kernel refuses it) [docs/BM-FORMAT.md]")
    for s in w["sections"]:
        if s["name"] == "AUDIO" and s["bytes"] > COSTS["audio_pcm_max"][0] * 2:
            flags.append(f"audio {mb(s['bytes'])} may pass the PCM cap of {COSTS['audio_pcm_max'][0]} values "
                         f"[{COSTS['audio_pcm_max'][1]}]; the bank may hold other data: check with the sound tool")
    heavy = [a for a in w["assets"] if a["percent"] >= 50 and a["bytes"] >= 64 * 1024]
    for a in heavy:
        flags.append(f"{a['name']} is {a['percent']}% of the file ({mb(a['bytes'])})")
    cats = {s["category"] for s in w["sections"]}
    names = {s["name"] for s in w["sections"]}
    nxt = []
    if "models" in cats:
        nxt.append(BENCHMARKS["models"])
    if any(s["facts"].get("stacks") for s in w["sections"]):
        nxt.append(BENCHMARKS["stacks"])
    if cats & {"sprites", "maps", "tiles"} and names & {"SHEET", "SHEET8", "MAP"}:
        nxt.append(BENCHMARKS["maps"])
    nxt.append(BENCHMARKS["gpu"])
    nxt.append(BENCHMARKS["storage"])
    known = {k: {"value": v[0], "source": v[1]} for k, v in COSTS.items()}
    return {"note": "estimates from the costs recorded in the docs; no live game data", "estimates": est,
            "flags": flags, "next_benchmarks": nxt, "known_costs": known}


# --- compat policy -----------------------------------------------------------------------------------------

ACTIONS = ("show", "grey", "hide")
REASON_MAX = 31                 # "Not here: " and the reason must fit the 41 characters of the RGB30's details line
MATCH_KEYS = ("edition", "res", "uses_3d", "audio_bytes_over", "size_over_mib", "declared", "kernel_not_listed")


def load_policy(path=POLICY_PATH):
    """read and check the policy table -> {"kernels": {id: {...}}, "rules": [...]}; ValueError when it is wrong"""
    with open(path, "rb") as f:
        raw = tomllib.load(f)
    kernels = {}
    for k in raw.get("kernel", []):
        for key in ("id", "name", "target", "default_action", "default_reason"):
            if key not in k:
                raise ValueError(f"{path}: a [[kernel]] row lacks '{key}'")
        if k["default_action"] not in ACTIONS:
            raise ValueError(f"{path}: kernel {k['id']}: default_action must be one of {ACTIONS}")
        if k["target"] not in range(1, 256) or k["target"] & (k["target"] - 1):
            raise ValueError(f"{path}: kernel {k['id']}: target must be one bit (1, 2, 4 ...)")
        if len(k["default_reason"]) > REASON_MAX:
            raise ValueError(f"{path}: kernel {k['id']}: default_reason is longer than {REASON_MAX} characters")
        if k["id"] in kernels:
            raise ValueError(f"{path}: kernel {k['id']} is listed twice")
        kernels[k["id"]] = k
    if not kernels:
        raise ValueError(f"{path}: no [[kernel]] row")
    rules = raw.get("rule", [])
    for i, r in enumerate(rules):
        for key in ("kernel", "action", "reason"):
            if key not in r:
                raise ValueError(f"{path}: rule {i + 1} lacks '{key}'")
        if r["kernel"] not in kernels:
            raise ValueError(f"{path}: rule {i + 1}: unknown kernel {r['kernel']!r}")
        for key in ("action", "market_action"):
            if key in r and r[key] not in ACTIONS:
                raise ValueError(f"{path}: rule {i + 1}: {key} must be one of {ACTIONS}")
        if len(r["reason"]) > REASON_MAX:
            raise ValueError(f"{path}: rule {i + 1}: reason is longer than {REASON_MAX} characters")
        for key in ("uses_3d", "declared", "kernel_not_listed"):
            if key in r and not isinstance(r[key], bool):
                raise ValueError(f"{path}: rule {i + 1}: {key} must be true or false")
        unknown = set(r) - {"kernel", "action", "market_action", "reason"} - set(MATCH_KEYS)
        if unknown:
            raise ValueError(f"{path}: rule {i + 1}: unknown keys {sorted(unknown)}")
    return {"kernels": kernels, "rules": rules}


def facts_of(info, w):
    """the facts a rule can match, from a parsed cart"""
    audio = sum(s["bytes"] for s in w["sections"] if s["name"] == "AUDIO")
    return {"edition": info["edition"], "res": "%dx%d" % info["res"], "uses_3d": any(s["name"] == "MESH" for s in w["sections"]),
            "audio_bytes": audio, "size_bytes": info["size"],
            "declared": info["compat"][0], "targets": info["compat"][1]}


def _matches(rule, facts, kbit=0):
    if "edition" in rule and facts["edition"] != rule["edition"]:
        return False
    if "res" in rule and facts["res"] not in rule["res"]:
        return False
    if "uses_3d" in rule and facts["uses_3d"] != rule["uses_3d"]:
        return False
    if "audio_bytes_over" in rule and not facts["audio_bytes"] > rule["audio_bytes_over"]:
        return False
    if "size_over_mib" in rule and not facts["size_bytes"] > rule["size_over_mib"] * (1 << 20):
        return False
    if "declared" in rule and facts.get("declared", False) != rule["declared"]:
        return False
    if "kernel_not_listed" in rule:
        not_listed = bool(facts.get("declared", False)) and not facts.get("targets", 0) & kbit
        if not_listed != rule["kernel_not_listed"]:
            return False
    return True


def classify(facts, kernel, policy, where="menu"):
    """-> (action, reason, row) for one kernel; where is "menu" or "market". The first matching rule wins."""
    if kernel not in policy["kernels"]:
        raise ValueError(f"unknown kernel {kernel!r}")
    kbit = policy["kernels"][kernel]["target"]
    for i, r in enumerate(policy["rules"]):
        if r["kernel"] == kernel and _matches(r, facts, kbit):
            act = r.get("market_action", r["action"]) if where == "market" else r["action"]
            return act, r["reason"], i + 1
    k = policy["kernels"][kernel]
    return k["default_action"], k["default_reason"], 0


# --- output ------------------------------------------------------------------------------------------------

def report(path, info, w, p, pol, kernels):
    L = []
    L.append(f"bm_weight: {path}  [{info['edition']}, {info['res'][0]}x{info['res'][1]}] "
             f"{info['title']!r} by {info['author']!r}")
    L.append(f"total {w['total']:,} bytes ({mb(w['total'])}), Market ceiling {mb(MARKET_MAX)}: {pct(w['total'], MARKET_MAX)}% used")
    L.append("")
    L.append("Sections, biggest first")
    for s in w["sections"]:
        L.append(f"  {s['name']:<22}{s['bytes']:>12,}  {s['percent']:>6.2f}%  {s['detail']}")
    L.append("")
    L.append("By kind: " + ", ".join(f"{c['category']} {c['percent']}%" for c in w["categories"]))
    L.append("")
    L.append("Assets, biggest first (models one by one)")
    for a in w["assets"][:25]:
        L.append(f"  {a['section']:<8}{a['name']:<18}{a['bytes']:>12,}  {a['percent']:>6.2f}%  {a['detail']}")
    if len(w["assets"]) > 25:
        L.append(f"  ... {len(w['assets']) - 25} more (use --json)")
    if p:
        L.append("")
        L.append("Performance plan (estimates from the docs, not measurements; no live game data)")
        for e in p["estimates"]:
            L.append(f"  {e['what']}: {e['value']} {e['unit']}  [estimate: {e['basis']}; source {e['source']}]")
        if p["flags"]:
            L.append("Flags")
            L.extend(f"  - {f}" for f in p["flags"])
        else:
            L.append("Flags: none")
        L.append("Next measurement on the hardware")
        L.extend(f"  - {b}" for b in p["next_benchmarks"])
    if pol:
        L.append("")
        L.append("Compat policy (docs/spec/compat-policy.toml)")
        facts = facts_of(info, w)
        for k in kernels:
            a, why, row = classify(facts, k, pol)
            m, mwhy, _ = classify(facts, k, pol, "market")
            L.append(f"  {k}: menu {a}, market {m}  (row {row}: {why})")
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help=".bm, .b16 or .bme")
    ap.add_argument("--json", action="store_true", help="print one JSON object")
    ap.add_argument("--weight-only", action="store_true", help="no performance plan, no compat policy")
    ap.add_argument("--policy", default=POLICY_PATH, help="compat policy table (TOML)")
    ap.add_argument("--kernel", action="append", help="kernel id to classify for (default: all of the table)")
    a = ap.parse_args(argv)
    try:
        with open(a.file, "rb") as f:
            data = f.read()
        info = parse(data, a.file)
        w = weight(info)
        p = None if a.weight_only else plan(info, w)
        pol = None if a.weight_only else load_policy(a.policy)
    except (OSError, ValueError) as e:
        print(f"bm_weight: {e}", file=sys.stderr)
        return 1
    kernels = a.kernel or (list(pol["kernels"]) if pol else [])
    if pol:
        for k in kernels:
            if k not in pol["kernels"]:
                print(f"bm_weight: unknown kernel {k!r}; the table has {', '.join(pol['kernels'])}", file=sys.stderr)
                return 1
    if a.json:
        out = {"file": a.file, "edition": info["edition"], "title": info["title"], "author": info["author"],
               "res": list(info["res"]), "weight": {k: v for k, v in w.items()}}
        if p:
            out["plan"] = p
        if pol:
            facts = facts_of(info, w)
            out["compat"] = {k: dict(zip(("menu", "reason", "row"), classify(facts, k, pol)),
                                      market=classify(facts, k, pol, "market")[0]) for k in kernels}
        for s in out["weight"]["sections"]:
            s.pop("facts", None)
        print(json.dumps(out, indent=2))
    else:
        print(report(a.file, info, w, p, pol, kernels))
    return 0


if __name__ == "__main__":
    sys.exit(main())
