#!/usr/bin/env bash
# Reproduce every measurement used in the report and write CSVs into results/.
#
# Usage (from the project root):
#   make mainMPI && make main
#   scripts/run_experiments.sh
#
# Configure via environment variables:
#   PROCS      process counts to test          (default "1 2 4 8")
#   REPEATS    runs per point, best is kept     (default 3)
#   HOSTFILE   MPI hostfile (cluster)           (default: none -> local)
#   MPIRUN     launcher                         (default "mpirun")
#   STRONG_LEN / STRONG_N   strong-scaling problem (default 5 / 5000000)
#   WEAK_LEN   / WEAK_PER   weak-scaling length / candidates-per-process
#   SWEEP_P    process count for the interval sweep (default: last of PROCS)
#   OVERSUB=1  add --oversubscribe (local testing only)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$ROOT/results"; mkdir -p "$OUT"

PROCS="${PROCS:-1 2 4 8}"
REPEATS="${REPEATS:-3}"
MPIRUN="${MPIRUN:-mpirun}"
STRONG_LEN="${STRONG_LEN:-5}"; STRONG_N="${STRONG_N:-5000000}"
WEAK_LEN="${WEAK_LEN:-5}";     WEAK_PER="${WEAK_PER:-1000000}"
SWEEP_P="${SWEEP_P:-$(echo $PROCS | awk '{print $NF}')}"
TARGET_ENC="${TARGET_ENC:-./files/myfile.enc}"
TARGET_SHA="${TARGET_SHA:-./files/myfile.sha512}"

LAUNCH=("$MPIRUN")
[ -n "${HOSTFILE:-}" ] && LAUNCH+=(--hostfile "$HOSTFILE")
[ "${OVERSUB:-0}" = "1" ] && LAUNCH+=(--oversubscribe)
# Extra launcher flags, e.g. MPI_EXTRA="--map-by node" to spread processes
# round-robin across nodes for a fair distribution.
[ -n "${MPI_EXTRA:-}" ] && LAUNCH+=($MPI_EXTRA)

# Extract "time=<x> s" from a bench/crack line.
time_of() { sed -n 's/.*time=\([0-9.]*\) s.*/\1/p'; }

# Run a bench point REPEATS times, echo the smallest wall time.
best_bench() { # args: procs, extra flags...
  local p="$1"; shift
  local best=""
  for _ in $(seq "$REPEATS"); do
    local t
    t=$("${LAUNCH[@]}" -np "$p" ./mainMPI --bench "$@" | time_of)
    [ -z "$t" ] && continue
    if [ -z "$best" ] || awk "BEGIN{exit !($t<$best)}"; then best="$t"; fi
  done
  echo "$best"
}

variant_flags() { # name -> flags
  case "$1" in
    baseline) echo "--partition block  --sync collective";;
    opt1)     echo "--partition cyclic --sync collective";;
    opt2)     echo "--partition cyclic --sync p2p";;
  esac
}

echo "== sequential baseline (for reference throughput) =="
SEQ_T=$(./main --bench "$STRONG_LEN" "$STRONG_N" | time_of)
echo "seq_len,$STRONG_LEN,seq_count,$STRONG_N,seq_time_s,$SEQ_T" > "$OUT/sequential.csv"
echo "  sequential: $SEQ_T s for $STRONG_N candidates"

echo "== strong scaling (fixed N=$STRONG_N, len=$STRONG_LEN) =="
echo "version,procs,count,time_s,throughput" > "$OUT/strong.csv"
for v in baseline opt1 opt2; do
  for p in $PROCS; do
    t=$(best_bench "$p" "$STRONG_LEN" "$STRONG_N" $(variant_flags "$v"))
    [ -z "$t" ] && continue
    thr=$(awk "BEGIN{printf \"%.0f\", $STRONG_N/$t}")
    echo "$v,$p,$STRONG_N,$t,$thr" >> "$OUT/strong.csv"
    echo "  $v P=$p -> $t s ($thr cand/s)"
  done
done

echo "== weak scaling (N=$WEAK_PER * P, len=$WEAK_LEN) =="
echo "version,procs,count,time_s,throughput" > "$OUT/weak.csv"
for v in baseline opt1 opt2; do
  for p in $PROCS; do
    n=$((WEAK_PER * p))
    t=$(best_bench "$p" "$WEAK_LEN" "$n" $(variant_flags "$v"))
    [ -z "$t" ] && continue
    thr=$(awk "BEGIN{printf \"%.0f\", $n/$t}")
    echo "$v,$p,$n,$t,$thr" >> "$OUT/weak.csv"
    echo "  $v P=$p N=$n -> $t s ($thr cand/s)"
  done
done

echo "== interval sweep (P=$SWEEP_P, collective vs p2p) =="
echo "sync,interval,time_s,throughput" > "$OUT/interval.csv"
for sync in collective p2p; do
  for iv in 1 16 64 256 1024 4096 16384; do
    t=$(best_bench "$SWEEP_P" "$STRONG_LEN" "$STRONG_N" --partition cyclic --sync "$sync" --interval "$iv")
    [ -z "$t" ] && continue
    thr=$(awk "BEGIN{printf \"%.0f\", $STRONG_N/$t}")
    echo "$sync,$iv,$t,$thr" >> "$OUT/interval.csv"
    echo "  sync=$sync interval=$iv -> $t s ($thr cand/s)"
  done
done

echo "== time-to-find (crack $TARGET_ENC, block vs cyclic) =="
echo "partition,procs,time_s" > "$OUT/ttf.csv"
for part in block cyclic; do
  for p in $PROCS; do
    best=""
    for _ in $(seq "$REPEATS"); do
      t=$("${LAUNCH[@]}" -np "$p" ./mainMPI --sync p2p --partition "$part" \
            "$TARGET_ENC" "$TARGET_SHA" | time_of)
      [ -z "$t" ] && continue
      if [ -z "$best" ] || awk "BEGIN{exit !($t<$best)}"; then best="$t"; fi
    done
    [ -z "$best" ] && continue
    echo "$part,$p,$best" >> "$OUT/ttf.csv"
    echo "  $part P=$p -> $best s"
  done
done

echo "Done. CSVs written to $OUT/"
