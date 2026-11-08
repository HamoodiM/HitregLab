"""Plots docs/data/peek_sweep.csv -> docs/media/peek_advantage.png (median peeker's advantage per config)."""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parents[1]
src = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "docs/data/peek_sweep.csv"
out = root / "docs/media/peek_advantage.png"

med = defaultdict(list)
for r in csv.DictReader(open(src)):
    if r["valid"] == "1":
        med[(int(r["rtt_ms"]), int(r["tick_hz"]), int(r["interp_ticks"]))].append(float(r["advantage_ms"]))

ticks = [20, 30, 60, 128]
colors = {0: "#2a6fdb", 50: "#1f9e74", 100: "#e08a1e", 150: "#c8433a"}
fig, ax = plt.subplots(figsize=(8, 4.6), dpi=150)
for rtt, color in colors.items():
    ys = [statistics.median(med[(rtt, t, 2)]) for t in ticks]
    ax.plot(ticks, ys, marker="o", color=color, lw=2, label=f"{rtt} ms simulated RTT")
    one = med.get((rtt, 60, 1))
    if one:
        ax.scatter([60], [statistics.median(one)], marker="v", color=color, s=55, zorder=3)
ax.set_xscale("log", base=2)
ax.set_xticks(ticks, [f"{t} Hz" for t in ticks])
ax.set_ylabel("Peeker's advantage (ms, median of 8)")
ax.set_xlabel("Server tick rate")
ax.set_title("Hitreg Lab: peeker's advantage vs tick rate and latency\n(circles: 2-tick interpolation, triangles: 1 tick at 60 Hz)", fontsize=10)
ax.grid(True, alpha=0.3)
ax.set_ylim(0, None)
ax.legend(frameon=False, fontsize=9)
fig.tight_layout()
out.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(out)
print(out)
