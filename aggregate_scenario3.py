#!/usr/bin/env python3
"""
aggregate_scenario3.py — agregacja wyników Scenariusza 3 (Wi-Fi 7 / MLO).

Czyta results/scenario3/flowmon-wifi7-mlo{on,off}-bg<N>-run<R>.xml, grupuje po
wariancie MLO i dla każdego:
  * buduje połączoną (pooled) dystrybuantę CDF opóźnień przepływu VoIP,
  * raportuje p50/p95/p99 jako średnią z powtórzeń z 95% przedziałem ufności.

Wynik:
  * results/scenario3/cdf_scenario3_mlo.pdf — dwie krzywe (MLO ON vs OFF),
  * tabela percentyli w konsoli.

Użycie:
    python3 aggregate_scenario3.py [katalog_z_xml] [--out=plik.pdf]
    (domyślny katalog: results/scenario3)
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
import matplotlib.ticker as ticker
import numpy as np

VOIP_DST_PORT = 5002
UDP_PROTOCOL = 17

T_95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447,
        7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179,
        13: 2.160, 14: 2.145, 15: 2.131, 19: 2.093, 29: 2.045}

FNAME_RE = re.compile(r"flowmon-wifi7-mlo(on|off)-.*run(\d+)\.xml$")

# Kolejność i etykiety wariantów na wykresie.
VARIANTS = [("off", "MLO OFF (1 link)", "#d62728", "--"),
            ("on",  "MLO ON (5+6 GHz)", "#1f77b4", "-")]


def t_value(n: int) -> float:
    if n <= 1:
        return float("nan")
    return T_95.get(n - 1, 1.96)


def find_voip_flow_id(root):
    classifier = root.find("Ipv4FlowClassifier")
    if classifier is None:
        return None
    for flow in classifier.iter("Flow"):
        if (int(flow.get("protocol", 0)) == UDP_PROTOCOL
                and int(flow.get("destinationPort", 0)) == VOIP_DST_PORT):
            return int(flow.get("flowId"))
    return None


def extract_histogram(xml_path):
    root = ET.parse(xml_path).getroot()
    flow_id = find_voip_flow_id(root)
    if flow_id is None:
        return None
    flow_stats = root.find("FlowStats")
    target = None
    for flow in flow_stats.iter("Flow"):
        if int(flow.get("flowId")) == flow_id:
            target = flow
            break
    if target is None or int(target.get("rxPackets", 0)) == 0:
        return None
    hist = target.find("delayHistogram")
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
    return np.array(delays), np.array(counts, dtype=float)


def cdf_from_hist(delays, counts):
    order = np.argsort(delays)
    x = delays[order]
    y = np.cumsum(counts[order])
    return x, y / y[-1]


def percentile_from_hist(delays, counts, q):
    x, y = cdf_from_hist(delays, counts)
    idx = min(np.searchsorted(y, q, side="left"), len(x) - 1)
    return float(x[idx])


def main():
    out_pdf = None
    in_dir = None
    for a in sys.argv[1:]:
        if a.startswith("--out="):
            out_pdf = a.split("=", 1)[1]
        else:
            in_dir = a
    if in_dir is None:
        in_dir = str(Path(__file__).resolve().parent / "results" / "scenario3")
    in_path = Path(in_dir)
    if out_pdf is None:
        out_pdf = str(in_path / "cdf_scenario3_mlo.pdf")

    files = sorted(in_path.glob("flowmon-wifi7-mlo*run*.xml"))
    if not files:
        print(f"Brak plików flowmon-wifi7-mlo*run*.xml w {in_path}.")
        print("Najpierw uruchom kampanię: ./scratch/ns3-wifi6-aggregation-delay/run_scenario3.sh")
        sys.exit(1)

    by_mlo = defaultdict(list)
    for f in files:
        m = FNAME_RE.search(f.name)
        if m:
            by_mlo[m.group(1)].append(f)

    fig, ax = plt.subplots(figsize=(8, 5))
    print(f"\nAgregacja z katalogu: {in_path}")
    print(f"{'Wariant':>18} | {'n':>2} | {'p50 [ms]':>16} | {'p95 [ms]':>18} | {'p99 [ms]':>18}")
    print("-" * 82)

    for tag, label, color, ls in VARIANTS:
        flist = by_mlo.get(tag, [])
        if not flist:
            continue
        pooled = []
        p50s, p95s, p99s = [], [], []
        for f in flist:
            res = extract_histogram(f)
            if res is None:
                continue
            d, c = res
            pooled.append((d, c))
            p50s.append(percentile_from_hist(d, c, 0.50))
            p95s.append(percentile_from_hist(d, c, 0.95))
            p99s.append(percentile_from_hist(d, c, 0.99))
        if not pooled:
            continue

        agg = defaultdict(float)
        for d, c in pooled:
            for di, ci in zip(d, c):
                agg[round(di, 6)] += ci
        xs = np.array(sorted(agg))
        ys = np.cumsum([agg[x] for x in xs])
        ys = ys / ys[-1]
        ax.plot(xs, ys, color=color, linestyle=ls, linewidth=2.0, label=label)

        n = len(p50s)
        t = t_value(n)

        def ci(vals):
            arr = np.array(vals)
            if len(arr) <= 1:
                return arr.mean(), 0.0
            return arr.mean(), t * arr.std(ddof=1) / np.sqrt(len(arr))

        m50, c50 = ci(p50s)
        m95, c95 = ci(p95s)
        m99, c99 = ci(p99s)
        print(f"{label:>18} | {n:>2} | {m50:7.2f} ± {c50:5.2f} | "
              f"{m95:8.2f} ± {c95:6.2f} | {m99:8.2f} ± {c99:6.2f}")

    ax.set_xlabel("Opóźnienie end-to-end VoIP [ms]", fontsize=13)
    ax.set_ylabel("CDF – prawdopodobieństwo", fontsize=13)
    ax.set_title("Scenariusz 3: MLO a CDF opóźnień VoIP\n"
                 "Wi-Fi 7 (802.11be), 5 stacji tła, 10 powtórzeń (pooled)",
                 fontsize=12, pad=12)
    ax.set_ylim(0, 1.02)
    ax.set_xlim(left=0)
    ax.yaxis.set_major_formatter(ticker.PercentFormatter(xmax=1.0, decimals=0))
    ax.grid(which="major", linestyle="--", alpha=0.5)
    ax.legend(fontsize=11, loc="lower right")
    fig.tight_layout()
    fig.savefig(out_pdf, bbox_inches="tight")
    print("-" * 82)
    print(f"Wykres zapisany: {out_pdf}\n")


if __name__ == "__main__":
    main()
