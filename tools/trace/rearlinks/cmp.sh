#!/bin/sh
# Runs the ledge trace for a few link geometries: cmp.sh <vehicle> "LT UP" ...
cd "$(dirname "$0")/../../.."
v=$1; shift
for cfg in "$@"; do
  set -- $cfg
  echo "== LT $1 UP $2"
  CZ_LT=$1 CZ_UP=$2 ./build/trace/rearlinks/trace ledge $v 2>&1 | grep -v borrow | head -${LINES_OUT:-5}
done
