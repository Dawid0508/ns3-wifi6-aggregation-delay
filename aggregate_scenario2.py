#!/usr/bin/env python3
"""
aggregate_scenario2.py — agregacja wyników Scenariusza 2 (skalowalność z N).

Czyta results/scenario2/flowmon-ampdu{on,off}-N<N>-run<R>.xml, grupuje po
(wariant agregacji, liczba stacji N) i dla każdego punktu liczy:
  * 99-percentyl opóźnienia end-to-end przepływu VoIP (p99),
  * średni jitter przepływu VoIP,
jako średnią z powtórzeń wraz z 95% przedziałem ufności (t-Studenta).

Wynik:
  * results/scenario2/scenario2_p99_vs_N.pdf    — p99 opóźnienia VoIP vs N,
  * results/scenario2/scenario2_jitter_vs_N.pdf — średni jitter VoIP vs N,
  * tabela w konsoli.

Użycie:
    python3 aggregate_scenario2.py [katalog_z_xml]
    (domyślny katalog: results/scenario2)
"""

import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

_cfg = Path(os.environ.get("MPLCONFIGDIR", Path.home() / ".config" / "matplotlib"))
if not os.access(_cfg.parent if not _cfg.exists() else _cfg, os.W_OK):
    _fallback = Path(tempfile.gettempdir()) / "mplconfig"
    _fallback.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(_fallback)

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

VOIP_DST_PORT = 5002
UDP_PROTOCOL = 17

T_95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447,
        7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179,
        13: 2.160, 14: 2.145, 15: 2.131, 19: 2.093, 29: 2.045}

FNAME_RE = re.compile(r"flowmon-ampdu(on|off)-N(\d+)-run(\d+)\.xml$")

VARIANTS = [("off", "A-MPDU OFF", "#d62728", "--", "s"),
            ("on",  "A-MPDU ON",  "#1f77b4", "-",  "o")]


def t_value(n: int) -> float:
    if n <= 1:
        return float("nan")
    return T_95.get(n - 1, 1.96)


def ns_to_float(v: str) -> float:
    """Konwersja wartości czasu FlowMonitora, np. '+1.65e+08ns' -> 1.65e8 [ns]."""
    return float(v[:-2]) if v.endswith("ns") else float(v)


def voip_flow(root):
    classifier = root.find("Ipv4FlowClassifier")
    if classifier is None:
        return None
    fid = None
    for flow in classifier.iter("Flow"):
        if (int(flow.get("protocol", 0)) == UDP_PROTOCOL
                and int(flow.get("destinationPort", 0)) == VOIP_DST_PORT):
            fid = int(flow.get("flowId"))
            break
    if fid is None:
        return None
    for flow in root.find("FlowStats").iter("Flow"):
        if int(flow.get("flowId")) == fid:
            return flow
    return None


def metrics_from_file(xml_path):
    """Zwraca (p99_ms, jitter_ms) dla przepływu VoIP albo None."""
    root = ET.parse(xml_path).getroot()
    flow = voip_flow(root)
    if flow is None:
        return None
    rx = int(flow.get("rxPackets", 0))
    if rx < 2:
        return None

    # Średni jitter z atrybutów przepływu.
    jitter_ms = ns_to_float(flow.get("jitterSum")) / (rx - 1) / 1e6

    # p99 z histogramu opóźnień.
    hist = flow.find("delayHistogram")
    if hist is None:
        return None
    delays, counts = [], []
    for b in hist.iter("bin"):
        c = int(b.get("count"))
        if c == 0:
            continue
        delays.append((float(b.get("start")) + float(b.get("width")) / 2.0) * 1e3)
        counts.append(c)
    if not delays:
        return None
    d = np.array(delays)
    c = np.array(counts, dtype=float)
    order = np.argsort(d)
    x = d[order]
    y = np.cumsum(c[order])
    y /= y[-1]
    idx = min(np.searchsorted(y, 0.99, side="left"), len(x) - 1)
    p99_ms = float(x[idx])
    return p99_ms, jitter_ms


