#!/usr/bin/env python3
"""Pulls the built-in initramfs out of an arm64 kernel Image.

ROCKNIX builds its kernel with CONFIG_INITRAMFS_SOURCE, so the Image carries a gzip-compressed newc cpio archive.
The rebuilt kernel needs the same archive (its init does the early device setup this boot chain relies on), and
ROCKNIX's build tree that made it is not available here. Usage: extract-initramfs.py <Image> <out.cpio.gz>
"""
import sys
import zlib

image = open(sys.argv[1], "rb").read()
best = None
pos = 0
while True:
    pos = image.find(b"\x1f\x8b\x08", pos)
    if pos < 0:
        break
    d = zlib.decompressobj(31)
    try:
        head = d.decompress(image[pos:pos + 4096], 6)
    except zlib.error:
        pos += 1
        continue
    if head.startswith(b"070701"):
        # the whole stream: decompress to its end to learn the compressed length
        d = zlib.decompressobj(31)
        out = d.decompress(image[pos:])
        used = len(image) - pos - len(d.unused_data)
        if best is None or len(out) > best[2]:
            best = (pos, used, len(out))
        break
    pos += 1
if best is None:
    sys.exit("no gzip cpio archive found in " + sys.argv[1])
pos, used, size = best
open(sys.argv[2], "wb").write(image[pos:pos + used])
print(f"initramfs: offset {pos}, {used} bytes compressed, {size} bytes cpio")
