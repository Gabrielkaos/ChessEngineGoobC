#!/usr/bin/env bash
set -Eeuo pipefail

# Wait for the existing SPSA process to finish, then shut down the PC.
# Run this while tools/spsa/spsa.py is already running.

pattern='tools/spsa/spsa.py'

mapfile -t pids < <(pgrep -f "$pattern" | while read -r pid; do
    # Exclude this script and its parent shell/command lines.
    [[ "$pid" == "$$" || "$pid" == "$PPID" ]] && continue
    cmd=$(ps -p "$pid" -o args= 2>/dev/null || true)
    [[ "$cmd" == *"python3 tools/spsa/spsa.py"* || "$cmd" == *"python tools/spsa/spsa.py"* ]] && echo "$pid"
done)

if (( ${#pids[@]} == 0 )); then
    echo "No running Python SPSA process found matching: $pattern"
    echo "Start the SPSA job first, then run this script again."
    exit 1
fi

if (( ${#pids[@]} > 1 )); then
    echo "Found more than one SPSA process; refusing to schedule shutdown:"
    printf '  PID %s\n' "${pids[@]}"
    echo "Ensure only the intended SPSA job is running, then retry."
    exit 1
fi

pid="${pids[0]}"
echo "Found SPSA process PID $pid:"
ps -p "$pid" -o pid,etime,args
echo
echo "This will shut down the computer when that process exits."
read -r -p "Continue? Type YES to schedule shutdown: " confirm
if [[ "$confirm" != "YES" ]]; then
    echo "Cancelled."
    exit 0
fi

unit="spsa-shutdown-$(date +%s)"
echo "Enter your password if prompted. The watcher will run as a system service."
sudo systemd-run --no-block --unit="$unit" --service-type=oneshot \
    /bin/bash -c 'pid="$1"; while kill -0 "$pid" 2>/dev/null; do sleep 30; done; /usr/sbin/shutdown -h now' \
    _ "$pid"

echo
echo "Shutdown watcher scheduled as: ${unit}.service"
echo "Check it with: systemctl status ${unit}.service --no-pager"
echo "You can close the terminal. The systemd service will keep running."
