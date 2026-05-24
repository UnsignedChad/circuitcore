#!/usr/bin/env python3
"""Run static IR drop on every .kicad_pcb under a directory tree.

Useful for regression testing the parser + mesher + solver chain against
many real boards. Prints a single line per board: PASS / FAIL / SKIP plus
the chosen (net, layer) and key result numbers.

Usage:
    examples/sweep_all_demos.py /path/to/kicad/demos
"""

import os
import re
import sys
import subprocess
import time
from pathlib import Path

PDNKIT = (Path(__file__).resolve().parent.parent / "build" / "pdnkit").resolve()
NETS_TO_TRY = ["GND", "+3V3", "+3.3V", "+5V", "VCC", "VDD", "+12V", "VBUS"]
LAYERS_TO_TRY = ["F.Cu", "B.Cu"]
TIMEOUT_S = 120


def parse_nets_from_stderr(text: str) -> set[str]:
    nets = set()
    for line in text.splitlines():
        m = re.match(r"\s+#(\d+)\s+(.*)", line)
        if m:
            name = m.group(2).strip()
            if name:
                nets.add(name)
    return nets


def try_one(pcb: Path, net: str, layer: str) -> tuple[int, float, str]:
    t0 = time.time()
    try:
        r = subprocess.run(
            [str(PDNKIT), "--analyze",
             "--net", net, "--layer", layer,
             "--cell-size", "0.5", str(pcb)],
            capture_output=True, text=True, timeout=TIMEOUT_S,
        )
    except subprocess.TimeoutExpired:
        return -1, TIMEOUT_S, "timeout"
    dt = time.time() - t0
    return r.returncode, dt, r.stdout.strip()


def main(root: str) -> int:
    if not PDNKIT.exists():
        print(f"pdnkit binary not found at {PDNKIT} -- build first", file=sys.stderr)
        return 1
    boards = sorted(Path(root).rglob("*.kicad_pcb"))
    if not boards:
        print(f"no .kicad_pcb files under {root}", file=sys.stderr)
        return 1

    pass_count = fail_count = skip_count = 0
    for pcb in boards:
        name = str(pcb.relative_to(root))
        # Probe to discover net names.
        probe = subprocess.run(
            [str(PDNKIT), "--analyze", "--net", "__probe__", str(pcb)],
            capture_output=True, text=True, timeout=TIMEOUT_S,
        )
        if probe.returncode == 2:
            print(f"FAIL {name:<60} parse")
            fail_count += 1
            continue
        avail = parse_nets_from_stderr(probe.stderr)
        candidates = [n for n in NETS_TO_TRY if n in avail]
        if not candidates:
            print(f"SKIP {name:<60} no power-rail nets")
            skip_count += 1
            continue
        win = None
        for net in candidates[:4]:
            for layer in LAYERS_TO_TRY:
                rc, dt, out = try_one(pcb, net, layer)
                if rc == 0:
                    win = (net, layer, dt, out)
                    break
            if win:
                break
        if win:
            net, layer, dt, out = win
            m = re.search(r"nodes=(\d+).*?drop=([\d.\-e]+)mV", out)
            extra = f"{m.group(1)}nd {float(m.group(2)):.2f}mV" if m else ""
            print(f"PASS {name:<60} {net:<8} {layer:<6} {extra:>20}  {dt:.2f}s")
            pass_count += 1
        else:
            print(f"FAIL {name:<60} mesh empty on tried nets")
            fail_count += 1

    total = pass_count + fail_count + skip_count
    print(f"\nPASS {pass_count}/{total}   FAIL {fail_count}/{total}   "
          f"SKIP {skip_count}/{total}")
    return 0 if fail_count == 0 else 2


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: sweep_all_demos.py <demo-tree-root>", file=sys.stderr)
        sys.exit(1)
    sys.exit(main(sys.argv[1]))
