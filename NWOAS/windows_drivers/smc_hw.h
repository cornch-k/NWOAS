/* SPDX-License-Identifier: MIT */
/*
 * Apple Silicon SMC (System Management Controller) Hardware Definitions
 * Target: T8103 (M1) — 근거: m1n1 src/smc.c, src/asc.c, src/rtkit.c
 *
 * [검증됨] = m1n1 소스 file:line 직접 확인
 * [추론]   = Asahi Linux 커널/WDK 지식 기반 설계 추론
 * [미확인] = T8103 ADT 없이 확인 불가
 */

#ifndef SMC_HW_H
#define SMC_HW_H

#include <ntddk.h>

/*
 * ============================================================
 * 1. MMIO 베이스 주소 (T8103 / M1 Mac mini)
 * ============================================================
 * [검증됨] 태스크 프롬프트 + m1n1 asc_init() 근거
 * asc_init("/arm-io/smc")  →  ADT reg[0] = ASC CPU base
 *
 * T6000 예: 0x290400000 (smc ASC), 0x291E00000 (SRAM)
 *   — t600x-die0.dtsi:37-41
 */
#define T8103_SMC_ASC_BASE    0x23E000000ULL   /* ASC CPU 제어 레지스터 기준 */
#define T8103_SMC_SRAM_BASE   0x23E800000ULL   /* AON/SRAM — 공유 메모리 [미확인: 정확한 크기] */
#define T8103_SMC_MBOX_BASE   (T8103_SMC_ASC_BASE + 0x8000ULL) /* src/asc.c:53: base+0x8000 */

/*
 * ============================================================
 * 2. ASC CPU 제어 레지스터 (base + offset)
 * ============================================================
 * [검증됨] src/asc.c:8-9, 71-81
 */
#define ASC_CPU_CONTROL       0x44
#define ASC_CPU_CONTROL_START (1U << 4)        /* bit 4: IOP CPU 기동 */

/*
 * ============================================================
 * 3. ASC 메일박스 레지스터 (MBOX base = ASC base + 0x8000)
 * ============================================================
 * [검증됨] src/asc.c:11-24
 *
 * A2I = AP to IOP (AP가 전송, IOP가 수신)
 * I2A = IOP to AP (IOP가 전송, AP가 수신)
 */
#define ASC_MBOX_CONTROL_FULL   (1U << 16)    /* FIFO 가득 참 */
#define ASC_MBOX_CONTROL_EMPTY  (1U << 17)    /* FIFO 비어 있음 */

#define ASC_MBOX_A2I_CONTROL    0x110         /* AP→IOP 제어 상태 */
#define ASC_MBOX_A2I_SEND0      0x800         /* AP→IOP FIFO 데이터[63:0]  쓰기 */
#define ASC_MBOX_A2I_SEND1      0x808         /* AP→IOP FIFO 데이터[95:64] 쓰기 */
#define ASC_MBOX_A2I_RECV0      0x810         /* (미사용) */
#define ASC_MBOX_A2I_RECV1      0x818

#define ASC_MBOX_I2A_CONTROL    0x114         /* IOP→AP 제어 상태 */
#define ASC_MBOX_I2A_SEND0      0x820         /* (IOP 전용) */
#define ASC_MBOX_I2A_SEND1      0x828
#define ASC_MBOX_I2A_RECV0      0x830         /* IOP→AP FIFO 데이터[63:0]  읽기 */
#define ASC_MBOX_I2A_RECV1      0x838         /* IOP→AP FIFO 데이터[95:64] 읽기 */

/*
 * ASC 메시지 레이아웃: 96비트 (msg0: 64bit, msg1: 32bit)
 * [검증됨] src/asc.h:8-11, src/rtkit.c:186-193
 *   msg0 = 페이로드 (RTKit/SMC 메시지)
 *   msg1 = 엔드포인트 번호 (8비트 유효)
 */

/*
 * ============================================================
 * 4. RTKit 시스템 엔드포인트 번호
 * ============================================================
 * [검증됨] src/rtkit.c:22-27
 */
#define RTKIT_EP_MGMT       0x00   /* 관리(전원/버전/에드포인트맵) */
#define RTKIT_EP_CRASHLOG   0x01   /* 크래시 로그 버퍼 */
#define RTKIT_EP_SYSLOG     0x02   /* 시스템 로그 */
#define RTKIT_EP_DEBUG      0x03
#define RTKIT_EP_IOREPORT   0x04
#define RTKIT_EP_OSLOG      0x08

