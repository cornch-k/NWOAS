#!/bin/bash
# Decode a base64 export job log (K15 dxbc, K24 Edge dumps) into a zip and unpack it.
#   bash gpucov/fetch-b64.sh <K15|K24> <link>/job-N.log <out dir>
set -eu
TAG=$1; LOG=$2; OUT=$3
mkdir -p "$OUT"
iconv -f CP949 -t UTF-8 "$LOG" | tr -d '\r' | awk -v t="$TAG" '$0 ~ t" END-BASE64"{p=0} p{print} $0 ~ t" BEGIN-BASE64"{p=1}' | base64 -D > "$OUT/export.zip"
WANT=$(iconv -f CP949 -t UTF-8 "$LOG" | tr -d '\r' | sed -n "s/.*$TAG ZIP bytes=[0-9]* sha256=\([0-9a-f]*\).*/\1/p")
GOT=$(shasum -a 256 "$OUT/export.zip" | cut -d' ' -f1)
[ "$WANT" = "$GOT" ] || { echo "SHA MISMATCH want=$WANT got=$GOT"; exit 1; }
(cd "$OUT" && unzip -o -q export.zip && rm export.zip)
echo "OK $(ls "$OUT" | wc -l) files in $OUT (sha256 $GOT)"
