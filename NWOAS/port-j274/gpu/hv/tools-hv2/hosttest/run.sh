#!/bin/bash
# Offline host tests for gpu-hv2 (no hardware). usage: run.sh <adt.bin> <out-binary-path>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
HV="$HERE/../../../../../m1n1-gpu-hv2/src"
clang -std=gnu11 -O1 -Wall -Wno-unused-function -DHV_AGX_CONFIG_SNAPSHOT_HOST_TEST -DHV_GUEST_IPA_PA_HOST_TEST \
  -DHV_AGX_POWER_BROKER_HOST_TEST -DAGX_LOCAL_RESERVE_V2= -I"$HERE" -I"$HV" -I"$HV/agx_shared/include" \
  "$HERE/test_hv2.c" "$HERE/adt_host.c" "$HV/hv_agx_config_snapshot.c" "$HV/hv_guest_ipa_pa.c" \
  "$HV/hv_agx_power_broker.c" "$HV/hv_agx_power_platform.c" "$HV/hv_agx_local_reserve.c" \
  "$HV/hv_agx_scanout_broker.c" -o "$2"
"$2" "$1"
