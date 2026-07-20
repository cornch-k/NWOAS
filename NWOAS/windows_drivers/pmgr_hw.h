/* SPDX-License-Identifier: MIT
 *
 * pmgr_hw.h — Apple M1 (T8103) Power Manager (PMGR) register definitions
 *
 * 근거: /Volumes/X31/NWOAS/m1n1_windows/src/pmgr.h
 *       /Volumes/X31/NWOAS/m1n1_windows/src/pmgr.c
 *       /Volumes/X31/NWOAS/m1n1_windows/proxyclient/m1n1/hw/pmgr.py
 *
 * 상태 분류:
 *   [VERIFIED]  m1n1 소스에서 직접 추출한 값
 *   [INFERRED]  m1n1 소스 로직으로부터 추론한 값
 *   [DESIGN]    Windows 드라이버 설계를 위해 추가한 정의 (m1n1 비해당)
 *
 * WARNING: WDK 없어 컴파일 미검증. Windows 환경에서 별도 검증 필요.
 */

#pragma once

#include <wdm.h>

/* ======================================================================
 * 1. MMIO 기반 주소
 * ====================================================================== */

/* [VERIFIED] T8103 PMGR 기본 MMIO 주소
 * 출처: 시스템 메모리맵 + trace_pmgr.py:88 (eCPU 상태 레지스터 0x23b738004)
 * ADT /arm-io/pmgr 노드의 reg[0] 베이스.
 * 실제 하드웨어에서는 ADT(Apple Device Tree)의 reg 속성으로 결정됨.
 */
#define PMGR_T8103_BASE_REG0    0x23B700000ULL   /* 크기 약 0x100000 */
/* reg[1], reg[2]는 ADT에서만 읽을 수 있음 — 정적 정의 불가 [INFERRED] */

/* [VERIFIED] 다이 간 오프셋 (멀티다이 T6001/T6002용; T8103은 단일 다이)
 * 출처: pmgr.h:8
 */
#define PMGR_DIE_OFFSET         0x2000000000ULL

/* ======================================================================
 * 2. Power State (PS) 제어 레지스터 비트필드 (R_PSTATE, 32-bit)
 * PS 레지스터마다 8-byte 정렬 슬롯에 배치; 하위 4바이트가 실제 레지스터.
 * ====================================================================== */

/* [VERIFIED] 출처: pmgr.c:9-17 */
#define PMGR_PSTATE_RESET           (1u << 31)   /* 디바이스 소프트 리셋 요청 */
#define PMGR_PSTATE_AUTO_ENABLE     (1u << 28)   /* 자동(하드웨어) 전원 관리 활성화 */
#define PMGR_PSTATE_AUTO_STATE_MASK (0xFu << 24) /* 자동 전환 목표 상태 [27:24] */
#define PMGR_PSTATE_AUTO_STATE_SHIFT 24
#define PMGR_PSTATE_PARENT_OFF      (1u << 11)   /* 부모 디바이스 꺼짐 중 표시 */
#define PMGR_PSTATE_DEV_DISABLE     (1u << 10)   /* 디바이스 비활성화 (리셋 전 설정) */
#define PMGR_PSTATE_WAS_CLKGATED    (1u <<  9)   /* 이전에 클럭게이트된 이력 */
#define PMGR_PSTATE_WAS_PWRGATED    (1u <<  8)   /* 이전에 전원게이트된 이력 */
#define PMGR_PSTATE_ACTUAL_MASK     (0xFu <<  4) /* 현재 실제 전원 상태 [7:4] (RO) */
#define PMGR_PSTATE_ACTUAL_SHIFT    4
#define PMGR_PSTATE_TARGET_MASK     (0xFu <<  0) /* 목표 전원 상태 [3:0] (RW) */
#define PMGR_PSTATE_TARGET_SHIFT    0

/* [VERIFIED] 전원 상태 코드 — 출처: pmgr.h:13-15 */
#define PMGR_PS_ACTIVE              0xFu  /* 완전 활성(클럭 공급됨) */
#define PMGR_PS_CLKGATE             0x4u  /* 클럭게이트(레지스터 보존, 클럭 차단) */
#define PMGR_PS_PWRGATE             0x0u  /* 전원게이트(완전 오프) */

