#!/bin/bash
# retry the dump run until the console accepts the upload (prints "Executing"), then let it run to completion
cd "$(dirname "$0")"
DCT=$(git rev-parse --show-toplevel)/build/tools/dcload-ip/host-src/tool/dc-tool-ip
for attempt in $(seq 1 60); do
  rm -f dctool.log
  ( timeout 2400 stdbuf -oL $DCT -t ${HW_IP:?"set HW_IP to the console address"} -x fipr_soak.elf > dctool.log 2>&1; echo "exit $?" >> dctool.log ) &
  pid=$!
  for i in $(seq 1 8); do sleep 5; grep -q "Executing" dctool.log 2>/dev/null && break; done
  if grep -q "Executing" dctool.log 2>/dev/null; then echo "attempt $attempt: running" >> retry.log; wait $pid; echo "finished" >> retry.log; exit 0; fi
  pkill -x dc-tool-ip; sleep 2; echo "attempt $attempt: no response" >> retry.log
done
echo "gave up" >> retry.log
