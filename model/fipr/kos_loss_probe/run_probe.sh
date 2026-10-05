#!/bin/bash
# upload and run the loss probe on the console, retrying until it accepts the upload
cd "$(dirname "$0")"
DCT=$(git rev-parse --show-toplevel)/build/tools/dcload-ip/host-src/tool/dc-tool-ip
rm -f probe_hw.bin
for attempt in $(seq 1 20); do
  rm -f dctool.log
  ( timeout 600 stdbuf -oL $DCT -t ${HW_IP:?"set HW_IP to the console address"} -x loss_probe.elf > dctool.log 2>&1; echo "exit $?" >> dctool.log ) &
  pid=$!
  for i in $(seq 1 8); do sleep 3; grep -q "Executing" dctool.log 2>/dev/null && break; done
  if grep -q "Executing" dctool.log 2>/dev/null; then
    echo "attempt $attempt: running"; wait $pid; echo "finished"; exit 0
  fi
  pkill -x dc-tool-ip; sleep 2; echo "attempt $attempt: no response"
done
echo "gave up"; exit 1
