/* rtkit_hw.h - Apple RTKit / ASC Mailbox register definitions for Windows driver skeleton
 * 근거: m1n1 src/asc.c, src/rtkit.c, src/afk.c (file:line 표기 아래)
 * 상태: WDK 미설치, 컴파일 불가. 설계 추론 포함 - 사실/추론 구분 표기 필수.
 */

#pragma once
#include <ntddk.h>

/* ============================================================
 * ASC (Apple Secure Coprocessor) MMIO 레이아웃
 * 근거: asc.c:8-24, asc.c:53-54
 *
 * 각 코프로세서(ANS/NVMe, DCP, SMC, SIO)는 독립된 MMIO 기반 주소를 가진다.
 * ACPI _CRS 에서 두 개의 메모리 리소스가 보고돼야 한다:
 *   reg[0] = cpu_base  (CPU 제어 레지스터 영역)
 *   reg[1] = cpu_base + 0x8000  (메일박스 레지스터 영역)  [사실: asc.c:53]
 *
 * 실제 베이스 주소는 디바이스마다 다르며 ACPI/DTB에서 읽어야 한다.
 * (예시값이므로 아래 #define은 자리표시용 — 실제 값은 ACPI _CRS 파싱 결과 사용)
 * ============================================================ */

/* CPU 제어 레지스터 오프셋 (cpu_base 기준)  [사실: asc.c:8-9] */
#define ASC_CPU_CONTROL        0x44
#define ASC_CPU_CONTROL_START  (1u << 4)   /* bit 0x10: IOP CPU 기동/정지 */

/* 메일박스 레지스터 오프셋 (mbox_base = cpu_base + 0x8000 기준) [사실: asc.c:13-24] */

/* AP→IOP (A2I) 채널 */
#define ASC_MBOX_A2I_CONTROL   0x110   /* 상태 레지스터: FULL/EMPTY 비트 */
#define ASC_MBOX_A2I_SEND0     0x800   /* 전송: 64비트 페이로드 하위  */
#define ASC_MBOX_A2I_SEND1     0x808   /* 전송: 32비트 엔드포인트 번호 */
#define ASC_MBOX_A2I_RECV0     0x810   /* 수신: (사용 안 함, echo용)  */
#define ASC_MBOX_A2I_RECV1     0x818

/* IOP→AP (I2A) 채널 */
#define ASC_MBOX_I2A_CONTROL   0x114   /* 상태 레지스터: FULL/EMPTY 비트 */
#define ASC_MBOX_I2A_SEND0     0x820   /* (IOP 전용, AP는 읽지 않음) */
#define ASC_MBOX_I2A_SEND1     0x828
#define ASC_MBOX_I2A_RECV0     0x830   /* 수신: 64비트 페이로드 하위  */
#define ASC_MBOX_I2A_RECV1     0x838   /* 수신: 32비트 엔드포인트 번호 */

/* CONTROL 레지스터 상태 비트 [사실: asc.c:11-12] */
#define ASC_MBOX_CONTROL_FULL  (1u << 16)  /* 메일박스 가득 참 — 전송 금지 */
#define ASC_MBOX_CONTROL_EMPTY (1u << 17)  /* 메일박스 비어 있음 — 수신 불가 */

/* ============================================================
 * RTKit 관리 프로토콜 상수
 * 근거: rtkit.c:22-73
 * ============================================================ */

/* 시스템 엔드포인트 번호 (0x00~0x1F: RTKit 예약) */
#define RTKIT_EP_MGMT      0    /* 관리/전원/엔드포인트 맵 [사실: rtkit.c:22] */
#define RTKIT_EP_CRASHLOG  1    /* IOP 크래시 로그 버퍼   [사실: rtkit.c:23] */
#define RTKIT_EP_SYSLOG    2    /* IOP 시스로그           [사실: rtkit.c:24] */
#define RTKIT_EP_DEBUG     3    /* 디버그 엔드포인트      [사실: rtkit.c:25] */
#define RTKIT_EP_IOREPORT  4    /* IO 리포트              [사실: rtkit.c:26] */
#define RTKIT_EP_OSLOG     8    /* OS 로그                [사실: rtkit.c:27] */
/* 0x20 이상: 애플리케이션 엔드포인트 (ANS, DCP 등)     [사실: rtkit.c:390] */