#define SMC_ENDPOINT        0x20   /* SMC 앱 엔드포인트 [검증됨] src/smc.c:31 */

/*
 * ============================================================
 * 5. RTKit 관리 메시지 타입 (msg0[59:52])
 * ============================================================
 * [검증됨] src/rtkit.c:29-68
 */
#define RTKIT_MGMT_TYPE_SHIFT   52
#define RTKIT_MGMT_TYPE_MASK    (0xFFULL << RTKIT_MGMT_TYPE_SHIFT)
#define RTKIT_MGMT_TYPE(m)      (((m) & RTKIT_MGMT_TYPE_MASK) >> RTKIT_MGMT_TYPE_SHIFT)
#define RTKIT_MGMT_TYPE_PREP(t) (((ULONG64)(t)) << RTKIT_MGMT_TYPE_SHIFT)

#define RTKIT_MSG_HELLO              1
#define RTKIT_MSG_HELLO_ACK          2
#define RTKIT_MSG_BUFFER_REQUEST     1    /* EP SYSLOG/CRASHLOG/IOREPORT 버퍼 요청 */
#define RTKIT_MSG_IOP_PWR_STATE      6
#define RTKIT_MSG_IOP_PWR_STATE_ACK  7
#define RTKIT_MSG_EPMAP              8
#define RTKIT_MSG_EPMAP_REPLY        8
#define RTKIT_MSG_AP_PWR_STATE       0x0B
#define RTKIT_MSG_AP_PWR_STATE_ACK   0x0B
#define RTKIT_MSG_START_EP           5

/* HELLO 버전 협상: msg0[15:0]=minver, msg0[31:16]=maxver */
#define RTKIT_HELLO_MINVER_SHIFT  0
#define RTKIT_HELLO_MAXVER_SHIFT  16
#define RTKIT_MIN_VERSION         11     /* [검증됨] src/rtkit.c:70 */
#define RTKIT_MAX_VERSION         12     /* [검증됨] src/rtkit.c:71 */

/* START_EP: msg0[39:32]=ep_idx, msg0[1]=flag */
#define RTKIT_START_EP_IDX_SHIFT  32
#define RTKIT_START_EP_FLAG       (1ULL << 1)

/* EPMAP: msg0[51]=done, msg0[34:32]=base, msg0[31:0]=bitmap */
#define RTKIT_EPMAP_DONE          (1ULL << 51)
#define RTKIT_EPMAP_BASE_SHIFT    32
#define RTKIT_EPMAP_BITMAP_MASK   0xFFFFFFFFULL
#define RTKIT_EPMAP_REPLY_DONE    (1ULL << 51)
#define RTKIT_EPMAP_REPLY_MORE    (1ULL << 0)

/* 전원 상태: msg0[15:0] */
#define RTKIT_PWR_STATE_SHIFT     0
#define RTKIT_PWR_STATE_MASK      0xFFFFULL
#define RTKIT_POWER_OFF           0x0000
#define RTKIT_POWER_SLEEP         0x0001
#define RTKIT_POWER_QUIESCED      0x0010
#define RTKIT_POWER_ON            0x0020
#define RTKIT_POWER_INIT          0x0220   /* [검증됨] src/rtkit.c:80 */

/* 버퍼 요청: SIZE=msg0[51:44] (4K 페이지수), IOVA=msg0[41:0] */
#define RTKIT_BUFREQ_SIZE_SHIFT   44
#define RTKIT_BUFREQ_SIZE_MASK    (0xFFULL << RTKIT_BUFREQ_SIZE_SHIFT)
#define RTKIT_BUFREQ_IOVA_MASK    0x3FFFFFFFFFFULL

/*
 * ============================================================
 * 6. SMC 메시지 필드 (64비트 메시지)
 * ============================================================
 * [검증됨] src/smc.c:10-27, proxyclient/m1n1/fw/smc.py:17-53
 *
 * TX 메시지(AP→SMC IOP):
 *   [7:0]   TYPE  : SMC 커맨드 코드
 *   [11:8]  UNK   : 0 (예약)
 *   [15:12] ID    : 트랜잭션 ID (0-15, 16개 동시)
 *   [23:16] SIZE  : 읽기/쓰기 데이터 크기(바이트)
 *   [31:24] WSIZE : SMC_RW_KEY 전용 — 쓰기 크기
 *   [63:32] KEY   : 4CC 키 (ASCII 4바이트, 빅엔디언 정수)
 *
 * RX 메시지(SMC IOP→AP):
 *   [7:0]   RESULT: 0=성공, 비제로=오류
 *   [11:8]  UNK   : 0
 *   [15:12] ID    : 요청 트랜잭션 ID와 일치
 *   [31:16] SIZE  : 응답 데이터 크기
 *   [63:32] VALUE : ≤4바이트면 인라인 데이터, >4바이트면 SRAM 참조
 */

