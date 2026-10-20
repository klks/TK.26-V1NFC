#!/usr/bin/env python3
"""Read the SDK-protected flash of the reader's PY32F002B through its own code.

    python tools/dump_flash_gadget.py [<start> <end> [<out.bin>]]

    defaults: 0x08000000 0x08006000 (all 24 KB) -> notes/dumps/firmware_PY32F002B_redump.bin
    e.g.      python tools/dump_flash_gadget.py 0x08005000 0x08006000 logs/sector5.bin

The PY32F002B's flash is SDK-protected end to end (FLASH_SDKR = 0x0B00: 0x08000000-
0x08005FFF), so the debugger cannot read it, but code running from flash can. The firmware
has `ldr r0,[r0]` at 0x08000526: put the address in R0, PC on the gadget, an FPB breakpoint on
0x08000528, run one instruction, read R0. notes/how_to_dump.md explains it in full.

READ-ONLY. It halts the core, sets PRIMASK, arms FPB comparators and single-steps the gadget;
nothing is written to flash or option bytes. When it is done it disarms the FPB and clears
both comparators, and leaves the core HALTED - power-cycle the reader afterwards. (Before
2026-10-18 the FPB was left armed; the reader then sat in a half-run state with SysTick off
and no clocks enabled until a full power cycle - see notes/how_to_dump.md.)

Why `-device Cortex-M0+` and not `PY32F002BX5`: with a device that has a flash definition,
J-Link may use flash breakpoints (which rewrite flash) and runs device-specific connect
scripts. The generic core gives plain SWD access only, which is what the original reader
must get.

Two quirks, both from the original dump:
  * each address is queried twice (the result lags one query); the value that is not the
    address itself is the flash word;
  * 0x08000000 reads back as 0 through the gadget; it is taken from a plain debugger read
    instead (the vector table base is debugger-readable).
Verify any dump with a second pass (or a second gadget) before trusting it - see
notes/swd-device-identification.md for the gadget that looked right and was 67% wrong.
"""
import collections
import os
import re
import struct
import subprocess
import sys

JLINK = r"L:\LocalDev\compilers\JLink_V842\JLink.exe"
FLASH_BASE = 0x08000000
FLASH_END = 0x08006000  # PY32F002B: 24 KB
GADGET = 0x08000526
BP0 = "0x48000529"
BP1 = "0x88000529"
CHUNK = 128
DEFAULT_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "notes", "dumps",
                           "firmware_PY32F002B_redump.bin")


def jlink(cmds, workdir):
    path = os.path.join(workdir, "_dump.jlink")
    with open(path, "w") as f:
        f.write("\n".join(cmds) + "\nexit\n")
    r = subprocess.run([JLINK, "-NoGui", "1", "-device", "Cortex-M0+", "-if", "SWD", "-speed", "4000",
                        "-autoconnect", "1", "-CommandFile", path],
                       capture_output=True, text=True, timeout=900)
    return r.stdout


def read_chunk(start, n, workdir):
    cmds = ["connect", "halt", "wreg PRIMASK 1", "w4 0xE0002000 0x00000003",
            "w4 0xE0002008 %s" % BP0, "w4 0xE000200C %s" % BP1]
    addrs = [start + 4 * i for i in range(n)]
    for t in addrs:
        for _ in range(2):
            cmds += ["wreg R0 0x%08X" % t, "SetPC 0x%08X" % GADGET, "go", "sleep 2", "halt"]
        cmds += ["mem32 0xE000ED00 1"]  # delimiter after each address
    out = jlink(cmds, workdir)
    parts = out.split("E000ED00 = ")
    res = {}
    for i, t in enumerate(addrs):
        if i >= len(parts):
            break
        r0s = re.findall(r"R0 = ([0-9A-F]{8})", parts[i])
        others = [v for v in r0s if int(v, 16) != t]
        if others:
            res[t] = collections.Counter(others).most_common(1)[0][0]
        elif r0s:
            res[t] = "%08X" % t
    return res


def disarm_fpb(workdir):
    # Clear both comparators, then FP_CTRL = KEY with ENABLE = 0.
    jlink(["connect", "halt", "w4 0xE0002008 0x00000000", "w4 0xE000200C 0x00000000",
           "w4 0xE0002000 0x00000002"], workdir)


def read_word0(workdir):
    out = jlink(["connect", "mem32 0x08000000 1"], workdir)
    m = re.search(r"08000000 = ([0-9A-F]{8})", out)
    return m.group(1) if m else None


def main():
    start = int(sys.argv[1], 0) if len(sys.argv) > 1 else FLASH_BASE
    end = int(sys.argv[2], 0) if len(sys.argv) > 2 else FLASH_END
    out_path = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUT
    if not (FLASH_BASE <= start < end <= FLASH_END) or start % 4 or end % 4:
        sys.exit("range must be word-aligned inside the PY32F002B's 24 KB flash 0x%08X-0x%08X"
                 % (FLASH_BASE, FLASH_END))
    workdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "logs")
    os.makedirs(workdir, exist_ok=True)
    mem = {}
    a = start
    while a < end:
        n = min(CHUNK, (end - a) // 4)
        r = read_chunk(a, n, workdir)
        mem.update(r)
        sys.stderr.write("  0x%08X: %d/%d (total %d)\n" % (a, len(r), n, len(mem)))
        a += CHUNK * 4
    if start == FLASH_BASE:
        w0 = read_word0(workdir)
        if w0:
            mem[FLASH_BASE] = w0
            sys.stderr.write("  0x08000000 from a plain debugger read: %s\n" % w0)
        else:
            sys.stderr.write("  WARNING: could not read 0x08000000 directly; it will be wrong (gadget reads 0)\n")
    disarm_fpb(workdir)
    out = bytearray(end - start)
    missing = 0
    for off in range(0, end - start, 4):
        v = mem.get(start + off)
        if v:
            struct.pack_into("<I", out, off, int(v, 16))
        else:
            missing += 1
    with open(out_path, "wb") as f:
        f.write(out)
    print("done: %d bytes -> %s; missing=%d" % (len(out), out_path, missing))


main()
