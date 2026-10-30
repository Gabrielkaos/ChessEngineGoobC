#!/bin/bash
# One-command engine match: NEW engine vs BASE engine, SPRT or fixed length.
#
#   tools/sprt/run_sprt.sh <new-binary> <base-binary> [name] [tc] [concurrency]
#
#   name         label for the log file            (default: basename of new-binary)
#   tc           time control                      (default: 6+0.06)
#   concurrency  games played at the same time     (default: 10, or CPUs / THREADS if lower)
#
# Environment overrides:
#   MODE     gainer  SPRT [0, 3]  - does the change gain Elo?             (default)
#            nonreg  SPRT [-3, 1] - bug fixes, simplifications, cleanups
#            fixed   no SPRT, play exactly ROUNDS game pairs (regression checks)
#   ELO0, ELO1  override the SPRT bounds of the mode
#   MODEL    Elo model of the bounds: logistic (default) or normalized
#   ROUNDS   game pairs to play at most (default 20000 for SPRT, i.e. run to a verdict;
#            2000 for MODE=fixed)
#   RUNNER   fastchess (default when installed) or cutechess
#   BOOK (default tools/book.epd), HASH (MB, default 32),
#   THREADS (search threads per engine, default 1; use 2+ for SMP changes)
#
# fastchess pairs the two games of every opening and computes the SPRT from the
# pentanomial (per game pair) statistics. With an unbalanced book like
# tools/book.epd that needs far fewer games than cutechess's per-game
# (trinomial) SPRT, which treats the two games of a pair as independent.
#
# The log goes to tools/sprt/logs/<name>.log (games in <name>.pgn). Watch it with:
#   grep -E "Elo:|LLR|Ptnml|Games" tools/sprt/logs/<name>.log | tail -5
set -e

NEW=$1; BASE=$2
[ -z "$NEW" ] || [ -z "$BASE" ] && { sed -n 2,28p "$0"; exit 1; }
NEW=$(realpath "$NEW"); BASE=$(realpath "$BASE")
[ -x "$NEW" ]  || { echo "not executable: $NEW";  exit 1; }
[ -x "$BASE" ] || { echo "not executable: $BASE"; exit 1; }

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
NAME=${3:-$(basename "$NEW")}
TC=${4:-6+0.06}
THREADS=${THREADS:-1}
# more busy search threads than CPUs makes the engines share cores, which adds timing noise
NCPU=$(nproc 2>/dev/null || echo 10)
FIT=$(( NCPU / THREADS > 0 ? NCPU / THREADS : 1 ))
CONC=${5:-$(( FIT < 10 ? FIT : 10 ))}
if [ $(( CONC * THREADS )) -gt "$NCPU" ]; then
    echo "warning: $CONC games x $THREADS threads is more than the $NCPU CPUs here"
fi

MODE=${MODE:-gainer}
case "$MODE" in
    gainer) ELO0=${ELO0:-0};  ELO1=${ELO1:-3}; ROUNDS=${ROUNDS:-20000} ;;
    nonreg) ELO0=${ELO0:--3}; ELO1=${ELO1:-1}; ROUNDS=${ROUNDS:-20000} ;;
    fixed)  ROUNDS=${ROUNDS:-2000} ;;
    *) echo "unknown MODE=$MODE (gainer, nonreg or fixed)"; exit 1 ;;
esac
MODEL=${MODEL:-logistic}
BOOK=${BOOK:-$ROOT/tools/book.epd}
HASH=${HASH:-32}

FASTCHESS=${FASTCHESS:-$(command -v fastchess || echo "$HOME/.local/bin/fastchess")}
CUTECHESS=${CUTECHESS:-$(command -v cutechess-cli || echo "$HOME/.local/bin/cutechess-cli")}
if [ -z "$RUNNER" ]; then
    if [ -x "$FASTCHESS" ]; then RUNNER=fastchess; else RUNNER=cutechess; fi
fi
case "$RUNNER" in
    fastchess) [ -x "$FASTCHESS" ] || { echo "fastchess not found (set FASTCHESS=...)"; exit 1; } ;;
    cutechess) [ -x "$CUTECHESS" ] || { echo "cutechess-cli not found (set CUTECHESS=...)"; exit 1; }
               echo "warning: cutechess-cli computes a per-game (trinomial) SPRT; it needs many more games than fastchess" ;;
    *) echo "unknown RUNNER=$RUNNER (fastchess or cutechess)"; exit 1 ;;
esac
[ -f "$BOOK" ] || { echo "opening book not found: $BOOK"; exit 1; }

mkdir -p "$ROOT/tools/sprt/logs"
LOG="$ROOT/tools/sprt/logs/$NAME.log"
PGN="$ROOT/tools/sprt/logs/$NAME.pgn"

SPRT_DESC="sprt=[$ELO0,$ELO1] $MODEL"
[ "$MODE" = fixed ] && SPRT_DESC="fixed $ROUNDS pairs"
echo "NEW : $NEW"
echo "BASE: $BASE"
echo "runner=$RUNNER mode=$MODE $SPRT_DESC tc=$TC concurrency=$CONC threads=$THREADS hash=${HASH}MB book=$(basename "$BOOK")"
echo "log : $LOG"
echo

if [ "$RUNNER" = fastchess ]; then
    SPRT_ARGS=()
    [ "$MODE" != fixed ] && SPRT_ARGS=(-sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05 model="$MODEL")
    # the autosaved tournament state (for -config resuming) lives next to the log
    "$FASTCHESS" \
      -engine cmd="$NEW"  name=NEW \
      -engine cmd="$BASE" name=BASE \
      -each proto=uci tc="$TC" option.Hash="$HASH" option.Threads="$THREADS" \
      -openings file="$BOOK" format=epd order=random -repeat \
      -rounds "$ROUNDS" -concurrency "$CONC" \
      "${SPRT_ARGS[@]}" \
      -draw movenumber=40 movecount=8 score=10 \
      -resign movecount=3 score=400 \
      -ratinginterval 20 -recover \
      -pgnout file="$PGN" \
      -config outname="$ROOT/tools/sprt/logs/$NAME.state.json" \
      2>&1 | tee "$LOG"
else
    SPRT_ARGS=()
    [ "$MODE" != fixed ] && SPRT_ARGS=(-sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05)
    "$CUTECHESS" \
      -engine cmd="$NEW"  name=NEW \
      -engine cmd="$BASE" name=BASE \
      -each proto=uci tc="$TC" option.Hash="$HASH" option.Threads="$THREADS" \
      -openings file="$BOOK" format=epd order=random -repeat \
      -rounds "$ROUNDS" -games 2 -concurrency "$CONC" \
      "${SPRT_ARGS[@]}" \
      -draw movenumber=40 movecount=8 score=10 \
      -resign movecount=3 score=400 \
      -ratinginterval 10 -recover \
      -pgnout "$PGN" \
      2>&1 | tee "$LOG"
fi
