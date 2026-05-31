#!/usr/bin/env python3
"""
plot_cdf.py – CDF opóźnień dla strumienia VoIP (STA2) z pliku FlowMonitor XML.

Użycie:
    python3 plot_cdf.py flowmon-results-ampdu-on.xml [flowmon-results-ampdu-off.xml]

Jeśli podano dwa pliki, na wykresie pojawią się dwie krzywe (porównanie).
Wynik: cdf_delay_voip.pdf w bieżącym katalogu.
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import matplotlib
matplotlib.use("Agg")  # backend bez GUI – bezpieczne na serwerze/WSL
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np


# ─── Stałe ────────────────────────────────────────────────────────────────────

VOIP_DST_PORT = 5002   # port docelowy strumienia VoIP (STA2 → AP)
VOIP_PROTOCOL = 17     # UDP = 17


# ─── Pomocnicze ───────────────────────────────────────────────────────────────

def find_voip_flow_id(root: ET.Element) -> int | None:
    """
    Szuka w <Ipv4FlowClassifier> wpisu UDP z docelowym portem VOIP_DST_PORT.
    Zwraca flowId (int) lub None, gdy nie znaleziono.
    """
    classifier = root.find("Ipv4FlowClassifier")
    if classifier is None:
        raise RuntimeError("Brak elementu <Ipv4FlowClassifier> w pliku XML.")

    for flow in classifier.iter("Flow"):
        proto = int(flow.get("protocol", 0))
        dst_port = int(flow.get("destinationPort", 0))
        if proto == VOIP_PROTOCOL and dst_port == VOIP_DST_PORT:
            return int(flow.get("flowId"))

    return None


def extract_delay_histogram(root: ET.Element, flow_id: int) -> tuple[np.ndarray, np.ndarray]:
    """
    Wyciąga histogram opóźnień dla danego flowId ze struktury <FlowStats>.

    Zwraca (delays_ms, counts):
        delays_ms – środki przedziałów histogramu [ms]
        counts    – liczba pakietów w każdym przedziale
    """
    flow_stats = root.find("FlowStats")
    if flow_stats is None:
        raise RuntimeError("Brak elementu <FlowStats> w pliku XML.")

    target = None
    for flow in flow_stats.iter("Flow"):
        if int(flow.get("flowId")) == flow_id:
            target = flow
            break

    if target is None:
        raise RuntimeError(f"Brak statystyk dla flowId={flow_id}.")

    # Podstawowa sanity-check: ile pakietów dotarło
    rx = int(target.get("rxPackets", 0))
    tx = int(target.get("txPackets", 0))
    lost = int(target.get("lostPackets", 0))
    print(f"  flowId={flow_id}: tx={tx}, rx={rx}, lost={lost}")
    if rx == 0:
        raise RuntimeError(f"Flow {flow_id} nie ma odebranych pakietów (rx=0).")

    histogram = target.find("delayHistogram")
    if histogram is None:
        raise RuntimeError(
            f"Brak <delayHistogram> dla flowId={flow_id}.\n"
            "Upewnij się, że FlowMonitor::SerializeToXmlFile wywołano z "
            "enableHistograms=true (drugi argument)."
        )

    delays_ms = []
    counts = []

    for bin_el in histogram.iter("bin"):
        start = float(bin_el.get("start"))   # [s]
        width = float(bin_el.get("width"))   # [s]
        count = int(bin_el.get("count"))

        if count == 0:
            continue

        midpoint_ms = (start + width / 2.0) * 1e3  # s → ms
        delays_ms.append(midpoint_ms)
        counts.append(count)

    if not delays_ms:
        raise RuntimeError(f"Histogram flowId={flow_id} jest pusty (wszystkie bin.count=0).")

    return np.array(delays_ms), np.array(counts)


def build_cdf(delays_ms: np.ndarray, counts: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """
    Buduje CDF z histogramu: sortuje po opóźnieniu, liczy narastającą sumę.
    Zwraca (x, y) gotowe do plt.plot().
    """
    order = np.argsort(delays_ms)
    x = delays_ms[order]
    y = np.cumsum(counts[order], dtype=float)
    y /= y[-1]  # normalizacja do [0, 1]
    return x, y


def parse_and_build(xml_path: str) -> tuple[np.ndarray, np.ndarray, str]:
    """Otwiera plik XML, lokalizuje VoIP flow, buduje CDF. Zwraca (x, y, label)."""
    path = Path(xml_path)
    if not path.exists():
        raise FileNotFoundError(f"Plik nie istnieje: {xml_path}")

    print(f"\nParsowanie: {path.name}")
    tree = ET.parse(path)
    root = tree.getroot()

    flow_id = find_voip_flow_id(root)
    if flow_id is None:
        raise RuntimeError(
            f"Nie znaleziono strumienia VoIP (UDP, port dst={VOIP_DST_PORT}) "
            f"w pliku {path.name}."
        )
    print(f"  Znaleziono VoIP flow: flowId={flow_id}")

    delays_ms, counts = extract_delay_histogram(root, flow_id)
    x, y = build_cdf(delays_ms, counts)

    # Etykieta na wykresie – wyciągnij „ampdu-on" / „ampdu-off" z nazwy pliku
    label = path.stem.replace("flowmon-results-", "").replace("-", " ")
    return x, y, label


def detect_title(xml_files: list[str]) -> str:
    """
    Wykrywa tytuł wykresu na podstawie nazw plików wejściowych.
    Rozpoznaje pliki wifi7-mlo-* i ampdu-*.
    """
    names = [Path(f).stem for f in xml_files]
    has_wifi7 = any("wifi7" in n for n in names)
    has_ampdu = any("ampdu" in n for n in names)

    if has_wifi7 and has_ampdu:
        return "CDF opóźnień strumienia VoIP (STA2 → AP)\nWi-Fi 6 (802.11ax) vs Wi-Fi 7 (802.11be / MLO)"
    elif has_wifi7:
        return "CDF opóźnień strumienia VoIP (STA2 → AP)\nWi-Fi 7 (802.11be, 5+6 GHz MLO STR)"
    else:
        return "CDF opóźnień strumienia VoIP (STA2 → AP)\nWi-Fi 6 (802.11ax, 5 GHz, 80 MHz)"


# ─── Główna logika ─────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    xml_files = sys.argv[1:]  # jeden lub dwa pliki

    # Kolory i style dla kolejnych krzywych
    STYLES = [
        {"color": "#1f77b4", "linestyle": "-",  "linewidth": 2.0},
        {"color": "#d62728", "linestyle": "--", "linewidth": 2.0},
    ]

    fig, ax = plt.subplots(figsize=(8, 5))

    stats_rows = []  # do tabeli percentyli pod wykresem

    for i, xml_path in enumerate(xml_files[:2]):  # max 2 pliki
        x, y, label = parse_and_build(xml_path)

        style = STYLES[i % len(STYLES)]
        ax.plot(x, y, label=label, **style)

        # Percentyle
        p50  = x[np.searchsorted(y, 0.50, side="left")]
        p95  = x[np.searchsorted(y, 0.95, side="left")]
        p99  = x[np.searchsorted(y, 0.99, side="left")]
        stats_rows.append(f"  {label:<22} P50={p50:.2f} ms  P95={p95:.2f} ms  P99={p99:.2f} ms")

    # ── Styl wykresu ──────────────────────────────────────────────────────────
    ax.set_xlabel("Opóźnienie end-to-end [ms]", fontsize=13)
    ax.set_ylabel("CDF – prawdopodobieństwo", fontsize=13)
    ax.set_title(detect_title(xml_files), fontsize=13, pad=12)

    ax.set_ylim(0, 1.02)
    ax.set_xlim(left=0)
    ax.yaxis.set_major_formatter(ticker.PercentFormatter(xmax=1.0, decimals=0))
    ax.xaxis.set_minor_locator(ticker.AutoMinorLocator())

    ax.grid(which="major", linestyle="--", alpha=0.5)
    ax.grid(which="minor", linestyle=":",  alpha=0.25)

    ax.axhline(0.95, color="gray", linestyle=":", linewidth=1.0, label="P95")
    ax.axhline(0.50, color="gray", linestyle=":",  linewidth=0.8, label="P50")

    ax.legend(fontsize=11, loc="lower right")
    fig.tight_layout()

    # ── Zapis ─────────────────────────────────────────────────────────────────
    out_pdf = "cdf_delay_voip.pdf"
    fig.savefig(out_pdf, format="pdf", bbox_inches="tight")
    print(f"\nWykres zapisany: {out_pdf}")

    # ── Percentyle w konsoli ───────────────────────────────────────────────────
    print("\nPercentyle opóźnień VoIP:")
    for row in stats_rows:
        print(row)
    print()


if __name__ == "__main__":
    main()