/* 커맨드 타입 [7:0] */
#define SMC_MSG_TYPE_SHIFT      0
#define SMC_MSG_TYPE_MASK       0xFFULL
#define SMC_MSG_ID_SHIFT        12
#define SMC_MSG_ID_MASK         (0xFULL << SMC_MSG_ID_SHIFT)
#define SMC_MSG_SIZE_SHIFT      16
#define SMC_MSG_SIZE_MASK       (0xFFULL << SMC_MSG_SIZE_SHIFT)
#define SMC_MSG_WSIZE_SHIFT     24
#define SMC_MSG_WSIZE_MASK      (0xFFULL << SMC_MSG_WSIZE_SHIFT)
#define SMC_MSG_KEY_SHIFT       32
#define SMC_MSG_KEY_MASK        (0xFFFFFFFFULL << SMC_MSG_KEY_SHIFT)

/* 결과 필드 */
#define SMC_RESULT_RESULT_MASK  0xFFULL
#define SMC_RESULT_ID_MASK      (0xFULL << 12)
#define SMC_RESULT_ID_SHIFT     12
#define SMC_RESULT_SIZE_MASK    (0xFFFFULL << 16)
#define SMC_RESULT_SIZE_SHIFT   16
#define SMC_RESULT_VALUE_MASK   (0xFFFFFFFFULL << 32)
#define SMC_RESULT_VALUE_SHIFT  32

/* SMC 커맨드 코드 [검증됨] src/smc.c:10-16 */
#define SMC_CMD_READ_KEY        0x10
#define SMC_CMD_WRITE_KEY       0x11
#define SMC_CMD_GET_KEY_BY_IDX  0x12
#define SMC_CMD_GET_KEY_INFO    0x13
#define SMC_CMD_INITIALIZE      0x17
#define SMC_CMD_NOTIFICATION    0x18   /* 결과 코드(0x18)로도 사용됨 */
#define SMC_CMD_RW_KEY          0x20

#define SMC_NUM_IDS             16     /* 동시 트랜잭션 최대 수 [검증됨] src/smc.c:29 */

/*
 * ============================================================
 * 7. 키 정보 레이아웃 (SRAM에서 6바이트)
 * ============================================================
 * [검증됨] proxyclient/m1n1/fw/smc.py:136-139
 * struct { uint8_t length; char type[4]; uint8_t flags; }
 * flags bit7 = 읽기 가능
 */
#define SMC_KEY_INFO_SIZE       6
#define SMC_KEY_FLAG_READABLE   0x80

/*
 * ============================================================
 * 8. 주요 SMC 키 (4CC — 4바이트 ASCII, 빅엔디언 정수)
 * ============================================================
 * [추론] Asahi Linux/macOS 리버스 엔지니어링 문헌 기반.
 *        m1n1 실험 도구(smc_watcher.py)에서 패턴 확인.
 *        모든 키 이름/의미는 추론으로 표시.
 */
/* 키 개수 조회 */
#define SMC_KEY_COUNT           0x234B4559UL  /* "#KEY" 빅엔디언 */

/* 전원/배터리 [추론] */
#define SMC_KEY_SHUTDOWN_FLAG   0x53536864UL  /* "SShd" — 셧다운 플래그 */
#define SMC_KEY_BOOT_STAGE      0x42535467UL  /* "BSTg" — 부트 단계 */
#define SMC_KEY_BOOT_ERR_CNT    0x42454374UL  /* "BECt" — 부트 오류 카운터 [미확인] */
#define SMC_KEY_PANIC_COUNT     0x50434E74UL  /* "PCNt" — 패닉 카운터 [미확인] */
#define SMC_KEY_PM_SETTING      0x504D5374UL  /* "PMSt" — 전원 관리 설정 [미확인] */

/* GPIO 키 패턴: "gP{2hex}" — gP00..gPff [검증됨] proxyclient/experiments/smc.py:20-23 */
/* pin = GPIO 핀 번호(16비트), fourcc = 'gP' + hex16 */
/* 예: RFKILL pin 13 → "gP0d" = 0x6750306DUL */

/* NTAP — 알림 탭 활성화 [추론, smc_watcher.py:40] */
#define SMC_KEY_NTAP            0x4E544150UL  /* "NTAP" */

#endif /* SMC_HW_H */
