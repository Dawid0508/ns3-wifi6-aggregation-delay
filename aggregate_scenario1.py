#!/usr/bin/env python3
"""
aggregate_scenario1.py — agregacja wyników Scenariusza 1 (sweep A-MPDU).

Czyta wszystkie pliki results/scenario1/flowmon-ampdu<A>-run<R>.xml, grupuje je
po wartości BE_MaxAmpduSize i dla każdej wartości:
  * buduje połączoną (pooled) dystrybuantę CDF opóźnień przepływu VoIP,
  * liczy percentyle p50/p95/p99 osobno dla każdego powtórzenia, a następnie
    raportuje średnią z 10 powtórzeń wraz z 95% przedziałem ufności (t-Studenta).

Wynik:
  * results/scenario1/cdf_scenario1_ampdu_sweep.pdf — 5 krzywych CDF (po jednej
    na wartość A-MPDU),
  * tabela percentyli w konsoli.

Użycie:
    python3 aggregate_scenario1.py [katalog_z_xml] [--out=plik.pdf]
    (domyślny katalog: results/scenario1)
"""

import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

# Cache matplotliba do katalogu zapisywalnego (WSL/serwer).
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

# Wartości t-Studenta (dwustronne, 95%) wg liczby stopni swobody df = n-1.
T_95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447,
        7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179,
        13: 2.160, 14: 2.145, 15: 2.131, 19: 2.093, 29: 2.045}

FNAME_RE = re.compile(r"flowmon-ampdu(\d+)-run(\d+)\.xml$")


def t_value(n: int) -> float:
    """Wartość t-Studenta dla 95% CI przy n próbach (df=n-1)."""
    if n <= 1:
        return float("nan")
    return T_95.get(n - 1, 1.96)


def find_voip_flow_id(root: ET.Element) -> int | None:
    classifier = root.find("Ipv4FlowClassifier")
    if classifier is None:
        return None
    for flow in classifier.iter("Flow"):
        if (int(flow.get("protocol", 0)) == UDP_PROTOCOL
                and int(flow.get("destinationPort", 0)) == VOIP_DST_PORT):
            return int(flow.get("flowId"))
    return None


def extract_histogram(xml_path: Path) -> tuple[np.ndarray, np.ndarray] | None:
    """Zwraca (delays_ms, counts) dla przepływu VoIP albo None."""
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
        mid_ms = (float(b.get("start")) + float(b.get("width")) / 2.0) * 1e3
        delays.append(mid_ms)
        counts.append(c)
    if not delays:
        return None
    return np.array(delays), np.array(counts, dtype=float)


def cdf_from_hist(delays: np.ndarray, counts: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(delays)
    x = delays[order]
    y = np.cumsum(counts[order])
    y = y / y[-1]
    return x, y


def percentile_from_hist(delays: np.ndarray, counts: np.ndarray, q: float) -> float:
    x, y = cdf_from_hist(delays, counts)
    idx = np.searchsorted(y, q, side="left")
    idx = min(idx, len(x) - 1)
    return float(x[idx])


def human_ampdu(a: int) -> str:
    return "0 (OFF)" if a == 0 else f"{a:,}".replace(",", " ")


def main():
    args = [a for a in sys.argv[1:]]
    out_pdf = None
    in_dir = None
    for a in args:
        if a.startswith("--out="):
            out_pdf = a.split("=", 1)[1]
        else:
            in_dir = a
    if in_dir is None:
        in_dir = str(Path(__file__).resolve().parent / "results" / "scenario1")
    in_path = Path(in_dir)
    if out_pdf is None:
        out_pdf = str(in_path / "cdf_scenario1_ampdu_sweep.pdf")

    # Zbierz pliki pogrupowane po wartości A-MPDU.
    files = sorted(in_path.glob("flowmon-ampdu*-run*.xml"))
    if not files:
        print(f"Brak plików flowmon-ampdu*-run*.xml w {in_path}.")
        print("Najpierw uruchom kampanię: ./scratch/ns3-wifi6-aggregation-delay/run_scenario1.sh")
        sys.exit(1)

    by_ampdu: dict[int, list[Path]] = defaultdict(list)
    for f in files:
        m = FNAME_RE.search(f.name)
        if m:
            by_ampdu[int(m.group(1))].append(f)

    STYLES = ["-", "--", "-.", ":", (0, (3, 1, 1, 1))]
    fig, ax = plt.subplots(figsize=(8, 5))

    print(f"\nAgregacja z katalogu: {in_path}")
    print(f"{'A-MPDU [B]':>14} | {'n':>2} | {'p50 [ms]':>16} | {'p95 [ms]':>18} | {'p99 [ms]':>18}")
    print("-" * 78)

    for k, a in enumerate(sorted(by_ampdu)):
        pooled_delays, pooled_counts = [], []
        p50s, p95s, p99s = [], [], []
        for f in by_ampdu[a]:
            res = extract_histogram(f)
            if res is None:
                continue
            d, c = res
            pooled_delays.append(d)
            pooled_counts.append(c)
            p50s.append(percentile_from_hist(d, c, 0.50))
            p95s.append(percentile_from_hist(d, c, 0.95))
            p99s.append(percentile_from_hist(d, c, 0.99))

        if not pooled_delays:
            print(f"{human_ampdu(a):>14} |  0 |  (brak danych)")
            continue

        # Połączona CDF: sumujemy zliczenia po identycznych środkach przedziałów.
        agg: dict[float, float] = defaultdict(float)
        for d, c in zip(pooled_delays, pooled_counts):
            for di, ci in zip(d, c):
                agg[round(di, 6)] += ci
        xs = np.array(sorted(agg))
        ys = np.cumsum([agg[x] for x in xs])
        ys = ys / ys[-1]
        ax.plot(xs, ys, linestyle=STYLES[k % len(STYLES)], linewidth=2.0,
                label=f"{human_ampdu(a)} B")

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
        print(f"{human_ampdu(a):>14} | {n:>2} | "
              f"{m50:7.2f} ± {c50:5.2f} | {m95:8.2f} ± {c95:6.2f} | {m99:8.2f} ± {c99:6.2f}")

    ax.set_xlabel("Opóźnienie end-to-end VoIP [ms]", fontsize=13)
    ax.set_ylabel("CDF – prawdopodobieństwo", fontsize=13)
    ax.set_title("Scenariusz 1: wpływ limitu A-MPDU na CDF opóźnień VoIP\n"
                 "Wi-Fi 6 (802.11ax), 5 stacji tła, 10 powtórzeń (pooled)",
                 fontsize=12, pad=12)
    ax.set_ylim(0, 1.02)
    ax.set_xlim(left=0)
    ax.yaxis.set_major_formatter(ticker.PercentFormatter(xmax=1.0, decimals=0))
    ax.grid(which="major", linestyle="--", alpha=0.5)
    ax.legend(title="BE_MaxAmpduSize", fontsize=10, loc="lower right")
    fig.tight_layout()
    fig.savefig(out_pdf, bbox_inches="tight")
    print("-" * 78)
    print(f"Wykres zapisany: {out_pdf}\n")


if __name__ == "__main__":
    main()
