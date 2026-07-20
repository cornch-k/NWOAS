/* SPDX-License-Identifier: MIT */
/*
 * Apple PCIe (apcie) hardware register definitions for T8103 (M1)
 *
 * 출처: /Volumes/X31/NWOAS/m1n1_windows/src/pcie.c (Asahi m1n1)
 * 모든 offset/bit 정의는 m1n1 소스 file:line 근거를 주석으로 표기.
 * 이 헤더는 WDK 없이 컴파일 불가 — 참조/설계용 스켈레톤.
 */

#pragma once
#include <ntdef.h>   /* ULONG, USHORT, UCHAR -- WDK 환경 필요 */

/* ============================================================
 * MMIO 기지 주소 (T8103 / M1 Mac mini)
 * 출처: pcie.c 10-34 ADT reg 주석 (address_hi=0x6, address_lo=...)
 * 실제 사용 시 ADT/ACPI _CRS 에서 런타임으로 읽어야 함.
 * ============================================================ */

/* 물리 주소 — ADT reg[0] */
#define APCIE_ECAM_BASE       0x690000000ULL   /* ECAM (config space), 0x10000000 */
/* ADT reg[1] */
#define APCIE_RC_BASE         0x680000000ULL   /* RC 제어 레지스터, 0x40000 */
/* ADT reg[2] */
#define APCIE_PHY_BASE        0x680080000ULL   /* PHY 공통, 0x90000 */
/* ADT reg[3] */
#define APCIE_PHYIP_BASE      0x6800C0000ULL   /* PHY IP, 0x20000 */
/* ADT reg[4] */
#define APCIE_AXI_BASE        0x68C000000ULL   /* AXI-to-AF 브릿지, 0x4000 */
/* ADT reg[5] */
#define APCIE_FUSE_BASE       0x3D2BC000ULL    /* 퓨즈 영역, 0x1000 */
/* ADT reg[6,10,14] — 포트 0/1/2 config */
#define APCIE_PORT0_CFG_BASE  0x681000000ULL   /* 0x8000 */
#define APCIE_PORT1_CFG_BASE  0x682000000ULL
#define APCIE_PORT2_CFG_BASE  0x683000000ULL
/* ADT reg[7,11,15] — 포트 0/1/2 LTSSM 디버그 */
#define APCIE_PORT0_LTSSM     0x681010000ULL   /* 0x1000 */
#define APCIE_PORT1_LTSSM     0x682010000ULL
#define APCIE_PORT2_LTSSM     0x683010000ULL
/* ADT reg[8,12,16] — 포트별 PHY */
#define APCIE_PORT0_PHY       0x680084000ULL   /* 0x4000 */
#define APCIE_PORT1_PHY       0x680088000ULL
#define APCIE_PORT2_PHY       0x68008C000ULL

/* PHY stride (phy N = phy_base + N * PHY_STRIDE) */
#define APCIE_PHY_STRIDE      0x4000           /* pcie.c:102 */
#define APCIE_PHYIP_STRIDE    0x40000          /* pcie.c:103 */

/* ============================================================
 * PHY 레지스터 (base = APCIE_PHY_BASE or port_phy_base)
 * 출처: pcie.c:38-51
 * ============================================================ */

#define APCIE_PHY_CTRL            0x000        /* pcie.c:38 */
#define   APCIE_PHY_CTRL_CLK0REQ  BIT(0)       /* 클럭 0 요청 */
#define   APCIE_PHY_CTRL_CLK1REQ  BIT(1)       /* 클럭 1 요청 */
#define   APCIE_PHY_CTRL_CLK0ACK  BIT(2)       /* 클럭 0 승인 (RO, poll) */
#define   APCIE_PHY_CTRL_CLK1ACK  BIT(3)       /* 클럭 1 승인 (RO, poll) */
#define   APCIE_PHY_CTRL_RESET    BIT(7)       /* PHY 리셋 (1=assert) */

/* T81XX 전용 PHY interface 제어 (base = RC_BASE) */
#define APCIE_PHYIF_CTRL          0x024        /* pcie.c:45 */
#define   APCIE_PHYIF_CTRL_RUN    BIT(0)       /* pcie.c:46 */

/* ============================================================
 * PHY Common 레지스터 (T602X 전용, base = phy_common_base)
 * 출처: pcie.c:49-51 (추정 포함 — pcie.c가 "Guesswork" 주석)
 * ============================================================ */

#define APCIE_PHYCMN_CLK          0x000        /* pcie.c:49 */
#define   APCIE_PHYCMN_CLK_MODE   GENMASK(1,0) /* [1:0] 추정: 0=off, 1=on */
#define   APCIE_PHYCMN_CLK_100MHZ BIT(31)      /* 100 MHz 기준클럭 준비 (RO) */

/* ============================================================
 * 포트 Config 레지스터 (base = port_base[port])
 * 출처: pcie.c:56-71
 * ============================================================ */