/* [VERIFIED] 폴링 타임아웃 (마이크로초) — 출처: pmgr.c:19 */
#define PMGR_POLL_TIMEOUT_US        10000

/* ======================================================================
 * 3. 디바이스 ID 인코딩 (clock-gates ADT 속성의 u32 값)
 * ====================================================================== */

/* [VERIFIED] 출처: pmgr.h:10-11 */
#define PMGR_DEVICE_ID_MASK         0x0000FFFFu  /* 디바이스 로컬 ID [15:0] */
#define PMGR_DIE_ID_MASK            0xF0000000u  /* 다이 번호 [31:28] */
#define PMGR_DIE_ID_SHIFT           28

#define PMGR_GET_DEVICE_ID(x)       ((x) & PMGR_DEVICE_ID_MASK)
#define PMGR_GET_DIE_ID(x)          (((x) & PMGR_DIE_ID_MASK) >> PMGR_DIE_ID_SHIFT)

/* ======================================================================
 * 4. 디바이스 플래그 (pmgr_device.flags)
 * ====================================================================== */

/* [VERIFIED] 출처: pmgr.c:21 */
#define PMGR_FLAG_VIRTUAL           0x10u /* 가상 디바이스 — PS 레지스터 없음 */

/* ======================================================================
 * 5. PS 레지스터 그룹 오프셋 (PMGRRegs0, reg[0] 기준)
 * [VERIFIED] 출처: proxyclient/m1n1/hw/pmgr.py (Python RegMap 정의)
 * 각 엔트리: (시작 오프셋, 엔트리 수, 엔트리당 바이트 수)
 * 실제 디바이스 배치는 ADT ps-regs 속성으로 런타임 결정됨.
 * ====================================================================== */

/* reg[0] 내 PS 그룹 시작 오프셋 */
#define PMGR_REG0_PS3_OFFSET        0x0000u  /* 10 devices x 8 bytes */
#define PMGR_REG0_PS11_OFFSET       0x0100u  /* 32 devices x 8 bytes */
#define PMGR_REG0_PS4_OFFSET        0x0200u  /* 32 devices x 8 bytes */
#define PMGR_REG0_PS5_OFFSET        0x0300u  /* 32 devices x 8 bytes */
#define PMGR_REG0_PS12_OFFSET       0x0400u  /* 15 devices x 8 bytes */
#define PMGR_REG0_PS6_OFFSET        0x0C00u  /* 2 devices x 8 bytes */
#define PMGR_REG0_PG1_OFFSET        0x1C010u /* 16 power gate regs x 8 bytes */
#define PMGR_REG0_PS7_OFFSET        0x4000u  /* 13 devices x 8 bytes */
#define PMGR_REG0_CPUTVM0_OFFSET    0x48000u /* CPU voltage monitor */
#define PMGR_REG0_CPUTVM1_OFFSET    0x48C00u
#define PMGR_REG0_CPUTVM2_OFFSET    0x48800u
#define PMGR_REG0_CPUTVM3_OFFSET    0x48400u
#define PMGR_REG0_PS8_OFFSET        0x8000u  /* 5 devices x 8 bytes */
#define PMGR_REG0_PS9_OFFSET        0xC000u  /* 7 devices x 8 bytes */
#define PMGR_REG0_PS10_OFFSET       0x10000u /* 10 devices x 8 bytes */

/* 알려진 특수 레지스터 절대 주소 (T8103, reg[0] = 0x23B700000)
 * [VERIFIED] 출처: trace_pmgr.py:88-89 */
#define PMGR_T8103_ECPU_STATE_REG   0x23B738004ULL  /* eCPU(효율 코어) 상태 보고 */
#define PMGR_T8103_PCPU_STATE_REG   0x23B738008ULL  /* pCPU(성능 코어) 상태 보고 */

/* reg[0] 내 오프셋으로 환산 */
#define PMGR_ECPU_STATE_OFFSET      0x38004u
#define PMGR_PCPU_STATE_OFFSET      0x38008u

