#!/bin/bash
set -euo pipefail
python3 workbench/spikes/ikea-composition/handle-boundaries/projection-native.py run "$@"
