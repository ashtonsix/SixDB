#!/bin/bash
set -euo pipefail
python3 workbench/spikes/ikea-composition/handle-boundaries/predicate-native.py run "$@"
