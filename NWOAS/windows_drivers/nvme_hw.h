/*
 * nvme_hw.h — Apple ANS2 NVMe 하드웨어 레지스터 정의
 *
 * 근거: m1n1_windows/src/nvme.c, sart.c, asc.c (all MIT)
 * 대상 SoC: Apple T8103 (M1), Mac mini (j274)
 *
 * 주의: WDK 없이는 컴파일 불가. 설계/참조용 스켈레톤.
 */

#pragma once

#include <ntddk.h>

/* =========================================================
 * 1. MMIO BASE — ADT reg[] 인덱스 기준
 *    nvme.c:305  adt_get_reg(adt, adt_path, "reg", 3, &nvme_base, NULL)
 *    asc.c:42    adt_get_reg(adt, asc_path, "reg", 0, &base, NULL)
 *    sart.c:115  adt_get_reg(adt, sart_path, "reg", 0, &base, NULL)
 *
 *    실제 물리 주소는 ADT / ACPI _CRS에서 결정됨.
 *    T8103 메모리맵 참고치 (과제 컨텍스트):
 *      ANS/NVMe 범위: 0x277000000 – 0x27BD00000
 * ========================================================= */

/* =========================================================
 * 2. NVMe 표준 레지스터 (NVMe Spec 1.x, CAP 기반)
 *    nvme.c:18-34
 * ========================================================= */

/* Controller Configuration (CC) — nvme.c:18 */
#define NVME_CC             0x14
#define NVME_CC_SHN         GENMASK32(15, 14)   /* Shutdown Notification */
#define NVME_CC_SHN_NONE    0
#define NVME_CC_SHN_NORMAL  1
#define NVME_CC_SHN_ABRUPT  2
#define NVME_CC_EN          BIT32(0)             /* Enable */

/* Controller Status (CSTS) — nvme.c:25 */
#define NVME_CSTS             0x1C
#define NVME_CSTS_SHST        GENMASK32(3, 2)   /* Shutdown Status */
#define NVME_CSTS_SHST_NORMAL 0
#define NVME_CSTS_SHST_BUSY   1
#define NVME_CSTS_SHST_DONE   2
#define NVME_CSTS_RDY         BIT32(0)           /* Ready */

/* Admin Queue Attributes (AQA) — nvme.c:32 */
#define NVME_AQA  0x24
/* bits[27:16] = ACQS (completion queue size - 1), bits[11:0] = ASQS */

/* Admin Submission Queue Base (ASQ, 64-bit) — nvme.c:33 */
#define NVME_ASQ  0x28   /* lo32 */
#define NVME_ASQ_HI (NVME_ASQ + 4)

/* Admin Completion Queue Base (ACQ, 64-bit) — nvme.c:34 */
#define NVME_ACQ  0x30   /* lo32 */
#define NVME_ACQ_HI (NVME_ACQ + 4)

/* =========================================================
 * 3. Apple ANS2 독자 확장 레지스터
 *    nvme.c:36-56
 * ========================================================= */

/* Admin CQ 도어벨 (표준 DB[0]과는 다른 오프셋) — nvme.c:36 */
#define NVME_DB_ACQ   0x1004

/* IO CQ 도어벨 — nvme.c:37 */
#define NVME_DB_IOCQ  0x100C

/* ANS2 펌웨어 부팅 완료 상태 레지스터 — nvme.c:39-40 */
#define NVME_BOOT_STATUS      0x1300
#define NVME_BOOT_STATUS_OK   0xDE71CE55u   /* "de71ce55" = "device ss"? 비공식 */

/* 최대 보류 커맨드 수 제어 — nvme.c:48 */
#define NVME_MAX_PEND_CMDS_CTRL  0x1210
/* bits[31:16] = IO queue 한계-1, bits[15:0] = Admin 한계-1
   nvme.c:353: write32(... , ((QUEUE_SIZE-1) << 16) | (QUEUE_SIZE-1)) */

