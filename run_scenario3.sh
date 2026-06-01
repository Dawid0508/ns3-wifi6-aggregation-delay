#!/usr/bin/env bash
#
# run_scenario3.sh — kampania pomiarowa dla Scenariusza 3 (Wi-Fi 7 / MLO).
#
# Ta sama metodologia co Scenariusz 1: nBackground stacji nasyca wspólny kanał
# 5 GHz, stacja VoIP konkuruje o medium. Oś badawcza: MLO ON vs OFF.
# N niezależnych powtórzeń (różne ziarna RNG).
#
# UWAGA: model widmowy (SpectrumChannel) + EHT są kosztowne — pojedynczy bieg
# 30 s z 5 stacjami tła trwa ~10 min. Cała kampania (20 biegów) to kilka godzin.
#
# Uruchomienie:
#   ./scratch/ns3-wifi6-aggregation-delay/run_scenario3.sh
# Agregacja:
#   python3 scratch/ns3-wifi6-aggregation-delay/aggregate_scenario3.py
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${NS3_ROOT}"

PROJECT="ns3-wifi6-aggregation-delay"
OUTDIR="scratch/${PROJECT}/results/scenario3"
mkdir -p "${OUTDIR}"

# --- Parametry kampanii -------------------------------------------------------
MLO_VALUES=(true false)   # MLO ON / OFF
RUNS=10                    # liczba powtórzeń na wariant
NBG=5                      # liczba stacji tła (jak w Scenariuszu 1)
SIMTIME=30                 # czas symulacji [s]
BULKRATE="150Mbps"         # szybkość OnOff jednej stacji tła
# ------------------------------------------------------------------------------

TOTAL=$(( ${#MLO_VALUES[@]} * RUNS ))
i=0
echo "=== Scenariusz 3: MLO {ON,OFF} × ${RUNS} powtórzeń = ${TOTAL} biegów ==="

for M in "${MLO_VALUES[@]}"; do
  TAG=$([ "${M}" = "true" ] && echo "on" || echo "off")
  for ((R=1; R<=RUNS; R++)); do
    i=$(( i + 1 ))
    OUT="${OUTDIR}/flowmon-wifi7-mlo${TAG}-bg${NBG}-run${R}.xml"
    printf "[%2d/%2d] MLO=%-5s run=%-2d -> %s\n" "${i}" "${TOTAL}" "${M}" "${R}" "${OUT}"
    ./ns3 run "${PROJECT}/wifi7-mlo-latency \
        --mlo=${M} --nBackground=${NBG} --bulkRate=${BULKRATE} \
        --run=${R} --simTime=${SIMTIME} --outFile=${OUT}" \
      2>&1 | grep -iE "zapisane" || true
  done
done

echo "=== Kampania zakończona. Pliki w ${OUTDIR}/ ==="
echo "Agregacja:  python3 scratch/${PROJECT}/aggregate_scenario3.py"
