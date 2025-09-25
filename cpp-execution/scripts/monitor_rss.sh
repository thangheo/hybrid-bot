#!/usr/bin/env zsh
# Simple RSS monitor for a process by name
# Usage: ./monitor_rss.sh <process-name-or-pattern> [interval_seconds] [output_file]
# Example: ./monitor_rss.sh semi_hft 1 ./rss_log.txt

PROG_NAME=${1:-semi_hft}
INTERVAL=${2:-1}
OUT=${3:-./rss_log.txt}

echo "Monitoring process pattern '$PROG_NAME' every ${INTERVAL}s, logging to $OUT"
while true; do
  TS=$(date +"%Y-%m-%d %H:%M:%S")
  PIDS=( $(pgrep -f "$PROG_NAME") )
  if (( ${#PIDS[@]} == 0 )); then
    echo "$TS - no process found for '$PROG_NAME'" >> "$OUT"
  else
    for PID in ${PIDS[@]}; do
      if [[ -r "/proc/$PID/status" ]]; then
        RSS_KB=$(awk '/VmRSS/ {print $2}' /proc/$PID/status)
        VSIZE_KB=$(awk '/VmSize/ {print $2}' /proc/$PID/status)
        echo "$TS PID:$PID RSS_kB:$RSS_KB VSize_kB:$VSIZE_KB" >> "$OUT"
      else
        echo "$TS PID:$PID status file not readable" >> "$OUT"
      fi
    done
  fi
  sleep "$INTERVAL"
done