/* 메시지 타입 필드: msg0[59:52] [사실: rtkit.c:29] */
#define RTKIT_MGMT_TYPE_SHIFT   52
#define RTKIT_MGMT_TYPE_MASK    (0xFFULL << 52)

/* 전원 상태 필드: msg0[15:0] [사실: rtkit.c:31] */
#define RTKIT_PWR_STATE_MASK    0xFFFFULL

/* 관리 메시지 타입 코드 [사실: rtkit.c:46-68] */
#define MGMT_MSG_HELLO            1
#define MGMT_MSG_HELLO_ACK        2
#define MGMT_MSG_IOP_PWR_STATE    6
#define MGMT_MSG_IOP_PWR_STATE_ACK 7
#define MGMT_MSG_EPMAP            8
#define MGMT_MSG_EPMAP_REPLY      8    /* 같은 값, 방향으로 구분 */
#define MGMT_MSG_AP_PWR_STATE     0xb
#define MGMT_MSG_AP_PWR_STATE_ACK 0xb
#define MGMT_MSG_START_EP         5

/* HELLO 버전 필드 [사실: rtkit.c:48-49] */
#define MGMT_HELLO_MINVER_SHIFT  0
#define MGMT_HELLO_MINVER_MASK   0xFFFFULL
#define MGMT_HELLO_MAXVER_SHIFT  16
#define MGMT_HELLO_MAXVER_MASK   (0xFFFFULL << 16)

/* 지원 버전 범위 [사실: rtkit.c:70-71] */
#define RTKIT_MIN_VERSION  11
#define RTKIT_MAX_VERSION  12

/* EPMAP 비트 [사실: rtkit.c:55-61] */
#define MGMT_EPMAP_DONE_BIT   (1ULL << 51)
#define MGMT_EPMAP_BASE_SHIFT 32
#define MGMT_EPMAP_BASE_MASK  (0x7ULL << 32)
#define MGMT_EPMAP_BITMAP_MASK 0xFFFFFFFFULL
#define MGMT_EPMAP_REPLY_DONE (1ULL << 51)
#define MGMT_EPMAP_REPLY_MORE (1ULL << 0)

/* START_EP 비트 [사실: rtkit.c:67-68] */
#define MGMT_START_EP_IDX_SHIFT 32
#define MGMT_START_EP_IDX_MASK  (0xFFULL << 32)
#define MGMT_START_EP_FLAG      (1ULL << 1)

/* IOP 전원 상태 열거값 [사실: rtkit.c:75-81] */
#define RTKIT_POWER_OFF       0x00
#define RTKIT_POWER_SLEEP     0x01
#define RTKIT_POWER_QUIESCED  0x10
#define RTKIT_POWER_ON        0x20
#define RTKIT_POWER_INIT      0x220   /* 부트 시 IOP에 전송하는 초기값 */

/* 버퍼 요청 메시지 필드 [사실: rtkit.c:33-35] */
#define MSG_BUFFER_REQUEST        1
#define MSG_BUFREQ_SIZE_SHIFT     44  /* 비트[51:44]: 크기(4K 페이지 단위) */
#define MSG_BUFREQ_SIZE_MASK      (0xFFULL << 44)
#define MSG_BUFREQ_IOVA_MASK      0x3FFFFFFFFFFULL  /* 비트[41:0]: IOVA */

/* IOVA 마스크 (35비트): DVA = IOVA | dva_base [사실: rtkit.c:73, rtkit.c:218] */
#define RTKIT_IOVA_MASK           0xFFFFFFFFFULL

/* ============================================================
 * AFK/EPIC 링버퍼 프로토콜 상수
 * 근거: afk.c:117-148
 * ============================================================ */

