# S230 extracted-process test review

Reviewer: Claude Code CLI, claude-fable-5-1. Read-only review of the initial 11-fixture harness. See claude-resolution.md for corrections made afterward.

검토를 마쳤습니다. 결과를 정리해 드립니다, 콘치님.

## 결론

11개 픽스처 중 단정 자체가 틀린 것은 없습니다. 통과 판정은 유효합니다. 다만 아래 네 가지는 커버리지 표기가 실제보다 넓게 읽히며, 그중 두 가지는 하네스가 프로덕션과 다른 값을 정의하고 있습니다.

## 반드시 고칠 것

- **REFUSED 상수 불일치.** 하네스 스텁은 `NVME_GUEST_REFUSED`를 `UINT64_MAX`로 정의하지만 프로덕션 `nvme.h:22`는 `2`입니다. 픽스처 5는 하네스 매크로끼리 맞아 통과할 뿐이라, 실제 `nvme_rw_guest`가 2를 돌려줄 때의 경로를 검증한 것이 아닙니다. 스텁 세 줄을 지우고 프로덕션 `nvme.h`를 include하도록 바꾸세요.
- **NS2 "cancel" 커버리지가 가짜입니다.** `nwoas_nvme_link_execute` 전체를 목으로 대체했기 때문에, 프로덕션의 취소 판정 논리(epoch·generation·sq_head 비교, magic/response 검증, READ_ERROR/WRITE_ERROR 매핑, `link_busy` set/clear)는 한 줄도 실행되지 않습니다. 픽스처 9는 "목이 -2를 돌려주면 process가 deferred를 세운다"만 확인합니다. `hv_exc_proxy`를 목으로 바꾸고 실제 `link_execute`를 포함하면 같은 비용으로 진짜 커버리지가 됩니다. 링크 중 CC.EN=0 쓰기로 epoch를 올려 취소를 유도하는 픽스처 하나면 충분합니다.
- **"deferred process"는 5 kHz 틱 경로를 안 탑니다.** 추출 구간이 `/* Called from the 5 kHz` 앞에서 끝나 `nwoas_nvme_fastpath_poll`이 하네스에 없습니다. 픽스처 11은 `process`를 직접 호출하므로 poll의 `deferred && cq_pending < cq_size-1` 게이트와 `s229_poll()` 선행 순서가 검증되지 않습니다. 추출 끝을 `#include "nwoas_s229_adapter.inc"`로 옮기고 `poll`을 호출하세요.
- **"mapping fault"는 목의 동작입니다.** 프로덕션 `nwoas_nvme_guest_ptr`의 16 KiB 페이지 경계 검사(`hv_vm.c:1380`)는 test_adapter.c의 목으로 교체되어 있습니다. 픽스처 10이 검증하는 것은 NULL을 받았을 때 process의 반응뿐입니다. PASS 문구와 result.json의 scope에 이 점을 명시하세요.

## 픽스처별 보강(값싼 것 위주)

- **1 (read OK):** `io_status`가 phase 비트를 버립니다. `(status & 1) == 1` 단정을 추가해 초기 phase(flags&1)가 CQE에 실리는지 확인하세요.
- **4 (FUA):** `cache_enabled=false` 경로가 없습니다. Set Features(op 9, fid 6)로 캐시를 끄고 FUA 없는 쓰기가 flush를 부르는지 확인하세요. 또 물리 쓰기 성공 후 flush 실패(FATAL) 케이스가 빠져 있습니다. 데이터는 커밋됐는데 컨트롤러가 fatal로 가는 실제 라이프사이클 위험 지점입니다.
- **6 (FAILED):** `sq_head == 0`, `errors == 1`, CQE 슬롯이 여전히 0인지 추가하세요. 코드 주석의 "retryable completion 노출 금지"를 직접 검증하는 단정이 됩니다. CC.EN=0 후 CFS 유지 단정은 `s229_reset_io`가 faulted에서 거부하는 경로를 정확히 타므로 유효합니다.
- **11 (backpressure):** `cq_pending=15`를 강제로 넣은 상태는 CQ에 실제 엔트리가 없어 ack가 존재하지 않는 항목을 소비하는 모순 상태입니다. 16-deep CQ에 명령 15개를 실제로 넣어 wrap과 phase 토글까지 같이 검증하면 이 픽스처를 대체할 수 있습니다.

## 빠진 시나리오

- 한 도어벨에 명령 2개 제출 시 트랩에서 1개만 실행되고 나머지가 poll로 넘어가는 예산 계약(1 command per tick).
- execute 검증 경계: `lba = LBAS-1, count=2`, 쓰기 상한 `WRITE_LAST_LBA+1`, cdw12 예약 비트, flags/metadata 비영, nsid 3, opcode 3, nsid 0xffffffff flush.
- SQ wrap.

## 테스트 범위 요약

실제 실행되는 프로덕션 코드는 `execute`, `process`, `update_irq`, control, mmio, adapter, frontend 스택입니다. 목은 `nvme_rw_guest`, `nvme_flush`, NS2 link 전체, 클럭, `guest_ptr`/`ipa_to_pa`입니다. IRQ 경로는 process → `s229_fast_irq_update` → frontend `io_pending` → control `irq_update` → `s229_irq`까지 실제로 연결되어 있어 픽스처 1의 `fast_level` 단정은 유효합니다.
