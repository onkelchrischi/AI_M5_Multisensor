#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# pio run installs the same pinned ArduinoJson headers used by the firmware.
headers='.pio/libdeps/m5stack-core2/ArduinoJson/src'
if [[ ! -d "$headers" ]]; then
  echo 'ArduinoJson headers missing. Run pio run first.' >&2
  exit 1
fi
binary=$(mktemp /tmp/sensor-engine-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Iinclude -I"$headers" tests/protocol_engine_test.cpp -o "$binary"
"$binary"
