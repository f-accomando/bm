#!/usr/bin/env python3
"""
Extracts the RTL8821C register tables (MAC, AGC, AGC BTG type 2, BB, RF
path A) from Linux's rtw88/rtw8821c_table.c (GPL-2.0 OR BSD-3-Clause) into
src/rgb30/rtw8821c_table.c, unchanged, under BSD-3-Clause.

  scripts/rtw_tables.py LINUX/drivers/net/wireless/realtek/rtw88/rtw8821c_table.c
"""
import os
import sys

TABLES = ("rtw8821c_mac", "rtw8821c_agc", "rtw8821c_agc_btg_type2", "rtw8821c_bb", "rtw8821c_rf_a")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "rgb30", "rtw8821c_table.c")


def main():
    lines = open(sys.argv[1]).read().split("\n")
    old = open(OUT).read().split("\n")
    header = old[:old.index('#include "rtw_phy.h"') + 1]
    out = list(header) + [""]
    for name in TABLES:
        start = lines.index("static const u32 %s[] = {" % name)
        end = start
        while lines[end].strip() != "};":
            end += 1
        out.append("const uint32_t %s[] = {" % name)
        out += lines[start + 1:end]
        out += ["};", "const unsigned %s_len = sizeof %s / sizeof %s[0];" % (name, name, name), ""]
    out.append("#endif")
    open(OUT, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
