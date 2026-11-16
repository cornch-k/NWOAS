#!/bin/bash
# Offline host tests for gpu-hv2 (no hardware). usage: [HWDATA=j313] run.sh <adt.bin> <out-binary-path>
# Default HWDATA=j274 (the profile AGX builds use).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
HV="$HERE/../../../../../m1n1-gpu-hv2/src"
# S251: the J274 KMD's own scanout client drives the HV broker + service (test_scanout_svc.c).
KMD="$HERE/../../../driver-p15/drivers/apple-agx/shared"
HWFLAG=-DNWOAS_AGX_HWDATA_J274=1; [ "${HWDATA:-j274}" = j313 ] && HWFLAG=
clang -std=gnu11 $HWFLAG -O1 -Wall -Wno-unused-function -DHV_AGX_CONFIG_SNAPSHOT_HOST_TEST -DHV_GUEST_IPA_PA_HOST_TEST \
  -DHV_AGX_POWER_BROKER_HOST_TEST -DAGX_LOCAL_RESERVE_V2= -DNWOAS_AGX_SCANOUT_J274=1 -DAPPLE_AGX_BOARD_J274=1 \
  -I"$HERE" -I"$HV" -I"$HV/agx_shared/include" -I"$KMD/include" \
  "$HERE/test_hv2.c" "$HERE/adt_host.c" "$HV/hv_agx_config_snapshot.c" "$HV/hv_guest_ipa_pa.c" \
  "$HV/hv_agx_power_broker.c" "$HV/hv_agx_power_platform.c" "$HV/hv_agx_local_reserve.c" \
  "$HV/hv_agx_scanout_broker.c" "$HV/hv_agx_scanout_service.c" "$HERE/test_scanout_svc.c" \
  "$KMD/src/apple_agx_scanout.c" -o "$2"
"$2" "$1"
