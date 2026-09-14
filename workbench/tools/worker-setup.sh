#!/bin/bash
# Minimal research toolchain on the pinned Ubuntu 24.04 worker image.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
# Retry failed metadata/package fetches; exhausted updates must not silently use old indexes.
apt_get() {
  apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 \
    -o Acquire::https::Timeout=30 -o APT::Update::Error-Mode=any "$@"
}
apt_get update -qq
apt_get install -y -qq ca-certificates curl gnupg git cmake ninja-build \
  g++ pkg-config time python3-yaml
clang_version='clang version 21[.]1[.]8([[:space:]]|$)'
if ! clang++-21 --version 2>/dev/null | grep -Eq "$clang_version"; then
  curl --fail --retry 3 -sS https://apt.llvm.org/llvm-snapshot.gpg.key \
    -o /tmp/sixdb-llvm.key
  gpg --batch --yes --dearmor -o /usr/share/keyrings/sixdb-llvm.gpg /tmp/sixdb-llvm.key
  echo 'deb [signed-by=/usr/share/keyrings/sixdb-llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main' \
    > /etc/apt/sources.list.d/sixdb-llvm.list
  apt_get update -qq
  apt_get install -y -qq clang-21 llvm-21 libclang-rt-21-dev
fi
clang++-21 --version | grep -E "$clang_version"
# Finish package work before measurement; do not let periodic apt jobs overlap it.
systemctl stop apt-daily.timer apt-daily-upgrade.timer
systemctl stop apt-daily.service apt-daily-upgrade.service
