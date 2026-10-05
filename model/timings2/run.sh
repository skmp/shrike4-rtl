#!/bin/bash
# run.sh [pass] [event_hex] [ip]  -- upload runner/t2run.elf to the Dreamcast (dcload-ip) and let it work through
# tests/chunk_*.bin; results land in results/<pass>/.  Retries until the console accepts the upload.  With no tests
# present the runner only writes probe.txt.  A pass can be resumed: chunks with a result file are skipped.
cd "$(dirname "$0")"
PASS=${1:-p0}; EVENT=${2:-14}; IP=${3:-${HW_IP:?"set HW_IP to the console address"}}
DCT=$(git rev-parse --show-toplevel)/build/tools/dcload-ip/host-src/tool/dc-tool-ip
mkdir -p results/$PASS
echo "pass=$PASS event=$EVENT n1=${N1:-8} n2=${N2:-136} reps=${REPS:-3} verbose=${VERBOSE:-0} stop=${STOP:-0} bracket=${BRACKET:-0}" > run.cfg
rm -f retry.log
for attempt in $(seq 1 30); do
  rm -f dctool.log
  ( timeout ${TIMEOUT:-14400} stdbuf -oL $DCT -t $IP -x runner/t2run.elf > dctool.log 2>&1; echo "exit $?" >> dctool.log ) &
  pid=$!
  for i in $(seq 1 8); do sleep 5; grep -q "Executing" dctool.log 2>/dev/null && break; done
  if grep -q "Executing" dctool.log 2>/dev/null; then echo "attempt $attempt: running" >> retry.log; wait $pid; echo "finished" >> retry.log
    cp tests/cases.jsonl results/$PASS/cases.jsonl; cp dctool.log results/$PASS/dctool.log; exit 0; fi
  pkill -x dc-tool-ip; sleep 2; echo "attempt $attempt: no response" >> retry.log
done
echo "gave up" >> retry.log; exit 1