def mean_ci(vals):
    arr = np.array(vals, dtype=float)
    if len(arr) == 0:
        return float("nan"), 0.0
    if len(arr) == 1:
        return float(arr[0]), 0.0
    return float(arr.mean()), t_value(len(arr)) * float(arr.std(ddof=1)) / np.sqrt(len(arr))


def main():
    in_dir = sys.argv[1] if len(sys.argv) > 1 else None
    if in_dir is None:
        in_dir = str(Path(__file__).resolve().parent / "results" / "scenario2")
    in_path = Path(in_dir)

    files = sorted(in_path.glob("flowmon-ampdu*-N*-run*.xml"))
    if not files:
        print(f"Brak plików flowmon-ampdu*-N*-run*.xml w {in_path}.")
        print("Najpierw uruchom kampanię: ./scratch/ns3-wifi6-aggregation-delay/run_scenario2.sh")
        sys.exit(1)

    # data[tag][N] = list of (p99, jitter)
    data = defaultdict(lambda: defaultdict(list))
    for f in files:
        m = FNAME_RE.search(f.name)
        if not m:
            continue
        res = metrics_from_file(f)
        if res is None:
            continue
        data[m.group(1)][int(m.group(2))].append(res)

    fig_p, ax_p = plt.subplots(figsize=(8, 5))
    fig_j, ax_j = plt.subplots(figsize=(8, 5))

    print(f"\nAgregacja z katalogu: {in_path}")
    print(f"{'wariant':>10} | {'N':>3} | {'n':>2} | {'p99 [ms]':>18} | {'jitter [ms]':>18}")
    print("-" * 66)

    for tag, label, color, ls, marker in VARIANTS:
        if tag not in data:
            continue
        Ns = sorted(data[tag])
        p99_m, p99_e, jit_m, jit_e = [], [], [], []
        for N in Ns:
            pairs = data[tag][N]
            p99s = [p for p, _ in pairs]
            jits = [j for _, j in pairs]
            pm, pe = mean_ci(p99s)
            jm, je = mean_ci(jits)
            p99_m.append(pm); p99_e.append(pe)
            jit_m.append(jm); jit_e.append(je)
            print(f"{label:>10} | {N:>3} | {len(pairs):>2} | "
                  f"{pm:8.2f} ± {pe:6.2f} | {jm:8.3f} ± {je:6.3f}")

        ax_p.errorbar(Ns, p99_m, yerr=p99_e, color=color, linestyle=ls, marker=marker,
                      linewidth=2.0, capsize=4, label=label)
        ax_j.errorbar(Ns, jit_m, yerr=jit_e, color=color, linestyle=ls, marker=marker,
                      linewidth=2.0, capsize=4, label=label)

    for ax, ylab, title in (
        (ax_p, "p99 opóźnienia VoIP [ms]",
         "Scenariusz 2: p99 opóźnienia VoIP vs liczba stacji tła"),
        (ax_j, "Średni jitter VoIP [ms]",
         "Scenariusz 2: średni jitter VoIP vs liczba stacji tła"),
    ):
        ax.set_xlabel("Liczba stacji w tle N", fontsize=13)
        ax.set_ylabel(ylab, fontsize=13)
        ax.set_title(title + "\nWi-Fi 6 (802.11ax), 10 powtórzeń, 95% CI", fontsize=12, pad=12)
        ax.grid(True, linestyle="--", alpha=0.5)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=11)

    out_p = str(in_path / "scenario2_p99_vs_N.pdf")
    out_j = str(in_path / "scenario2_jitter_vs_N.pdf")
    fig_p.tight_layout(); fig_p.savefig(out_p, bbox_inches="tight")
    fig_j.tight_layout(); fig_j.savefig(out_j, bbox_inches="tight")
    print("-" * 66)
    print(f"Wykresy zapisane:\n  {out_p}\n  {out_j}\n")


if __name__ == "__main__":
    main()
