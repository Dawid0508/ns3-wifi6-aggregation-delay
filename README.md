# ns3-wifi6-aggregation-delay

Projekt badawczy: **Wpływ agregacji ramek A-MPDU na opóźnienia w standardzie Wi-Fi 6 (802.11ax)**

AGH WIEiT — Iza Skowrońska, Szymon Domagała, Dawid Gruszecki  
Prowadzący: prof. dr hab. inż. Szymon Szott

---

## Struktura projektu

```
ns3-wifi6-aggregation-delay/
├── wifi6-ampdu-latency.cc    # skrypt symulacyjny ns-3 (C++)
├── plot_cdf.py               # skrypt analizy i wykresów CDF (Python 3)
├── report_sections.tex       # draft raportu (LaTeX)
└── results/
    ├── flowmon-results-ampdu-on.xml   # wyniki FlowMonitor z agregacją
    ├── flowmon-results-ampdu-off.xml  # wyniki FlowMonitor bez agregacji
    └── cdf_delay_voip.pdf             # wykres CDF opóźnień VoIP
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
```

Wyniki XML zapisywane do `results/`.

### Parametry CLI

| Parametr | Domyślnie | Opis |
|---|---|---|
| `--ampdu` | `true` | Włącz/wyłącz agregację A-MPDU |
| `--simTime` | `15` | Czas symulacji w sekundach |

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
