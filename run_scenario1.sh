#!/usr/bin/env bash
#
# run_scenario1.sh — kampania pomiarowa dla Scenariusza 1 (sekcja 4.1 raportu).
#
# Sweep limitu agregacji BE_MaxAmpduSize po pięciu wartościach, przy stałej
# liczbie stacji tła, z N niezależnymi powtórzeniami (różne ziarna RNG).
# Każdy bieg zapisuje osobny plik FlowMonitor XML do results/scenario1/.
#
# Uruchomienie (z dowolnego katalogu):
#   ./scratch/ns3-wifi6-aggregation-delay/run_scenario1.sh
#
# Wyniki agreguje następnie:
#   python3 scratch/ns3-wifi6-aggregation-delay/aggregate_scenario1.py
#
set -euo pipefail

# Przejdź do katalogu głównego ns-3 (dwa poziomy nad tym skryptem: scratch/<proj>/).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${NS3_ROOT}"

PROJECT="ns3-wifi6-aggregation-delay"
OUTDIR="scratch/${PROJECT}/results/scenario1"
mkdir -p "${OUTDIR}"

# --- Parametry kampanii (zgodne z planem 4.1) ---------------------------------
AMPDU_VALUES=(0 8000 65535 524287 6500631)   # BE_MaxAmpduSize [B]
RUNS=10                                       # liczba powtórzeń na punkt
NBG=5                                          # liczba stacji tła
SIMTIME=30                                     # czas symulacji [s]
BULKRATE="150Mbps"                             # szybkość OnOff jednej stacji tła
# ------------------------------------------------------------------------------

TOTAL=$(( ${#AMPDU_VALUES[@]} * RUNS ))
i=0
echo "=== Scenariusz 1: ${#AMPDU_VALUES[@]} wartości A-MPDU × ${RUNS} powtórzeń = ${TOTAL} biegów ==="

for A in "${AMPDU_VALUES[@]}"; do
  for ((R=1; R<=RUNS; R++)); do
    i=$(( i + 1 ))
    OUT="${OUTDIR}/flowmon-ampdu${A}-run${R}.xml"
    printf "[%2d/%2d] A-MPDU=%-8s B  run=%-2d -> %s\n" "${i}" "${TOTAL}" "${A}" "${R}" "${OUT}"
    ./ns3 run "${PROJECT}/wifi6-ampdu-latency \
        --maxAmpdu=${A} --nBackground=${NBG} --bulkRate=${BULKRATE} \
        --run=${R} --simTime=${SIMTIME} --outFile=${OUT}" \
      2>&1 | grep -iE "zapisany" || true
  done
done

echo "=== Kampania zakończona. Pliki w ${OUTDIR}/ ==="
echo "Agregacja:  python3 scratch/${PROJECT}/aggregate_scenario1.py"
