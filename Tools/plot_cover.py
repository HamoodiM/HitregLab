"""Plots the behind-cover deaths sweep -> docs/media/cover_deaths.png.

Input: docs/data/cover_sweep.csv (Saved/Measure/cover_*.csv from "-CoverSweep=N") and, if present,
docs/data/hitreg_rules.csv (Saved/Measure/hitreg_*.csv from "-HitregBench=N -BenchRules": the same rules
against open-field strafing targets).

Left: of the shots that were on target on the shooter's screen, the share that hit a runner whose OWN screen
had already lost sight of the shooter ("I was behind the wall"). Right: the share of those on-screen shots
the server registered (the cost of a stricter rule), with open-field registration dashed.
Usage: python Tools/plot_cover.py [cover.csv] [hitreg_rules.csv]
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parents[1]
cover_src = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "docs/data/cover_sweep.csv"
rules_src = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "docs/data/hitreg_rules.csv"
out = root / "docs/media/cover_deaths.png"

STYLE = {
    "cap_100": ("Cap 100 ms", "#1f9e74", "-"),
    "cap_200": ("Cap 200 ms (default)", "#2a6fdb", "-"),
    "cap_300": ("Cap 300 ms", "#c8433a", "-"),
    "cover_rule_100": ("Cover rule (cap 200, 100 ms in cover)", "#e08a1e", "-"),
}

valid = defaultdict(int)
behind = defaultdict(int)
registered = defaultdict(int)
behind_ms = defaultdict(list)
for r in csv.DictReader(open(cover_src)):
    if r["valid"] != "1":
        continue
    key = (r["config"], int(r["rtt_ms"]))
    valid[key] += 1
    behind[key] += int(r["behind_cover_death"])
    registered[key] += int(r["registered"])
    if r["registered"] == "1" and r["hit_arrived"] == "1" and r["runner_lost_sight"] == "1":
        behind_ms[key].append(float(r["behind_ms"]))

rtts = sorted({k[1] for k in valid})
configs = [c for c in STYLE if any((c, t) in valid for t in rtts)]

open_field = {}
if rules_src.exists():
    for r in csv.DictReader(open(rules_src)):
        rule = r.get("rule", "")
        name = "cover_rule_100" if rule.startswith("cover_rule") else rule
        p = int(r["predicted_hits"])
        if p:
            open_field[(name, int(r["rtt_ms"]))] = min(100.0, 100.0 * int(r["confirmed_hits"]) / p)

fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.4), dpi=150)
for c in configs:
    label, color, ls = STYLE[c]
    xs = [t for t in rtts if (c, t) in valid]
    a1.plot(xs, [100.0 * behind[(c, t)] / valid[(c, t)] for t in xs], marker="o", color=color, lw=2, ls=ls, label=label)
    a2.plot(xs, [100.0 * registered[(c, t)] / valid[(c, t)] for t in xs], marker="o", color=color, lw=2, ls=ls, label=label)
    of = [(t, open_field[(c, t)]) for t in rtts if (c, t) in open_field]
    if of:
        a2.plot([t for t, _ in of], [v for _, v in of], marker="s", ms=4, color=color, lw=1.2, ls="--", alpha=0.8)

n = statistics.median(valid.values()) if valid else 0
a1.set_title("Deaths that land after the victim's own screen\nalready shows them behind the wall", fontsize=10)
a1.set_ylabel(f"Share of on-screen shots (n = {n:.0f} per point)")
a2.set_title("On-screen shots the server registered\n(solid: target diving into cover; dashed: open-field strafe)", fontsize=10)
a2.set_ylabel("Registered")
for a in (a1, a2):
    a.set_xlabel("Simulated round-trip time, both players (ms)")
    a.set_xticks(rtts)
    a.set_ylim(-3, 105)
    a.grid(True, alpha=0.3)
a1.legend(frameon=False, fontsize=8, loc="upper left")
fig.suptitle("Hitreg Lab: \"I was behind the wall and still died\" (dedicated server, 2 clients, 60 Hz)", fontsize=11)
fig.tight_layout()
out.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(out)
print(out)

# Table for the README
print("rtt | " + " | ".join(STYLE[c][0] for c in configs))
for t in rtts:
    cells = []
    for c in configs:
        k = (c, t)
        if k not in valid:
            cells.append("-")
            continue
        med = statistics.median(behind_ms[k]) if behind_ms[k] else float("nan")
        cells.append(f"{100.0 * behind[k] / valid[k]:.0f}% behind, {100.0 * registered[k] / valid[k]:.0f}% reg, median {med:+.0f} ms")
    print(f"{t} | " + " | ".join(cells))