/* Apple 미상 제어 레지스터 — nvme.c:45-46 */
#define NVME_UNKNOWN_CTRL                0x24008
#define NVME_UNKNOWN_CTRL_PRP_NULL_CHECK BIT32(11)
/* nvme.c:352: 초기화 시 이 비트를 반드시 clear해야 함 */

/* Linear SQ 모드 제어 — nvme.c:42-43 */
#define NVME_LINEAR_SQ_CTRL     0x24908
#define NVME_LINEAR_SQ_CTRL_EN  BIT32(0)
/* nvme.c:351: set 후 표준 DB 대신 LINEAR_ASQ/LINEAR_IOSQ 도어벨 사용 */

/* Linear Admin SQ 도어벨 — nvme.c:49 */
#define NVME_DB_LINEAR_ASQ  0x2490C
/* nvme.c:235: write32(nvme_base + NVME_DB_LINEAR_ASQ, tag) */

/* Linear IO SQ 도어벨 — nvme.c:50 */
#define NVME_DB_LINEAR_IOSQ  0x24910
/* nvme.c:237: write32(nvme_base + NVME_DB_LINEAR_IOSQ, tag) */

/* =========================================================
 * 4. NVMMU (Apple NVMe Memory Management Unit) 레지스터
 *    nvme.c:52-56
 *    NVMMU는 표준 IOMMU/DART와 별개의 Apple 독자 TCB 기반 DMA 제어장치.
 * ========================================================= */

/* TCB 슬롯 총 수 (0-based 최대값 기록) — nvme.c:52 */
#define NVMMU_NUM         0x28100

/* Admin SQ TCB 배열 기저 주소 (64-bit) — nvme.c:53 */
#define NVMMU_ASQ_BASE    0x28108
#define NVMMU_ASQ_BASE_HI (NVMMU_ASQ_BASE + 4)

/* IO SQ TCB 배열 기저 주소 (64-bit) — nvme.c:54 */
#define NVMMU_IOSQ_BASE   0x28110
#define NVMMU_IOSQ_BASE_HI (NVMMU_IOSQ_BASE + 4)

/* TCB 무효화 (tag 번호를 write) — nvme.c:55 */
#define NVMMU_TCB_INVAL   0x28118
/* nvme.c:259: write32(nvme_base + NVMMU_TCB_INVAL, cqe.tag) after CQE */

/* TCB 무효화 상태 (0=성공, 비0=실패) — nvme.c:56 */
#define NVMMU_TCB_STAT    0x29120

/* =========================================================
 * 5. ASC (Apple Secure Coprocessor) 메일박스 레지스터
 *    asc.c:8-24
 *    ANS2 펌웨어(RTKit)와의 IPC 채널.
 *    base = reg[0] from /arm-io/ans ADT node
 *    mailbox_base = base + 0x8000  (asc.c:53)
 * ========================================================= */

/* CPU 시작/정지 제어 (cpu_base = reg[0]) */
#define ASC_CPU_CONTROL        0x44   /* asc.c:8 */
#define ASC_CPU_CONTROL_START  0x10   /* asc.c:9 */

/* 메일박스 제어 플래그 — asc.c:11-12 */
#define ASC_MBOX_CONTROL_FULL   BIT32(16)
#define ASC_MBOX_CONTROL_EMPTY  BIT32(17)

/* AP→IOP (A2I) 채널 (mailbox_base = cpu_base + 0x8000) — asc.c:14-18 */
#define ASC_MBOX_A2I_CONTROL  0x110
#define ASC_MBOX_A2I_SEND0    0x800   /* msg0 low 64 bit */
#define ASC_MBOX_A2I_SEND1    0x808   /* msg1 upper 32 bit (endpoint ID 등) */
#define ASC_MBOX_A2I_RECV0    0x810
#define ASC_MBOX_A2I_RECV1    0x818

/* IOP→AP (I2A) 채널 — asc.c:20-24 */
#define ASC_MBOX_I2A_CONTROL  0x114
#define ASC_MBOX_I2A_SEND0    0x820
#define ASC_MBOX_I2A_SEND1    0x828
#define ASC_MBOX_I2A_RECV0    0x830
#define ASC_MBOX_I2A_RECV1    0x838

