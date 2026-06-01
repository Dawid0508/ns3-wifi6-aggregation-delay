#!/usr/bin/env bash
#
# run_scenario2.sh — kampania pomiarowa dla Scenariusza 2 (sekcja 4.2 raportu).
#
# Skalowalność z obciążeniem: zmienna liczba stacji w tle N, w dwóch
# konfiguracjach agregacji (ON = 6 500 631 B, OFF = 0 B). Metryki pierwotne:
# 99-percentyl opóźnienia VoIP (p99) oraz średni jitter, w funkcji N.
# N niezależnych powtórzeń (różne ziarna RNG).
#
# Wykorzystuje ten sam program co Scenariusz 1 (wifi6-ampdu-latency).
#
# Uruchomienie:
#   ./scratch/ns3-wifi6-aggregation-delay/run_scenario2.sh
# Agregacja:
#   python3 scratch/ns3-wifi6-aggregation-delay/aggregate_scenario2.py
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${NS3_ROOT}"

PROJECT="ns3-wifi6-aggregation-delay"
OUTDIR="scratch/${PROJECT}/results/scenario2"
mkdir -p "${OUTDIR}"

# --- Parametry kampanii (zgodne z planem 4.2) ---------------------------------
N_VALUES=(1 3 5 7 9 11 13 15)   # liczba stacji w tle
RUNS=10                          # liczba powtórzeń na punkt
SIMTIME=30                       # czas symulacji [s]
BULKRATE="150Mbps"               # szybkość OnOff jednej stacji tła
AMPDU_ON=6500631                 # BE_MaxAmpduSize dla wariantu ON [B]
AMPDU_OFF=0                      # BE_MaxAmpduSize dla wariantu OFF [B]
# ------------------------------------------------------------------------------

TOTAL=$(( 2 * ${#N_VALUES[@]} * RUNS ))
i=0
echo "=== Scenariusz 2: {ON,OFF} × ${#N_VALUES[@]} wartości N × ${RUNS} powtórzeń = ${TOTAL} biegów ==="

for TAG in on off; do
  if [ "${TAG}" = "on" ]; then A=${AMPDU_ON}; else A=${AMPDU_OFF}; fi
  for N in "${N_VALUES[@]}"; do
    for ((R=1; R<=RUNS; R++)); do
      i=$(( i + 1 ))
      OUT="${OUTDIR}/flowmon-ampdu${TAG}-N${N}-run${R}.xml"
      printf "[%3d/%3d] ampdu=%-3s N=%-2d run=%-2d -> %s\n" "${i}" "${TOTAL}" "${TAG}" "${N}" "${R}" "${OUT}"
      ./ns3 run "${PROJECT}/wifi6-ampdu-latency \
          --maxAmpdu=${A} --nBackground=${N} --bulkRate=${BULKRATE} \
          --run=${R} --simTime=${SIMTIME} --outFile=${OUT}" \
        2>&1 | grep -iE "zapisany" || true
    done
  done
done

echo "=== Kampania zakończona. Pliki w ${OUTDIR}/ ==="
echo "Agregacja:  python3 scratch/${PROJECT}/aggregate_scenario2.py"
