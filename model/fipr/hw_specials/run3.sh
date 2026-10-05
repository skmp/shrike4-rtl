#!/bin/bash
# upload fpu_pr1_enables.elf to the Dreamcast (dcload-ip) and let it run; retries until the console accepts the upload
cd "$(dirname "$0")"
DCT=$(git rev-parse --show-toplevel)/build/tools/dcload-ip/host-src/tool/dc-tool-ip
IP=${1:-${HW_IP:?"set HW_IP to the console address"}}
rm -f retry.log
for attempt in $(seq 1 20); do
  rm -f dctool.log
  ( timeout 900 stdbuf -oL $DCT -t $IP -x fpu_pr1_enables.elf > dctool.log 2>&1; echo "exit $?" >> dctool.log ) &
  pid=$!
  for i in $(seq 1 8); do sleep 5; grep -q "Executing" dctool.log 2>/dev/null && break; done
  if grep -q "Executing" dctool.log 2>/dev/null; then echo "attempt $attempt: running" >> retry.log; wait $pid; echo "finished" >> retry.log; exit 0; fi
  pkill -x dc-tool-ip; sleep 2; echo "attempt $attempt: no response" >> retry.log
done
echo "gave up" >> retry.log; exit 1