/* =========================================================
 * 6. RTKit 프로토콜 상수
 *    rtkit.c:22-73
 * ========================================================= */

/* 엔드포인트 번호 */
#define RTKIT_EP_MGMT      0
#define RTKIT_EP_CRASHLOG  1
#define RTKIT_EP_SYSLOG    2
#define RTKIT_EP_DEBUG     3
#define RTKIT_EP_IOREPORT  4
#define RTKIT_EP_OSLOG     8

/* 메시지 타입 필드 bits[59:52] */
#define RTKIT_MGMT_TYPE_SHIFT  52

/* Management 메시지 타입 */
#define RTKIT_MGMT_MSG_HELLO         1   /* 버전 협상 요청 */
#define RTKIT_MGMT_MSG_HELLO_ACK     2
#define RTKIT_MGMT_MSG_START_EP      5
#define RTKIT_MGMT_MSG_IOP_PWR_STATE     6
#define RTKIT_MGMT_MSG_IOP_PWR_STATE_ACK 7
#define RTKIT_MGMT_MSG_EPMAP         8   /* endpoint 비트맵 */
#define RTKIT_MGMT_MSG_AP_PWR_STATE  0xB

/* 버전 범위 — rtkit.c:70-71 */
#define RTKIT_MIN_VERSION  11
#define RTKIT_MAX_VERSION  12

/* 전원 상태 — rtkit.c:75-80 */
#define RTKIT_POWER_OFF       0x00
#define RTKIT_POWER_SLEEP     0x01
#define RTKIT_POWER_QUIESCED  0x10
#define RTKIT_POWER_ON        0x20
#define RTKIT_POWER_INIT      0x220

/* =========================================================
 * 7. SARTv2 레지스터 (T8103 M1 사용 버전)
 *    sart.c:23-31
 *    base = reg[0] from /arm-io/sart-ans ADT node
 *    최대 16 엔트리 (sart.c:17)
 * ========================================================= */

#define APPLE_SART_MAX_ENTRIES  16
#define APPLE_SART_FLAGS_ALLOW  0xFF   /* sart.c:21 — 정확한 비트 의미 미확인 */

/* SARTv2: CONFIG(idx) = 0x00 + 4*idx
   bits[31:24] = flags, bits[23:0] = size >> 12 — sart.c:23-27 */
#define SART2_CONFIG(idx)          (0x00 + 4*(idx))
#define SART2_CONFIG_FLAGS_SHIFT   24
#define SART2_CONFIG_FLAGS_MASK    (0xFFu << 24)
#define SART2_CONFIG_SIZE_SHIFT    0
#define SART2_CONFIG_SIZE_MASK     0x00FFFFFFu
#define SART2_CONFIG_ADDR_SHIFT    12   /* size field <<12 = actual bytes */

/* SARTv2: PADDR(idx) = 0x40 + 4*idx
   value = phys_addr >> 12 — sart.c:29-31 */
#define SART2_PADDR(idx)       (0x40 + 4*(idx))
#define SART2_PADDR_SHIFT      12

/* SARTv3 (M2 이후, 참고용) — sart.c:33-40 */
#define SART3_CONFIG(idx)  (0x00 + 4*(idx))
#define SART3_PADDR(idx)   (0x40 + 4*(idx))
#define SART3_SIZE(idx)    (0x80 + 4*(idx))
#define SART3_PADDR_SHIFT  12
#define SART3_SIZE_SHIFT   12

/* =========================================================
 * 8. 큐/타이밍 파라미터
 *    nvme.c:13-16
 * ========================================================= */

#define ANS2_QUEUE_SIZE          64    /* SQ/CQ 엔트리 수 */
#define ANS2_QUEUE_ALIGN         (16 * 1024)   /* 16K 정렬 필요 */
#define ANS2_TIMEOUT_US          1000000ULL    /* 일반 타임아웃 1초 */
#define ANS2_ENABLE_TIMEOUT_US   5000000ULL    /* CC.EN 대기 5초 */
#define ANS2_SHUTDOWN_TIMEOUT_US 5000000ULL    /* 셧다운 대기 5초 */

