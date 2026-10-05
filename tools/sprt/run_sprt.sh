#!/bin/bash
# One-command SPRT: NEW engine vs BASE engine with cutechess-cli.
#
#   tools/sprt/run_sprt.sh <new-binary> <base-binary> [name] [tc] [concurrency]
#
#   name         label for the log file            (default: basename of new-binary)
#   tc           time control                      (default: 6+0.06)
#   concurrency  games played at the same time     (default: 6)
#
# Environment overrides: ELO0, ELO1 (SPRT hypotheses, default 0 / 5),
#                        ROUNDS (default 1500 rounds = 3000 games max),
#                        BOOK (default tools/book.epd), HASH (MB, default 32)
#
# The log goes to tools/sprt/logs/<name>.log. Watch it with:
#   grep -E "Score of|Elo diff|SPRT" tools/sprt/logs/<name>.log | tail -3
set -e

NEW=$1; BASE=$2
[ -z "$NEW" ] || [ -z "$BASE" ] && { sed -n 2,16p "$0"; exit 1; }
NEW=$(realpath "$NEW"); BASE=$(realpath "$BASE")
[ -x "$NEW" ]  || { echo "not executable: $NEW";  exit 1; }
[ -x "$BASE" ] || { echo "not executable: $BASE"; exit 1; }

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
NAME=${3:-$(basename "$NEW")}
TC=${4:-6+0.06}
CONC=${5:-6}
ELO0=${ELO0:-0}
ELO1=${ELO1:-5}
ROUNDS=${ROUNDS:-1500}
BOOK=${BOOK:-$ROOT/tools/book.epd}
HASH=${HASH:-32}
CUTECHESS=${CUTECHESS:-$(command -v cutechess-cli || echo "$HOME/.local/bin/cutechess-cli")}

[ -x "$CUTECHESS" ] || { echo "cutechess-cli not found (set CUTECHESS=...)"; exit 1; }
[ -f "$BOOK" ]      || { echo "opening book not found: $BOOK"; exit 1; }

mkdir -p "$ROOT/tools/sprt/logs"
LOG="$ROOT/tools/sprt/logs/$NAME.log"

echo "NEW : $NEW"
echo "BASE: $BASE"
echo "tc=$TC concurrency=$CONC sprt=[$ELO0,$ELO1] hash=${HASH}MB book=$(basename "$BOOK")"
echo "log : $LOG"
echo

exec "$CUTECHESS" \
  -engine cmd="$NEW"  name=NEW \
  -engine cmd="$BASE" name=BASE \
  -each proto=uci tc="$TC" option.Hash="$HASH" \
  -openings file="$BOOK" format=epd order=random -repeat \
  -rounds "$ROUNDS" -games 2 -concurrency "$CONC" \
  -sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05 \
  -draw movenumber=40 movecount=8 score=10 \
  -resign movecount=3 score=400 \
  -ratinginterval 20 -recover \
  2>&1 | tee "$LOG"
