#!/bin/bash
# Only ephemeral study workers. Keep the stock module on disk as recovery.
set -euo pipefail
apt-get install -y -qq ethtool build-essential "linux-headers-$(uname -r)"
driver=/opt/sixdb/ena-2.16.0
if [[ ! -e /sys/module/ena/parameters/phc_enable ]]; then
  mkdir -p "$driver"
  curl --fail --retry 2 -sSL https://codeload.github.com/amzn/amzn-drivers/tar.gz/refs/tags/ena_linux_2.16.0 -o "$driver/source.tar.gz"
  sha256sum "$driver/source.tar.gz" > "$SIXDB_RESULTS/ena-source.sha256"
  echo "43f9f36c671e54389f9f2e86d97a5fc930f9f73a0bbe9ad5383b8a6eb9d228eb  $driver/source.tar.gz" | sha256sum -c -
  tar -xzf "$driver/source.tar.gz" -C "$driver" --strip-components=1
  (cd "$driver/kernel/linux/ena" && ENA_PHC_INCLUDE=1 make -j2) > "$SIXDB_RESULTS/ena-build.txt" 2>&1
  modprobe ptp
  rmmod ena
  if ! insmod "$driver/kernel/linux/ena/ena.ko" phc_enable=1; then
    modprobe ena
    exit 1
  fi
  # Re-acquire DHCP routes after replacing the network driver.
  systemctl restart systemd-networkd
  for attempt in {1..30}; do
    if ip route show default | grep -q default; then break; fi
    sleep 1
  done
fi
sha256sum "$driver/source.tar.gz" > "$SIXDB_RESULTS/ena-source.sha256"