/* RBEP 메시지 타입 (msg[63:48]) [사실: afk.c:117-130, afk.c:135] */
#define RBEP_TYPE_SHIFT    48
#define RBEP_TYPE_MASK     (0xFFFFULL << 48)

#define RBEP_INIT          0x80   /* EP 초기화 요청 (AP→IOP) */
#define RBEP_INIT_ACK      0xa0   /* EP 초기화 확인 (IOP→AP) */
#define RBEP_GETBUF        0x89   /* 공유 버퍼 요청 (IOP→AP) */
#define RBEP_GETBUF_ACK    0xa1   /* 공유 버퍼 응답 (AP→IOP) */
#define RBEP_INIT_TX       0x8a   /* TX 링버퍼 위치 통보 (IOP→AP) */
#define RBEP_INIT_RX       0x8b   /* RX 링버퍼 위치 통보 (IOP→AP) */
#define RBEP_START         0xa3   /* 링버퍼 운용 시작 (AP→IOP) */
#define RBEP_START_ACK     0x86   /* 시작 확인 (IOP→AP) */
#define RBEP_SEND          0xa2   /* 데이터 전송 알림 (AP→IOP): WPTR 포함 */
#define RBEP_RECV          0x85   /* 데이터 수신 알림 (IOP→AP) */
#define RBEP_SHUTDOWN      0xc0   /* 종료 요청 (AP→IOP) */
#define RBEP_SHUTDOWN_ACK  0xc1   /* 종료 확인 (IOP→AP) */

/* GETBUF 메시지 필드 [사실: afk.c:137-139] */
#define GETBUF_SIZE_SHIFT  16   /* 비트[31:16]: 크기 / 64 (BLOCK_SHIFT=6) */
#define GETBUF_SIZE_MASK   (0xFFFFULL << 16)
#define GETBUF_TAG_MASK    0xFFFFULL
#define GETBUF_ACK_DVA_MASK 0xFFFFFFFFFFFFULL   /* 비트[47:0]: DVA */

/* INIT_TX / INIT_RX 메시지 필드 [사실: afk.c:141-143] */
#define INITRB_OFFSET_SHIFT 32   /* 비트[47:32]: 버퍼 내 오프셋 / 64 */
#define INITRB_OFFSET_MASK  (0xFFFFULL << 32)
#define INITRB_SIZE_SHIFT   16   /* 비트[31:16]: 크기 / 64 */
#define INITRB_SIZE_MASK    (0xFFFFULL << 16)
#define INITRB_TAG_MASK     0xFFFFULL

/* SEND 메시지: WPTR [사실: afk.c:145] */
#define RBEP_SEND_WPTR_MASK 0xFFFFFFFFULL

/* 블록 정렬: 모든 링버퍼 오프셋은 64바이트 단위 [사실: afk.c:132] */
#define AFK_BLOCK_SHIFT    6
#define AFK_BLOCK_SIZE     (1u << AFK_BLOCK_SHIFT)

/* 큐 엔트리 매직 [사실: afk.c:133]  ' POI' = 0x20494F50 */
#define AFK_QE_MAGIC       0x20494F50u

/* 최대 채널 수 per 엔드포인트 [사실: afk.c:91] */
#define AFK_MAX_CHANNEL    8

/* ============================================================
 * EPIC 서브프로토콜 타입/카테고리
 * 근거: afk.c:28-40, afk.h:10-11
 * ============================================================ */
#define EPIC_TYPE_NOTIFY      0
#define EPIC_TYPE_COMMAND     3
#define EPIC_TYPE_REPLY       4
#define EPIC_TYPE_NOTIFY_ACK  8

#define EPIC_CAT_REPORT   0x00
#define EPIC_CAT_NOTIFY   0x10
#define EPIC_CAT_REPLY    0x20
#define EPIC_CAT_COMMAND  0x30

#define EPIC_SUBTYPE_ANNOUNCE     0x30   /* 서비스 선언 [사실: afk.h:11] */
#define EPIC_SUBTYPE_STD_SERVICE  0xc0   /* 표준 서비스 호출 [사실: afk.h:12] */
