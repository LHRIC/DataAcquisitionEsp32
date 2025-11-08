#!/usr/bin/env bash
# usage: sudo ./send_can.sh <kbps>
# Example: sudo ./send_can.sh 100   # send at rate matching ~100 kbps bus load

set -euo pipefail

KBPS="${1:?kbps required}"
IFACE="can0"
FRAME="123#DEADBEEF"

ip link set "$IFACE" down 2>/dev/null || true
ip link set "$IFACE" type can bitrate 1000000 fd off
ip link set "$IFACE" txqueuelen 1000
ip link set "$IFACE" up

FRAME_BITS=144

MSG_PER_SEC=$(awk -v kb="$KBPS" -v fb="$FRAME_BITS" 'BEGIN { printf("%.3f", (kb*1000)/fb) }')

INTERVAL=$(awk -v r="$MSG_PER_SEC" 'BEGIN { printf("%.6f", 1.0/r) }')

echo "Target bus load: ${KBPS} kbps"
echo "≈ ${MSG_PER_SEC} msgs/sec, interval ≈ ${INTERVAL}s"

while true; do
    cansend "$IFACE" "$FRAME"
    sleep "$INTERVAL"
done
