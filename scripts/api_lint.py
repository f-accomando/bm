#!/usr/bin/env python3
"""Linter for unknown API calls in Lua code and markdown docs."""
import argparse
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

LUA_STD = {
    "assert", "error", "ipairs", "pairs", "next", "pcall", "xpcall", "select",
    "tonumber", "tostring", "type", "unpack", "rawget", "rawset", "rawequal",
    "rawlen", "setmetatable", "getmetatable", "require", "collectgarbage",
    "print", "string", "table", "math", "coroutine", "utf8"
}

KEYWORDS = {
    "function", "if", "while", "until", "return", "and", "or", "not",
    "elseif", "in", "local"
}

LUA_STRIP_RE = re.compile(
    r"--\[(=*)\[.*?\]\1\]"   # block comment
    r"|--[^\n]*"             # line comment
    r"|\[(=*)\[.*?\]\2\]"    # block string
    r'|"(?:\\.|[^"\\])*"'    # double quoted string
    r"|'(?:\\.|[^'\\])*'",   # single quoted string
    re.S,
)

CALL_RE = re.compile(r"(?<![\w.:])([A-Za-z_]\w*)\s*\(")


def load_api_tsv(path=None):
    if path is None:
        path = os.path.join(ROOT, "src", "ai", "api.tsv")
    names = set()
    if not os.path.exists(path):
        return names
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split("\t")
            if not parts:
                continue
            name = parts[0].strip()
            # If name has . or :, take the first segment as well
            if "." in name:
                names.add(name.split(".")[0])
            elif ":" in name:
                names.add(name.split(":")[0])
            else:
                names.add(name)
    return names


def load_allow_list(path=None):
    if path is None:
        path = os.path.join(ROOT, "scripts", "api_lint_allow.txt")
    allowed = set()
    if not os.path.exists(path):
        return allowed
    with open(path, "r", encoding="utf-8") as f:
        for lno, line in enumerate(f, 1):
            line_str = line.strip()
            if not line_str or line_str.startswith("#"):
                continue
            if "#" not in line:
                # "# motivo" obbligatorio
                sys.stderr.write(f"Warning: {path}:{lno}: missing '# reason'\n")
            name = line.split("#")[0].strip()
            if name:
                allowed.add(name)
    return allowed


def mask_markdown(text):
    lines = text.splitlines(keepends=True)
    in_lua = False
    result = []
    for line in lines:
        stripped = line.strip()
        if not in_lua:
            if stripped.startswith("```lua"):
                in_lua = True
                result.append("\n")
            else:
                result.append("\n")
        else:
            if stripped.startswith("```"):
                in_lua = False
                result.append("\n")
            else:
                result.append(line)
    return "".join(result)


def strip_comments_and_strings(text):
    def repl(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return LUA_STRIP_RE.sub(repl, text)


def extract_local_definitions(clean_text):
    defs = set()

    # function N, local function N, function tbl:N, function tbl.N
    for m in re.finditer(r"\bfunction\s+(?:([A-Za-z_]\w*)[.:])?([A-Za-z_]\w*)", clean_text):
        if m.group(1):
            defs.add(m.group(1))
        if m.group(2):
            defs.add(m.group(2))

    # local N[, M] = or local N[, M]
    for m in re.finditer(r"\blocal\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)", clean_text):
        for item in m.group(1).split(","):
            var_name = item.strip()
            if var_name:
                defs.add(var_name)

    # N = function
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*=\s*function\b", clean_text):
        defs.add(m.group(1))

    # parametri di function x(a, b) o function(a, b)
    for m in re.finditer(r"\bfunction(?:\s+[A-Za-z_]\w*(?:[.:][A-Za-z_]\w*)*)?\s*\(([^)]*)\)", clean_text):
        params = m.group(1)
        for p in params.split(","):
            p_name = p.strip()
            if p_name and p_name != "...":
                defs.add(p_name)

    # variabili di for N[, M] in ...
    for m in re.finditer(r"\bfor\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s+in\b", clean_text):
        for item in m.group(1).split(","):
            var_name = item.strip()
            if var_name:
                defs.add(var_name)

    # for N = ...
    for m in re.finditer(r"\bfor\s+([A-Za-z_]\w*)\s*=", clean_text):
        defs.add(m.group(1))

    return defs


def lint_file(fpath, base_known_names):
    if not os.path.isabs(fpath) and not os.path.exists(fpath):
        candidate = os.path.join(ROOT, fpath)
        if os.path.exists(candidate):
            fpath = candidate
    try:
        with open(fpath, "r", encoding="utf-8") as f:
            raw_text = f.read()
    except Exception as e:
        sys.stderr.write(f"Cannot read {fpath}: {e}\n")
        return None

    is_md = fpath.lower().endswith(".md")
    code_text = mask_markdown(raw_text) if is_md else raw_text
    clean_text = strip_comments_and_strings(code_text)

    local_defs = extract_local_definitions(clean_text)
    known = base_known_names | local_defs

    try:
        rel_path = os.path.relpath(fpath, ROOT).replace("\\", "/")
    except ValueError:
        rel_path = fpath.replace("\\", "/")

    issues = []
    lines = clean_text.splitlines()
    for lno, line in enumerate(lines, 1):
        for m in CALL_RE.finditer(line):
            name = m.group(1)
            if name not in known:
                issues.append(f"{rel_path}:{lno}: unknown API name '{name}'")
    return issues


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser(description="Lint unknown API calls in Lua / docs")
    parser.add_argument("files", nargs="+", help="Files to lint")
    parser.add_argument("--api-tsv", default=None, help="Path to api.tsv")
    parser.add_argument("--allow-file", default=None, help="Path to api_lint_allow.txt")
    args = parser.parse_args(argv)

    api_names = load_api_tsv(args.api_tsv)
    allowed_names = load_allow_list(args.allow_file)

    base_known = api_names | LUA_STD | KEYWORDS | allowed_names

    all_issues = []
    read_err = False

    for fpath in args.files:
        issues = lint_file(fpath, base_known)
        if issues is None:
            read_err = True
        else:
            all_issues.extend(issues)

    if read_err:
        return 2

    for issue in all_issues:
        print(issue)

    return 1 if all_issues else 0


if __name__ == "__main__":
    sys.exit(main())
