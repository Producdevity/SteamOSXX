#!/usr/bin/env python3
"""Finds the DRAM parameter block in an Allwinner boot0 image (eGON.BT0) and prints it.

The block starts with dram_clk (MHz) followed by dram_type (3 DDR3, 4 DDR4, 7 LPDDR3, 8 LPDDR4),
then zq, odt_en, para1, para2, mr0..mr3, tpr0..tpr13 (H616-family layout).
usage: boot0-dram.py boot0.bin
"""
import struct
import sys

TYPES = {3: "DDR3", 4: "DDR4", 7: "LPDDR3", 8: "LPDDR4"}
NAMES = ["clk", "type", "zq", "odt_en", "para1", "para2", "mr0", "mr1", "mr2", "mr3", "mr4", "mr5",
         "mr6", "mr11", "mr12", "mr13", "mr14", "mr16", "mr17", "mr22", "tpr0", "tpr1", "tpr2", "tpr3",
         "tpr6", "tpr10", "tpr11", "tpr12", "tpr13", "tpr14"]

data = open(sys.argv[1], "rb").read()
assert data[4:12] == b"eGON.BT0", "not a boot0 image"
words = struct.unpack_from("<%dI" % (min(len(data), 0x800) // 4), data)
for i in range(len(words) - 2):
    if 300 <= words[i] <= 1600 and words[i + 1] in TYPES:
        print(f"dram_para at offset 0x{i*4:x}: type {TYPES[words[i+1]]}, clk {words[i]} MHz")
        for n, w in zip(NAMES, words[i:i + len(NAMES)]):
            print(f"  {n:7s} 0x{w:08x} ({w})")
        break
else:
    print("no dram_para block found")
