#!/bin/bash
# Minimal research toolchain on the pinned Ubuntu 24.04 worker image.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq ca-certificates curl gnupg git cmake ninja-build \
  g++ pkg-config time python3-yaml
if ! clang++-21 --version 2>/dev/null | grep -q 'clang version 21.1.8'; then
  curl --fail --retry 3 -sS https://apt.llvm.org/llvm-snapshot.gpg.key \
    -o /tmp/sixdb-llvm.key
  gpg --batch --yes --dearmor -o /usr/share/keyrings/sixdb-llvm.gpg /tmp/sixdb-llvm.key
  echo 'deb [signed-by=/usr/share/keyrings/sixdb-llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main' \
    > /etc/apt/sources.list.d/sixdb-llvm.list
  apt-get update -qq
  apt-get install -y -qq clang-21 llvm-21 libclang-rt-21-dev
fi
clang++-21 --version | grep 'clang version 21.1.8'
# Finish package work before measurement; do not let periodic apt jobs overlap it.
systemctl stop apt-daily.timer apt-daily-upgrade.timer
systemctl stop apt-daily.service apt-daily-upgrade.service
