#!/usr/bin/env python3
"""Turn the CSVs in results/ into report tables and graphs.

    python3 scripts/plot.py            # reads results/, writes results/*.png
Prints speedup/efficiency tables to stdout. Graphs are skipped (with a note)
if matplotlib is not installed; the tables still print so the numbers are
usable in a spreadsheet.
"""
import csv, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_PLT = True
except Exception:
    HAVE_PLT = False


def read(name):
    path = os.path.join(RES, name)
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def strong():
    rows = read("strong.csv")
    if not rows:
        return
    by = {}
    for r in rows:
        by.setdefault(r["version"], []).append((int(r["procs"]), float(r["time_s"])))
    print("\n== Strong scaling (speedup = T(P=1)/T(P), efficiency = speedup/P) ==")
    for v, pts in by.items():
        pts.sort()
        t1 = dict(pts).get(1, pts[0][1])
        print(f"\n[{v}]  P    time(s)   speedup   efficiency")
        for p, t in pts:
            sp = t1 / t
            print(f"        {p:<4} {t:<9.4f} {sp:<9.2f} {sp/p:.2f}")
    if HAVE_PLT:
        plt.figure()
        for v, pts in by.items():
            pts.sort(); t1 = dict(pts).get(1, pts[0][1])
            plt.plot([p for p, _ in pts], [t1 / t for _, t in pts], "o-", label=v)
        ideal = sorted({p for pts in by.values() for p, _ in pts})
        plt.plot(ideal, ideal, "k--", label="ideal")
        plt.xlabel("processes"); plt.ylabel("speedup"); plt.title("Strong scaling")
        plt.legend(); plt.grid(True); plt.savefig(os.path.join(RES, "strong_speedup.png"), dpi=130)
        print("  wrote results/strong_speedup.png")


def weak():
    rows = read("weak.csv")
    if not rows:
        return
    by = {}
    for r in rows:
        by.setdefault(r["version"], []).append((int(r["procs"]), float(r["time_s"])))
    print("\n== Weak scaling (efficiency = T(P=1)/T(P), ideal = flat time) ==")
    for v, pts in by.items():
        pts.sort(); t1 = dict(pts).get(1, pts[0][1])
        print(f"\n[{v}]  P    time(s)   efficiency")
        for p, t in pts:
            print(f"        {p:<4} {t:<9.4f} {t1/t:.2f}")
    if HAVE_PLT:
        plt.figure()
        for v, pts in by.items():
            pts.sort()
            plt.plot([p for p, _ in pts], [t for _, t in pts], "o-", label=v)
        plt.xlabel("processes"); plt.ylabel("time (s)"); plt.title("Weak scaling (flat = ideal)")
        plt.legend(); plt.grid(True); plt.savefig(os.path.join(RES, "weak_time.png"), dpi=130)
        print("  wrote results/weak_time.png")


def interval():
    rows = read("interval.csv")
    if not rows or not HAVE_PLT:
        return
    by = {}
    for r in rows:
        by.setdefault(r["sync"], []).append((int(r["interval"]), float(r["throughput"])))
    plt.figure()
    for s, pts in by.items():
        pts.sort()
        plt.plot([i for i, _ in pts], [t for _, t in pts], "o-", label=s)
    plt.xscale("log", base=2); plt.xlabel("check interval (candidates)")
    plt.ylabel("throughput (cand/s)"); plt.title("Termination check: collective vs p2p")
    plt.legend(); plt.grid(True); plt.savefig(os.path.join(RES, "interval.png"), dpi=130)
    print("  wrote results/interval.png")


def ttf():
    rows = read("ttf.csv")
    if not rows:
        return
    by = {}
    for r in rows:
        by.setdefault(r["partition"], []).append((int(r["procs"]), float(r["time_s"])))
    print("\n== Time-to-find (crack mode) ==")
    for part, pts in by.items():
        pts.sort()
        print(f"[{part}] " + "  ".join(f"P={p}:{t:.4f}s" for p, t in pts))
    if HAVE_PLT:
        plt.figure()
        for part, pts in by.items():
            pts.sort()
            plt.plot([p for p, _ in pts], [t for _, t in pts], "o-", label=part)
        plt.xlabel("processes"); plt.ylabel("time to find (s)"); plt.title("Time-to-find: block vs cyclic")
        plt.legend(); plt.grid(True); plt.savefig(os.path.join(RES, "ttf.png"), dpi=130)
        print("  wrote results/ttf.png")


if __name__ == "__main__":
    if not HAVE_PLT:
        print("(matplotlib not found: printing tables only, no PNGs)", file=sys.stderr)
    strong(); weak(); interval(); ttf()
