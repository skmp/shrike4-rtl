#!/bin/bash
# hwrun.sh ELF [LOG] -- run one KOS program on the Dreamcast (dcload-ip) and print its console output.
#
# THE ONLY WAY to run anything on the console: every caller takes an exclusive lock on $LOCK first, so two tests can
# never run at the same time (a second caller waits; HW_LOCK_WAIT seconds, default 3600, then gives up with exit 3).
# The upload is retried until dcload accepts it (the console may still be rebooting from the previous run).  The run
# is killed after HW_TIMEOUT seconds (default 300).  Output: the program's dcload console output on stdout, also kept in
# LOG (default <elf>.hwlog).  Exit status: the program's exit code as dc-tool reports it, 2 if the upload never
# started, 124 on timeout, 3 if the lock was not obtained.
#   HW_IP (the console's address: the environment, or HW_IP=... in the untracked tools/hw/hwrun.conf),
#   HW_TIMEOUT, HW_LOCK_WAIT.
# Programs: build with the KOS toolchain (source /opt/toolchains/dc/kos/environ.sh; see tools/hw/hello for a template).
# A program should print its results and return from main(): dcload then gets the exit code and the console returns to
# the dcload menu for the next run.
set -u
ELF=$(readlink -f "$1"); LOG=${2:-$ELF.hwlog}
HERE=$(dirname "$(readlink -f "$0")")
[ -z "${HW_IP:-}" ] && [ -f "$HERE/hwrun.conf" ] && . "$HERE/hwrun.conf"
IP=${HW_IP:?"hwrun: set HW_IP to the console address (environment or tools/hw/hwrun.conf)"}
TMO=${HW_TIMEOUT:-300}; WAIT=${HW_LOCK_WAIT:-3600}
DCT=$(readlink -f "$HERE/../../build/tools/dcload-ip/host-src/tool/dc-tool-ip")
LOCK=/tmp/shrike4-dreamcast-hw.lock
[ -f "$ELF" ] || { echo "hwrun: no such file $ELF" >&2; exit 2; }
exec 9>"$LOCK"
if ! flock -w "$WAIT" 9; then echo "hwrun: console busy for ${WAIT}s, giving up" >&2; exit 3; fi
echo "hwrun: $(date +%T) lock taken by pid $$ for $ELF" >&2
for attempt in $(seq 1 30); do
  rm -f "$LOG"
  ( timeout "$TMO" stdbuf -oL "$DCT" -t "$IP" -x "$ELF" > "$LOG" 2>&1; echo "hwrun-exit $?" >> "$LOG" ) &
  pid=$!
  for i in $(seq 1 10); do sleep 2; grep -q "Executing" "$LOG" 2>/dev/null && break; done
  if grep -q "Executing" "$LOG" 2>/dev/null; then
    wait $pid
    sed -n '/Executing/,$p' "$LOG" | grep -v "^hwrun-exit"
    rc=$(sed -n 's/^hwrun-exit //p' "$LOG" | tail -1)
    code=$(sed -n 's/.*exit return code \([0-9-]*\).*/\1/p' "$LOG" | tail -1)
    [ "$rc" = "124" ] && { echo "hwrun: timeout after ${TMO}s" >&2; exit 124; }
    exit "${code:-$rc}"
  fi
  pkill -f "$DCT -t $IP" 2>/dev/null; wait $pid 2>/dev/null; sleep 3
  echo "hwrun: attempt $attempt: no response" >&2
done
echo "hwrun: upload never started" >&2; exit 2
