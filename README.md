# ns3-wifi6-aggregation-delay

Projekt badawczy: **Wpływ agregacji ramek A-MPDU na opóźnienia w standardzie Wi-Fi 6 (802.11ax)**

AGH WIEiT — Iza Skowrońska, Szymon Domagała, Dawid Gruszecki  
Prowadzący: prof. dr hab. inż. Szymon Szott

---

## Struktura projektu

```
ns3-wifi6-aggregation-delay/
├── wifi6-ampdu-latency.cc    # Wi-Fi 6 (802.11ax): sweep A-MPDU, N stacji tła (C++)
├── wifi7-mlo-latency.cc      # Wi-Fi 7 (802.11be): MLO STR pod obciążeniem (C++)
├── run_scenario1.sh          # kampania S1: sweep A-MPDU
├── run_scenario2.sh          # kampania S2: skalowalność z liczbą stacji N
├── run_scenario3.sh          # kampania S3: Wi-Fi 7 MLO on/off
├── run_all.sh                # S1 + S3 + agregacja jednym ciągiem
├── aggregate_scenario1.py    # pooled CDF + percentyle p50/p95/p99 (S1)
├── aggregate_scenario2.py    # p99 + jitter w funkcji N (S2)
├── aggregate_scenario3.py    # pooled CDF MLO on/off (S3)
├── plot_cdf.py               # ad-hoc CDF z 1–2 plików XML
├── CMakeLists.txt            # definicje targetów build_exec dla scratch
└── results/
    ├── scenario1/   # flowmon-ampdu<A>-run<R>.xml + cdf_scenario1_ampdu_sweep.pdf
    ├── scenario2/   # flowmon-ampdu{on,off}-N<N>-run<R>.xml + wykresy p99/jitter
    └── scenario3/   # flowmon-wifi7-mlo{on,off}-...-run<R>.xml + cdf_scenario3_mlo.pdf
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

Główny sposób to **pełne kampanie pomiarowe** (Scenariusze 1–3 poniżej).
Pojedynczy bieg przydaje się tylko do szybkiego sprawdzenia/debugowania — komendy
wykonujemy z katalogu głównego ns-3 (`ns-3.47/`):

```bash
# pojedynczy bieg Wi-Fi 6 (debug): pełna agregacja, 5 stacji tła, 30 s
./ns3 run "ns3-wifi6-aggregation-delay/wifi6-ampdu-latency --maxAmpdu=6500631 --nBackground=5 --run=1 --simTime=30"

# pojedynczy bieg Wi-Fi 7 (debug): MLO włączone
./ns3 run "ns3-wifi6-aggregation-delay/wifi7-mlo-latency --mlo=true --nBackground=5 --run=1 --simTime=30"
```

Bez `--outFile` nazwa pliku XML jest generowana automatycznie do `results/`.

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

## Wykresy ad-hoc (plot_cdf.py)

Do szybkiego porównania CDF z dowolnych 1–2 plików XML (poza agregatorami kampanii):

```bash
cd scratch/ns3-wifi6-aggregation-delay

# --port=5002 = VoIP (domyślnie), --port=5001 = bulk
python3 plot_cdf.py --port=5002 \
    results/scenario1/flowmon-ampdu6500631-run1.xml \
    results/scenario1/flowmon-ampdu0-run1.xml
```

Wynik: `cdf_delay_VoIP.pdf` (nazwa zależna od portu) w bieżącym katalogu.

---

## Topologia symulacyjna

```
        bulk × N (OnOff, 150 Mbps)        VoIP (UdpClient, ~60 kbps)
   STA_bg … STA_bg ─────[5 m]────► AP ◄────[5 m]───── STA_VoIP
                                   │
              Wi-Fi 6: 802.11ax / 5 GHz / 80 MHz
              Wi-Fi 7: 802.11be / 5 GHz (+ 6 GHz przy MLO)
```

- **Stacje tła (N)**: `OnOffApplication`, 1400 B/pakiet — łącznie nasycają kanał i wywołują blokowanie HOL
- **VoIP**: `UdpClientHelper`, 150 B/pakiet co 20 ms (profil G.729), **AC_BE** (bez priorytetu — celowo, by rywalizował z ruchem bulk)
- **AP**: `PacketSink` na portach 5001 (bulk) i 5002 (VoIP)
- Wszystkie STA rozmieszczone na okręgu o promieniu 5 m wokół AP

---

## Scenariusze badawcze

| # | Osoba | Scenariusz |
|---|---|---|
| 1 | Dawid Gruszecki | Wpływ limitu A-MPDU na CDF opóźnień VoIP |
| 2 | Iza Skowrońska | Skalowanie opóźnień VoIP z liczbą stacji w tle (1–15 STA) |
| 3 | Szymon Domagała | Porównanie z Wi-Fi 7 (802.11be / MLO) |