/* Admin 커맨드 opcode — nvme.c:58-62 */
#define NVME_ADMIN_CMD_DELETE_SQ  0x00
#define NVME_ADMIN_CMD_CREATE_SQ  0x01
#define NVME_ADMIN_CMD_DELETE_CQ  0x04
#define NVME_ADMIN_CMD_CREATE_CQ  0x05
#define NVME_QUEUE_CONTIGUOUS     BIT32(0)   /* cdw11 bit0 */

/* IO 커맨드 opcode — nvme.c:64-66 */
#define NVME_CMD_FLUSH  0x00
#define NVME_CMD_WRITE  0x01
#define NVME_CMD_READ   0x02

/* =========================================================
 * 9. 데이터 구조체 (Windows 드라이버용, pack 필수)
 *    nvme.c:68-106, static_assert 검증값 포함
 * ========================================================= */
#pragma pack(push, 1)

/*
 * Apple ANS2 NVMe 커맨드 (64 bytes)
 * nvme.c:68-85
 * 표준 NVMe와 차이: tag가 u8(표준은 u16의 CID 위치와 다름)
 */
typedef struct _ANS2_NVME_COMMAND {
    UINT8  opcode;
    UINT8  flags;
    UINT8  tag;      /* Apple: u8, 표준 NVMe CID와 위치/크기 다름 */
    UINT8  rsvd;
    UINT32 nsid;
    UINT32 cdw2;
    UINT32 cdw3;
    UINT64 metadata;
    UINT64 prp1;
    UINT64 prp2;
    UINT32 cdw10;
    UINT32 cdw11;
    UINT32 cdw12;
    UINT32 cdw13;
    UINT32 cdw14;
    UINT32 cdw15;
} ANS2_NVME_COMMAND;
C_ASSERT(sizeof(ANS2_NVME_COMMAND) == 64);

/*
 * Apple ANS2 NVMe 완료 엔트리 (16 bytes)
 * nvme.c:87-92
 * 표준 NVMe와 차이: rsvd 위치에 sq_head/sq_id 없음; tag가 별도 필드
 * Phase bit: status bit[0] (표준과 동일 위치이나 해석 주의)
 */
typedef struct _ANS2_NVME_COMPLETION {
    UINT64 result;
    UINT32 rsvd;   /* 표준 NVMe: sq_head + sq_id — ANS2에서는 미사용 */
    UINT16 tag;    /* Apple 독자: 대응 커맨드 tag */
    UINT16 status; /* bit[0] = phase bit; bits[15:1] = status code */
} ANS2_NVME_COMPLETION;
C_ASSERT(sizeof(ANS2_NVME_COMPLETION) == 16);

/*
 * Apple NVMMU TCB (Transaction Control Block) — 128 bytes
 * nvme.c:94-106
 * 커맨드당 1개; NVMMU가 이를 보고 DMA 범위를 검증함.
 * aes_iv / _aes_unk: 저장장치 암호화 관련, 상세 미확인(가설).
 */
typedef struct _APPLE_NVMMU_TCB {
    UINT8  opcode;
    UINT8  dma_flags;  /* 3 = read+write 모두 허용 (nvme.c:224) */
    UINT8  slot_id;
    UINT8  unk0;
    UINT32 len;        /* cdw12 값 (LBA count-1) */
    UINT64 unk1[2];
    UINT64 prp1;
    UINT64 prp2;
    UINT64 unk2[2];
    UINT8  aes_iv[8];
    UINT8  aes_unk[64];
} APPLE_NVMMU_TCB;
C_ASSERT(sizeof(APPLE_NVMMU_TCB) == 128);

#pragma pack(pop)

/* =========================================================
 * 비트 조작 헬퍼 (WDK 환경 호환)
 * ========================================================= */
#ifndef BIT32
#define BIT32(n)         (1UL << (n))
#endif
#ifndef GENMASK32
#define GENMASK32(h, l)  (((1UL << ((h)-(l)+1)) - 1) << (l))
#endif