/* reg[1] 내 PS 그룹 오프셋 (베이스 주소는 ADT 의존) */
#define PMGR_REG1_PS0_OFFSET        0x0058u  /* 32 devices x 8 bytes */
#define PMGR_REG1_PG0_OFFSET        0x1C010u /* 32 power gate regs x 8 bytes */
#define PMGR_REG1_PS1_OFFSET        0x4000u  /* 32 devices x 8 bytes */
#define PMGR_REG1_PS2_OFFSET        0x8000u  /* 32 devices x 8 bytes */

/* reg[2] 내 클럭 설정 그룹 오프셋 (베이스 주소는 ADT 의존) */
#define PMGR_REG2_CLK_CFG0_OFFSET   0x40000u /* 86 clocks x 4 bytes */
#define PMGR_REG2_CLK_CFG1_OFFSET   0x40200u /* 8 clocks x 4 bytes */
#define PMGR_REG2_CLK_CFG2_OFFSET   0x40280u /* 2 clocks x 4 bytes */

/* ======================================================================
 * 6. 클럭 설정 레지스터 비트필드 (R_CLK_CFG, 32-bit)
 * [VERIFIED] 출처: proxyclient/m1n1/hw/pmgr.py (R_CLK_CFG 정의)
 * ====================================================================== */
#define PMGR_CLK_CFG_UNK31         (1u << 31)
#define PMGR_CLK_CFG_SRC_MASK      (0x7Fu << 24) /* 클럭 소스 선택 [30:24] */
#define PMGR_CLK_CFG_SRC_SHIFT     24
#define PMGR_CLK_CFG_UNK20         (1u << 20)
#define PMGR_CLK_CFG_UNK8          (1u <<  8)
#define PMGR_CLK_CFG_UNK0_MASK     (0xFFu <<  0) /* [7:0] */

/* ======================================================================
 * 7. Power Gate 레지스터 (R_PWRGATE, 32-bit)
 * [VERIFIED] 출처: proxyclient/m1n1/hw/pmgr.py (R_PWRGATE 정의)
 * ====================================================================== */
#define PMGR_PWRGATE_GATE          (1u << 31) /* 1=게이트 활성화(전력 차단) */

/* ======================================================================
 * 8. ISP 전용 PMGR 오프셋 (참고용)
 * [VERIFIED] 출처: src/isp.c:19-21
 * ISP 디바이스 ADT 노드의 reg[1] (ISP 자체 PMGR 슬레이브 레지스터 블록)에 적용
 * ====================================================================== */
#define ISP_PMGR_OFF_T8103         0x4018u
#define ISP_PMGR_OFF_T6000         0x0008u
#define ISP_PMGR_OFF_T6020         0x4008u

/* ======================================================================
 * 9. [DESIGN] Windows KMDF 드라이버용 헬퍼 매크로
 * ====================================================================== */

/* PS 레지스터에서 현재 상태 읽기 */
#define PMGR_READ_ACTUAL(val)   (((val) & PMGR_PSTATE_ACTUAL_MASK) >> PMGR_PSTATE_ACTUAL_SHIFT)
#define PMGR_READ_TARGET(val)   (((val) & PMGR_PSTATE_TARGET_MASK) >> PMGR_PSTATE_TARGET_SHIFT)

/* PS 레지스터 목표 상태 필드 쓰기 (RMW) */
#define PMGR_SET_TARGET(val, mode) \
    (((val) & ~PMGR_PSTATE_TARGET_MASK) | (((mode) << PMGR_PSTATE_TARGET_SHIFT) & PMGR_PSTATE_TARGET_MASK))

/* PS 레지스터 절대 주소 계산
 * base: psreg 그룹 시작 물리 주소
 * dev_addr_offset: pmgr_device.addr_offset 값
 * 출처: pmgr.c:121 (addr += device->addr_offset << 3)
 */
#define PMGR_DEVICE_PS_ADDR(base, dev_addr_offset) \
    ((base) + ((ULONG64)(dev_addr_offset) << 3))
