#!/bin/bash
# Decode the K15 job log into a zip and unpack it.
#   bash gpucov/fetch-dxbc.sh <link>/job-N.log <out dir>
set -eu
LOG=$1; OUT=$2
mkdir -p "$OUT"
iconv -f CP949 -t UTF-8 "$LOG" | tr -d '\r' | awk '/K15 END-BASE64/{p=0} p{print} /K15 BEGIN-BASE64/{p=1}' | base64 -D > "$OUT/dxbc.zip"
WANT=$(iconv -f CP949 -t UTF-8 "$LOG" | tr -d '\r' | sed -n 's/.*K15 ZIP bytes=[0-9]* sha256=\([0-9a-f]*\).*/\1/p')
GOT=$(shasum -a 256 "$OUT/dxbc.zip" | cut -d' ' -f1)
[ "$WANT" = "$GOT" ] || { echo "SHA MISMATCH want=$WANT got=$GOT"; exit 1; }
(cd "$OUT" && unzip -o -q dxbc.zip && rm dxbc.zip)
echo "OK $(ls "$OUT" | wc -l) files in $OUT (sha256 $GOT)"
