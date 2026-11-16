# J274 GPU 전원·성능 값(Hwdata) 헤더

- `generate_agx_hwdata_profile_j274.py`: paulsmir `tools/generate_agx_hwdata_profile.py`(MIT, `LICENSE.windows-on-m1`) 복사본. 바꾼 곳 4곳: 보드 0x26→0x22, 프로필 태그 J313→J274, 오류 문구, upstream 트리 경로.
- `apple_agx_hwdata_profile_j274.generated.h`: 2026-11-16 맥미니 13.5 부팅 실측 ADT(`nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin`, sha256 32e63706…)로 생성 [사실].
- J313 판(`external/windows-on-m1/drivers/apple-agx/shared/include/apple_agx_hwdata_profile.generated.h`)과 바이트 줄 69개 다름: 성능 단계 전압 표, 전원 한도 등 [사실]. 이 값으로 GPU가 바르게 도는지는 실기기에서만 확인됨 [가설].

다시 만들기:

    PYTHONPATH=/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1/m1n1_windows/proxyclient \
      /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows/.venv-hv/bin/python3 -B generate_agx_hwdata_profile_j274.py \
      --adt <adt.bin> --output apple_agx_hwdata_profile_j274.generated.h
