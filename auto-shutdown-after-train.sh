#!/usr/bin/env bash
set -Eeuo pipefail

# Wait for the existing NNUE train.py process to finish, then shut down the PC.

pattern='tools/nnue_project/scripts/train\.py'

mapfile -t all_pids < <(pgrep -f "$pattern" || true)

# Keep only top-level train.py processes (exclude forked DataLoader worker children)
pids=()
for pid in "${all_pids[@]}"; do
    [[ "$pid" == "$$" || "$pid" == "$PPID" ]] && continue
    ppid=$(ps -p "$pid" -o ppid= 2>/dev/null | tr -d ' ' || true)
    is_child=0
    for other in "${all_pids[@]}"; do
        if [[ "$ppid" == "$other" ]]; then
            is_child=1
            break
        fi
    done
    if (( ! is_child )); then
        pids+=("$pid")
    fi
done

if (( ${#pids[@]} == 0 )); then
    echo "No running train.py process found."
    echo "Start train.py first, then run this script again."
    exit 1
fi

if (( ${#pids[@]} > 1 )); then
    echo "Found more than one top-level train.py process; refusing to schedule shutdown:"
    printf '  PID %s\n' "${pids[@]}"
    exit 1
fi

pid="${pids[0]}"
echo "Found train.py process PID $pid:"
ps -p "$pid" -o pid,etime,args
echo
echo "This will shut down the computer when train.py (PID $pid) finishes."
read -r -p "Continue? Type YES to schedule shutdown: " confirm
if [[ "$confirm" != "YES" ]]; then
    echo "Cancelled."
    exit 0
fi

unit="nnue-train-shutdown-$(date +%s)"
echo "Enter your password if prompted. The watcher will run as a system service."
sudo systemd-run --no-block --unit="$unit" --service-type=oneshot \
    /bin/bash -c 'pid="$1"; while kill -0 "$pid" 2>/dev/null; do sleep 30; done; /usr/sbin/shutdown -h now' \
    _ "$pid"

echo
echo "Shutdown watcher scheduled as: ${unit}.service"
echo "Check it with: systemctl status ${unit}.service --no-pager"
echo "Cancel it with: sudo systemctl stop ${unit}.service"
echo "You can close the terminal. The systemd service will keep running."
