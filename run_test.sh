#!/usr/bin/env bash
# Run all executables under cpp-execution/bin or cpp-execution/build/tests with 5s timeout each

set -uo pipefail

ROOT_DIR="$(pwd)"
DIRS=("cpp-execution/bin" "cpp-execution/build/tests")
MAX_TIME="5s"
TIMEOUT_CMD="$(command -v timeout || true)"
if [[ -z "$TIMEOUT_CMD" ]]; then
    echo "error: 'timeout' command not found in PATH" >&2
    exit 2
fi

tests=()
for d in "${DIRS[@]}"; do
    p="$ROOT_DIR/$d"
    if [[ -d "$p" ]]; then
        while IFS= read -r -d '' f; do
            tests+=("$f")
        done < <(find "$p" -maxdepth 1 -type f -executable -print0)
    fi
done

if [[ ${#tests[@]} -eq 0 ]]; then
    echo "No test executables found in ${DIRS[*]}"
    exit 0
fi

passed=0
failed=0
timedout=0

for t in "${tests[@]}"; do
    echo "=== Running: $t ==="
    # run with timeout, don't let script exit on non-zero
    "$TIMEOUT_CMD" --preserve-status "$MAX_TIME" "$t"
    rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "PASS: $t"
        ((passed++))
    elif [[ $rc -eq 124 || $rc -eq 137 ]]; then
        echo "TIMEOUT: $t (returned $rc)"
        ((timedout++))
    else
        echo "FAIL: $t (returned $rc)"
        ((failed++))
    fi
done

echo
echo "Summary: total=${#tests[@]} passed=$passed failed=$failed timedout=$timedout"
if (( failed>0 || timedout>0 )); then
    exit 1
else
    exit 0
fi