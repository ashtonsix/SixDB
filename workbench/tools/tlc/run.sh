#!/bin/bash
set -euo pipefail
if ! command -v javac >/dev/null; then
  apt-get update -qq
  apt-get install -y -qq openjdk-21-jdk-headless
fi
exec python3 workbench/tools/tlc/tlc.py run "$@"
