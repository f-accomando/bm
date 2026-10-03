#!/usr/bin/env python3
"""make test-hyp: tests/boot/hyp_main.c with kernel7.img's start.S in QEMU's
virt machine with the virtualization extensions (it starts in Hyp mode,
like the Pi Zero 2 W's firmware); the result comes on the serial port.

  run_hyp.py QEMU hyp.elf
"""
import subprocess
import sys


def main():
    qemu, elf = sys.argv[1], sys.argv[2]
    p = subprocess.Popen([qemu, "-M", "virt,virtualization=on", "-cpu", "cortex-a7", "-m", "256",
                          "-display", "none", "-monitor", "none", "-serial", "stdio", "-nic", "none",
                          "-kernel", elf], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = b""
    try:
        out, _ = p.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
    text = out.decode(errors="replace")
    sys.stdout.write(text)
    if "hyp test: all passed" not in text:
        print("test-hyp: FAILED")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
