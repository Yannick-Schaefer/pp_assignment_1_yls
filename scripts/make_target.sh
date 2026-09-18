#!/usr/bin/env bash
# Create an encrypted brute-force target with a chosen password, using the
# provided `encrypt` tool (correct salt + IV). Produces <out>.enc and
# <out>.sha512 that the crackers can then attack.
#
#   make encrypt
#   scripts/make_target.sh <password> [output_basename] [plaintext]
#
# Example: scripts/make_target.sh wXy7 files/deep "some short secret text"
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"

PW="${1:?usage: make_target.sh <password> [out_basename] [plaintext]}"
OUT="${2:-files/target}"
TEXT="${3:-Hello World!}"

[ -x ./encrypt ] || { echo "build the tool first: make encrypt"; exit 1; }
printf '%s' "$TEXT" > "$OUT"
./encrypt "$OUT" "$PW"
echo "created $OUT.enc + $OUT.sha512 (password: \"$PW\", length ${#PW})"
