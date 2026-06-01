# ns3-wifi6-aggregation-delay

Projekt badawczy: **Wpływ agregacji ramek A-MPDU na opóźnienia w standardzie Wi-Fi 6 (802.11ax)**

AGH WIEiT — Iza Skowrońska, Szymon Domagała, Dawid Gruszecki  
Prowadzący: prof. dr hab. inż. Szymon Szott

---

## Struktura projektu

```
ns3-wifi6-aggregation-delay/
├── wifi6-ampdu-latency.cc    # Wi-Fi 6 (802.11ax): wpływ A-MPDU (C++)
├── wifi7-mlo-latency.cc      # Wi-Fi 7 (802.11be): wpływ MLO STR (C++)
├── plot_cdf.py               # skrypt analizy i wykresów CDF (Python 3)
├── CMakeLists.txt            # definicje targetów build_exec dla scratch
├── report_sections.tex       # draft raportu (LaTeX)
└── results/
    ├── flowmon-results-ampdu-on.xml       # Wi-Fi 6 z agregacją
    ├── flowmon-results-ampdu-off.xml      # Wi-Fi 6 bez agregacji
    ├── flowmon-results-wifi7-mlo-on.xml   # Wi-Fi 7 z MLO (2 linki)
    ├── flowmon-results-wifi7-mlo-off.xml  # Wi-Fi 7 bez MLO (1 link)
    └── cdf_delay_voip.pdf                 # wykres CDF opóźnień VoIP
```

---

## Wymagania

- **ns-3** >= 3.47 (folder `scratch/` projektu)
- **Python** >= 3.10
- Biblioteki Python: `matplotlib`, `numpy`

```bash
pip install matplotlib numpy
```

---

## Uruchomienie symulacji

Komendy wykonywane z katalogu głównego ns-3 (`ns-3.47/`):

```bash
# Agregacja WŁĄCZONA (domyślnie)
./ns3 run "ns3-wifi6-aggregation-delay/wifi6-ampdu-latency --ampdu=true --simTime=15"

# Agregacja WYŁĄCZONA
./ns3 run "ns3-wifi6-aggregation-delay/wifi6-ampdu-latency --ampdu=false --simTime=15"

# Wi-Fi 7 (802.11be) — MLO STR włączone (5 GHz + 6 GHz)
./ns3 run "ns3-wifi6-aggregation-delay/wifi7-mlo-latency --mlo=true  --simTime=15"

# Wi-Fi 7 — MLO wyłączone (pojedynczy link)
./ns3 run "ns3-wifi6-aggregation-delay/wifi7-mlo-latency --mlo=false --simTime=15"
```

Wyniki XML zapisywane do `results/`.

### Parametry CLI

| Parametr | Domyślnie | Opis |
|---|---|---|
| `--ampdu` | `true` | (Wi-Fi 6) Szybki on/off agregacji (gdy `--maxAmpdu < 0`) |
| `--maxAmpdu` | `-1` | (Wi-Fi 6) `BE_MaxAmpduSize` w bajtach; `< 0` => użyj `--ampdu` |
| `--nBackground` | `5` | (Wi-Fi 6) Liczba stacji generujących ruch masowy |
| `--bulkRate` | `150Mbps` | (Wi-Fi 6) Szybkość OnOff jednej stacji tła |
| `--run` | `1` | (Wi-Fi 6) Numer powtórzenia / ziarno RNG |
| `--mlo` | `true` | (Wi-Fi 7) Włącz/wyłącz MLO STR (2 linki) |
| `--simTime` | `30` (Wi-Fi 6) / `15` (Wi-Fi 7) | Czas symulacji w sekundach (musi być > 1) |
| `--outFile` | auto | (Wi-Fi 6) Ścieżka pliku XML; pusta => nazwa automatyczna |

### Scenariusz 1 (sekcja 4.1) — pełna kampania

Sweep `BE_MaxAmpduSize ∈ {0, 8000, 65535, 524287, 6500631}` B, 5 stacji w tle,
10 powtórzeń z różnymi ziarnami RNG, 30 s symulacji:

```bash
# 50 biegów -> results/scenario1/flowmon-ampdu<A>-run<R>.xml
./scratch/ns3-wifi6-aggregation-delay/run_scenario1.sh

# Agregacja: pooled CDF (5 krzywych) + percentyle p50/p95/p99 ze średnią i 95% CI
python3 scratch/ns3-wifi6-aggregation-delay/aggregate_scenario1.py
```

Wynik: `results/scenario1/cdf_scenario1_ampdu_sweep.pdf` + tabela w konsoli.

### Scenariusz 2 (sekcja 4.2) — skalowalność z obciążeniem

Liczba stacji w tle `N ∈ {1,3,5,7,9,11,13,15}`, w dwóch konfiguracjach agregacji
(ON = 6500631 B, OFF = 0 B), 10 powtórzeń. Metryki: p99 opóźnienia VoIP i średni
jitter w funkcji N (ten sam program co Scenariusz 1).

```bash
# 2 × 8 × 10 = 160 biegów -> results/scenario2/flowmon-ampdu{on,off}-N<N>-run<R>.xml
./scratch/ns3-wifi6-aggregation-delay/run_scenario2.sh

# p99 + jitter vs N (krzywe ON/OFF z 95% CI)
python3 scratch/ns3-wifi6-aggregation-delay/aggregate_scenario2.py
```

Wynik: `results/scenario2/scenario2_p99_vs_N.pdf` oraz `scenario2_jitter_vs_N.pdf`.

### Scenariusz 3 (Wi-Fi 7 / MLO)

```bash
# MLO on/off × 10 powtórzeń = 20 biegów (uwaga: model widmowy jest wolny)
./scratch/ns3-wifi6-aggregation-delay/run_scenario3.sh
python3 scratch/ns3-wifi6-aggregation-delay/aggregate_scenario3.py
```

Wynik: `results/scenario3/cdf_scenario3_mlo.pdf`.

---

## Generowanie wykresów CDF

```bash
cd scratch/ns3-wifi6-aggregation-delay

# Jedno porównanie — dwie krzywe na jednym wykresie
python3 plot_cdf.py results/flowmon-results-ampdu-on.xml results/flowmon-results-ampdu-off.xml
```

Wynik: `cdf_delay_voip.pdf` w bieżącym katalogu.

---

## Topologia symulacyjna

```
STA1 (bulk UDP, 150 Mbps) ──[5m]── AP ──[5m]── STA2 (VoIP UDP, 60 kbps)
                                  │
                            802.11ax / 5 GHz / 80 MHz
```

- **STA1**: `OnOffApplication`, 1400 B/pakiet, nasycenie kanału (background traffic)
- **STA2**: `UdpClientHelper`, 150 B/pakiet co 20 ms (profil G.729)
- **AP**: `PacketSink` na portach 5001 (bulk) i 5002 (VoIP)

---

## Scenariusze badawcze

| # | Osoba | Scenariusz |
|---|---|---|
| 1 | Dawid Gruszecki | Wpływ limitu A-MPDU na CDF opóźnień VoIP |
| 2 | Iza Skowrońska | Skalowanie opóźnień VoIP z liczbą stacji w tle (1–15 STA) |
| 3 | Szymon Domagała | Porównanie z Wi-Fi 7 (802.11be / MLO) |
