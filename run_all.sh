#!/usr/bin/env bash
#
# run_all.sh — pełna kampania: Scenariusz 1 (Wi-Fi 6) + Scenariusz 3 (Wi-Fi 7),
# a następnie agregacja obu zestawów wyników.
#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="ns3-wifi6-aggregation-delay"

echo "############ START: $(date) ############"

echo "===== Scenariusz 1 (Wi-Fi 6) ====="
bash "${SCRIPT_DIR}/run_scenario1.sh"

echo "===== Scenariusz 3 (Wi-Fi 7 / MLO) ====="
bash "${SCRIPT_DIR}/run_scenario3.sh"

echo "===== Agregacja ====="
cd "${NS3_ROOT}"
python3 "scratch/${PROJECT}/aggregate_scenario1.py" || true
python3 "scratch/${PROJECT}/aggregate_scenario3.py" || true

echo "############ ALL_DONE: $(date) ############"