#define APCIE_PORT_LINKSTS        0x208        /* pcie.c:56 */
#define   APCIE_PORT_LINKSTS_UP   BIT(0)       /* 링크 up */
#define   APCIE_PORT_LINKSTS_BUSY BIT(2)       /* 링크 busy (poll = 0) */
#define   APCIE_PORT_LINKSTS_L2   BIT(6)       /* L2 상태 */

#define APCIE_PORT_APPCLK         0x800        /* pcie.c:61 */
#define   APCIE_PORT_APPCLK_EN    BIT(0)       /* app 클럭 인에이블 */

#define APCIE_PORT_STATUS         0x804        /* pcie.c:64 */
#define   APCIE_PORT_STATUS_RUN   BIT(0)       /* 포트 동작 중 (poll) */

#define APCIE_PORT_RESET          0x814        /* pcie.c:67 (T81XX) */
#define   APCIE_PORT_RESET_DIS    BIT(0)       /* 리셋 비활성(=리셋 해제) */

/* T602X 전용 포트 레지스터 */
#define APCIE_T602X_PORT_RESET    0x82C        /* pcie.c:70 */
#define APCIE_T602X_PORT_MSIMAP   0x3800       /* pcie.c:71, 512 entries × 4B */

/* ============================================================
 * DesignWare PCIe Core (DBI) 레지스터 (base = ECAM / config_base)
 * 출처: pcie.c:85-100
 * ============================================================ */

#define DWC_DBI_RO_WR               0x8BC      /* pcie.c:85 */
#define   DWC_DBI_RO_WR_EN          BIT(0)     /* RO 레지스터 쓰기 허용 */

#define DWC_DBI_PORT_LINK_CONTROL   0x710      /* pcie.c:88 */
#define   DWC_DBI_PORT_LINK_DLL_EN  BIT(5)
#define   DWC_DBI_PORT_LINK_FAST    BIT(7)
#define   DWC_DBI_PORT_LINK_MODE    GENMASK(21,16) /* [21:16] */
#define     APCIE_LINK_MODE_1L      0x01
#define     APCIE_LINK_MODE_2L      0x03
#define     APCIE_LINK_MODE_4L      0x07
#define     APCIE_LINK_MODE_8L      0x0F
#define     APCIE_LINK_MODE_16L     0x1F

#define DWC_DBI_LINK_WIDTH_SPEED    0x80C      /* pcie.c:98 */
#define   DWC_DBI_LINK_WIDTH        GENMASK(12,8)
#define   DWC_DBI_SPEED_CHANGE      BIT(17)

/* ============================================================
 * PCIe Express Capability (base = config_base + PCIE_CAP_BASE)
 * 출처: pcie.c:74-81
 * ============================================================ */

#define PCIE_CAP_BASE               0x70       /* pcie.c:74 */

#define PCIE_LNKCAP                 0x0C       /* pcie.c:75 */
#define   PCIE_LNKCAP_SLS           GENMASK(3,0)
#define   PCIE_LNKCAP_MLW           GENMASK(9,4)

#define PCIE_LNKCAP2                0x2C       /* pcie.c:78 */
#define   PCIE_LNKCAP2_SLS          GENMASK(6,1)

#define PCIE_LNKCTL2                0x30       /* pcie.c:80 */
#define   PCIE_LNKCTL2_TLS          GENMASK(3,0)

/* ============================================================
 * T8103 퓨즈 비트 테이블
 * 출처: pcie.c:113-118
 * 형식: {src_reg, tgt_reg(PHY IP), src_bit, tgt_bit, width}
 * ============================================================ */
/*
 * T8103 fuse_bits[] (pcie.c:113-118):
 * {0x0084, 0x6238,  4,  0, 6}
 * {0x0084, 0x6220, 10, 14, 3}
 * {0x0084, 0x62a4, 13, 17, 2}
 * {0x0418, 0x522c, 27,  9, 2}
 * {0x0418, 0x522c, 13, 12, 3}
 * {0x0418, 0x5220, 18, 14, 3}
 * {0x0418, 0x52a4, 21, 17, 2}
 * {0x0418, 0x522c, 23, 16, 5}
 * {0x0418, 0x5278, 23, 20, 3}
 * {0x0418, 0x5018, 31,  2, 1}
 * {0x041c, 0x1204,  0,  2, 5}
 */

/* ============================================================
 * 도우미 매크로 (WDK READ_REGISTER_ULONG 등으로 치환 필요)
 * ============================================================ */
#ifndef BIT
#define BIT(n)          (1UL << (n))
#endif
#ifndef GENMASK
#define GENMASK(h,l)    (((1UL << ((h)-(l)+1)) - 1) << (l))
#endif
#ifndef FIELD_PREP
#define FIELD_PREP(mask, val) (((ULONG)(val) << __builtin_ctz(mask)) & (mask))
#endif
#ifndef FIELD_GET
#define FIELD_GET(mask, val)  (((ULONG)(val) & (mask)) >> __builtin_ctz(mask))
#endif
