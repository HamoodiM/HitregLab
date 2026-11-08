"""Plots docs/data/hitreg_bench.csv -> docs/media/hitreg_registration.png.

Metric: of the shots that were on target ON THE SHOOTER'S SCREEN (client-predicted hits), the share the
server confirmed. Perfect aim at the drawn target, single taps at a dummy strafing 10 m away."""
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parents[1]
rows = list(csv.DictReader(open(root / "docs/data/hitreg_bench.csv")))
rtts = sorted({int(r["rtt_ms"]) for r in rows})


def rate(rtt, on):
    r = next(r for r in rows if int(r["rtt_ms"]) == rtt and r["lag_compensation"] == ("1" if on else "0"))
    p = int(r["predicted_hits"])
    return min(100.0, 100.0 * int(r["confirmed_hits"]) / p) if p else 0.0


fig, ax = plt.subplots(figsize=(8, 4.2), dpi=150)
w = 0.38
xs = range(len(rtts))
on = [rate(r, True) for r in rtts]
off = [rate(r, False) for r in rtts]
b1 = ax.bar([x - w / 2 for x in xs], on, w, color="#2a6fdb", label="Lag compensation on")
b2 = ax.bar([x + w / 2 for x in xs], off, w, color="#c8433a", label="Lag compensation off")
for bars in (b1, b2):
    for b in bars:
        ax.text(b.get_x() + b.get_width() / 2, b.get_height() + 1.5, f"{b.get_height():.0f}%", ha="center", fontsize=8)
ax.set_xticks(list(xs), [f"{r} ms" for r in rtts])
ax.set_xlabel("Simulated round-trip time (200 ms rewind cap)")
ax.set_ylabel("On-screen hits the server confirmed")
ax.set_ylim(0, 110)
ax.set_title("Hitreg Lab: perfect aim at what the client draws, single taps, 10 m strafing target", fontsize=10)
ax.legend(frameon=False, fontsize=9, loc="upper right")
ax.grid(True, axis="y", alpha=0.3)
fig.tight_layout()
out = root / "docs/media/hitreg_registration.png"
fig.savefig(out)
print(out)
