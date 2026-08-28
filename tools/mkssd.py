#!/usr/bin/env python3
#
# frank-micro — BBC Micro for RP2350
#
# Copyright (c) 2026 Andrew de Quincey <adq@lidskialf.net>
# https://github.com/rh1tech/frank-micro
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Build an Acorn DFS .ssd disc image from files on disk.

frank-micro mounts disc images read-only, so the way to get a test program
onto the machine is to build an image here and mount it with F11.  Text files
are converted to BBC line endings and are meant to be run with *EXEC, which
feeds them to the command line as though they had been typed.

  ./tools/mkssd.py out.ssd --title TESTS --text prog.bas
  ./tools/mkssd.py out.ssd --title TESTS --text prog.bas:MYPROG

A name after a colon overrides the DFS name, which is otherwise the file's
stem uppercased and cut to 7 characters.  --boot sets the *OPT4 option so
SHIFT-BREAK runs !BOOT; mounting into drive 0 from the F11 browser triggers a
SHIFT-BREAK on its own, so a disc with a !BOOT file starts by itself.

Format reference: Acorn DFS keeps a two-sector catalogue at the start of the
disc.  Sector 0 holds the first 8 title characters and 31 filename entries of
8 bytes; sector 1 holds the rest of the title, the file count, the disc size,
the boot option, and 31 matching entries of addresses and lengths.  The high
bits of each 18-bit address and each 10-bit sector count are packed into one
byte per entry, which is the only fiddly part.
"""

import argparse
import os
import sys

SECTOR = 256
SECTORS_PER_TRACK = 10
TRACKS = 80
TOTAL_SECTORS = SECTORS_PER_TRACK * TRACKS   # 800 sectors, 200K single sided
CATALOGUE_SECTORS = 2
MAX_FILES = 31


def dfs_name(path, override=None):
    """DFS filenames are 7 characters, uppercase, in a single-letter directory."""
    if override:
        name = override.upper()
    else:
        name = os.path.splitext(os.path.basename(path))[0].upper()
    name = "".join(c for c in name if 32 < ord(c) < 127 and c not in '.:*#"')
    if not name:
        raise SystemExit(f"{path}: nothing usable left in the name")
    return name[:7]


def to_bbc_text(data):
    """BBC line endings are CR.  Accepts LF or CRLF input."""
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r")


def build(entries, title, boot_option):
    if len(entries) > MAX_FILES:
        raise SystemExit(f"DFS holds {MAX_FILES} files, got {len(entries)}")

    image = bytearray(b"\x00" * (TOTAL_SECTORS * SECTOR))
    sector = CATALOGUE_SECTORS
    placed = []

    for name, load, exec_, data in entries:
        length = len(data)
        need = (length + SECTOR - 1) // SECTOR
        if sector + need > TOTAL_SECTORS:
            raise SystemExit("disc full")
        off = sector * SECTOR
        image[off:off + length] = data
        placed.append((name, load, exec_, length, sector))
        sector += need

    title = (title.upper() + " " * 12)[:12].encode("ascii")
    image[0:8] = title[0:8]
    image[SECTOR + 0:SECTOR + 4] = title[8:12]
    image[SECTOR + 4] = 0                       # cycle number
    image[SECTOR + 5] = len(placed) * 8         # catalogue entries, in bytes
    # Boot option in bits 4-5, top two bits of the sector count in bits 0-1.
    image[SECTOR + 6] = ((boot_option & 3) << 4) | ((TOTAL_SECTORS >> 8) & 3)
    image[SECTOR + 7] = TOTAL_SECTORS & 0xFF

    for i, (name, load, exec_, length, start) in enumerate(placed):
        e0 = 8 + i * 8            # sector 0: name and directory
        e1 = SECTOR + 8 + i * 8   # sector 1: addresses, length, start sector

        padded = (name + " " * 7)[:7]
        image[e0:e0 + 7] = padded.encode("ascii")
        image[e0 + 7] = ord("$")   # directory; bit 7 would mark it locked

        image[e1 + 0] = load & 0xFF
        image[e1 + 1] = (load >> 8) & 0xFF
        image[e1 + 2] = exec_ & 0xFF
        image[e1 + 3] = (exec_ >> 8) & 0xFF
        image[e1 + 4] = length & 0xFF
        image[e1 + 5] = (length >> 8) & 0xFF
        # One packing byte: bits 0-1 start sector, 2-3 load, 4-5 length,
        # 6-7 exec, each holding the bits that did not fit above.
        image[e1 + 6] = (((start >> 8) & 0x03)
                         | (((load >> 16) & 0x03) << 2)
                         | (((length >> 16) & 0x03) << 4)
                         | (((exec_ >> 16) & 0x03) << 6))
        image[e1 + 7] = start & 0xFF

    return bytes(image)


def read_catalogue(image):
    """Read back what build() wrote, so the caller can check it."""
    count = image[SECTOR + 5] // 8
    out = []
    for i in range(count):
        e0 = 8 + i * 8
        e1 = SECTOR + 8 + i * 8
        name = image[e0:e0 + 7].decode("ascii").rstrip()
        packed = image[e1 + 6]
        length = image[e1 + 4] | (image[e1 + 5] << 8) | ((packed >> 4 & 3) << 16)
        start = image[e1 + 7] | ((packed & 3) << 8)
        out.append((name, length, start))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output")
    ap.add_argument("--title", default="FRANK", help="disc title, 12 chars")
    ap.add_argument("--text", action="append", default=[], metavar="FILE[:NAME]",
                    help="add a text file, converted to CR line endings")
    ap.add_argument("--binary", action="append", default=[], metavar="FILE[:NAME]",
                    help="add a file verbatim")
    ap.add_argument("--load", default="0000", help="load address, hex")
    ap.add_argument("--exec", dest="exec_", default="0000", help="exec address, hex")
    ap.add_argument("--boot", type=int, default=0, choices=[0, 1, 2, 3],
                    help="*OPT4 boot option; 3 runs *EXEC !BOOT on SHIFT-BREAK")
    args = ap.parse_args()

    load = int(args.load, 16)
    exec_ = int(args.exec_, 16)

    entries = []
    for spec, is_text in [(s, True) for s in args.text] + [(s, False) for s in args.binary]:
        path, _, override = spec.partition(":")
        with open(path, "rb") as fh:
            data = fh.read()
        if is_text:
            data = to_bbc_text(data)
        entries.append((dfs_name(path, override or None), load, exec_, data))

    if not entries:
        raise SystemExit("nothing to add; pass --text or --binary")

    image = build(entries, args.title, args.boot)
    with open(args.output, "wb") as fh:
        fh.write(image)

    print(f"{args.output}: {len(image)} bytes, boot option {args.boot}")
    for name, length, start in read_catalogue(image):
        print(f"  $.{name:<7} {length:6d} bytes at sector {start}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
