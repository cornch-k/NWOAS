/*
 * aic_hw.h -- Apple Interrupt Controller (AIC v1/v2/v3) register definitions
 *             for the NWOAS Windows HAL Extension skeleton.
 *
 * Source: m1n1_windows/src/aic_regs.h  (all offsets verified line-by-line)
 *         m1n1_windows/src/aic.h        (struct layout, constants)
 *         m1n1_windows/src/aic.c        (initialization sequence)
 *         m1n1_windows/src/hv_vgic.h    (vGIC CPU-interface constants)
 *
 * Fact/Design legend used throughout this file:
 *   [FACT]   - directly confirmed in m1n1 source at the cited file:line.
 *   [DESIGN] - Windows driver design inferred from hardware facts; not verified on HW.
 *   [UNVERIFIED] - plausible but not yet confirmed by a hardware run.
 *
 * Copyright notice: register names/values are from the public m1n1 project
 * (MIT License, (c) Asahi Linux contributors).  Driver skeleton is NWOAS work.
 */

#pragma once
#include <ntddk.h>

/*---------------------------------------------------------------------------
 * AIC MMIO base (T8103 / M1 Mac mini)
 * [FACT] aic_regs.h:3, aic.c:170 -- base read from ADT /arm-io/aic "reg" property
 * The literal base address for M1 is from the system prompt MMIO map: 0x23B100000.
 *---------------------------------------------------------------------------*/
#define AIC_BASE_ADDR_M1    0x23B100000ULL   /* [FACT] system MMIO map */
#define AIC_REG_SIZE        0x8000           /* [FACT] aic_regs.h:3 */

/*---------------------------------------------------------------------------
 * AIC v1 register offsets  (M1 = T8103, uses AIC v1)
 * [FACT] aic_regs.h:4-20
 *---------------------------------------------------------------------------*/
#define AIC_INFO            0x0004   /* [FACT] aic_regs.h:4  -- bits[15:0] = nr_hw_irqs */
#define AIC_WHOAMI          0x2000   /* [FACT] aic_regs.h:5  -- read: current CPU index */
#define AIC_EVENT           0x2004   /* [FACT] aic_regs.h:6  -- read: pending event word */
#define AIC_IPI_SEND        0x2008   /* [FACT] aic_regs.h:7  -- write: IPI send bitmask */
#define AIC_IPI_ACK         0x200C   /* [FACT] aic_regs.h:8  -- write: IPI ack bitmask */
#define AIC_IPI_MASK_SET    0x2024   /* [FACT] aic_regs.h:9  -- write: mask IPI sources */
#define AIC_IPI_MASK_CLR    0x2028   /* [FACT] aic_regs.h:10 -- write: unmask IPI sources */
#define AIC_TARGET_CPU      0x3000   /* [FACT] aic_regs.h:11 -- per-IRQ CPU affinity array (4B each) */
#define AIC_SW_SET          0x4000   /* [FACT] aic_regs.h:12 -- software-trigger set bitmap */
#define AIC_SW_CLR          0x4080   /* [FACT] aic_regs.h:13 -- software-trigger clear bitmap */
#define AIC_MASK_SET        0x4100   /* [FACT] aic_regs.h:14 -- mask (disable) IRQ bitmap */
#define AIC_MASK_CLR        0x4180   /* [FACT] aic_regs.h:15 -- unmask (enable) IRQ bitmap */

/* Per-CPU IPI control (AIC v1): stride = 0x80 per cpu  [FACT] aic_regs.h:17-20 */
#define AIC_CPU_IPI_SET(cpu)      (0x5008 + ((cpu) << 7))
#define AIC_CPU_IPI_CLR(cpu)      (0x500C + ((cpu) << 7))
#define AIC_CPU_IPI_MASK_SET(cpu) (0x5024 + ((cpu) << 7))
#define AIC_CPU_IPI_MASK_CLR(cpu) (0x5028 + ((cpu) << 7))

/*---------------------------------------------------------------------------
 * AIC_EVENT word bitfields  [FACT] aic_regs.h:46-53
 *---------------------------------------------------------------------------*/
#define AIC_EVENT_DIE_SHIFT   24
#define AIC_EVENT_DIE_MASK    (0xFFU  << AIC_EVENT_DIE_SHIFT)
#define AIC_EVENT_TYPE_SHIFT  16
#define AIC_EVENT_TYPE_MASK   (0xFFU  << AIC_EVENT_TYPE_SHIFT)
#define AIC_EVENT_NUM_SHIFT   0
#define AIC_EVENT_NUM_MASK    (0xFFFFU)

#define AIC_EVENT_TYPE_HW     1   /* [FACT] aic_regs.h:50 -- hardware IRQ */
#define AIC_EVENT_TYPE_IPI    4   /* [FACT] aic_regs.h:51 -- IPI */
#define AIC_EVENT_IPI_OTHER   1   /* [FACT] aic_regs.h:52 -- IPI from another CPU */
#define AIC_EVENT_IPI_SELF    2   /* [FACT] aic_regs.h:53 -- self-IPI */

/*---------------------------------------------------------------------------
 * IPI bitmasks  [FACT] aic_regs.h:57-58
 *---------------------------------------------------------------------------*/
#define AIC_IPI_OTHER   (1U << 0)
#define AIC_IPI_SELF    (1U << 31)

/* IPI send: bit per target CPU  [FACT] aic_regs.h:55 */
#define AIC_IPI_SEND_CPU(cpu)   (1U << (cpu))

/*---------------------------------------------------------------------------
 * AIC_INFO bitfields  [FACT] aic_regs.h:41
 *---------------------------------------------------------------------------*/
#define AIC_INFO_NR_HW_MASK   0xFFFFU   /* bits[15:0] = number of hardware IRQs */

/*---------------------------------------------------------------------------
 * AIC v1 limits  [FACT] aic_regs.h:43-44, aic.h:8
 *---------------------------------------------------------------------------*/
#define AIC1_MAX_IRQ    0x400          /* [FACT] aic_regs.h:43  = 1024 */
#define AIC_MAX_HW_NUM  (0x80 * 32)   /* [FACT] aic_regs.h:44  = 4096 (M1 Max upper bound) */
#define AIC_MAX_DIES    4              /* [FACT] aic.h:8 */

/* Mask register helpers: IRQ 'n' sits in 32-bit word at offset (n>>5)*4, bit (n&31)
 * [FACT] aic.c:9-10
 */
#define AIC_MASK_REG(n)  ((n) >> 5)   /* word index */
#define AIC_MASK_BIT(n)  (1U << ((n) & 0x1F))

/*---------------------------------------------------------------------------
 * AIC v2 / v3 offsets (reference; not used on M1)
 * [FACT] aic_regs.h:22-29
 *---------------------------------------------------------------------------*/
#define AIC2_CAP0            0x0004
#define AIC2_INFO2           0x0008
#define AIC2_MAXNUMIRQ       0x000C
#define AIC2_LATENCY         0x0204
#define AIC2_IRQ_CFG         0x2000    /* per-IRQ config: bits[3:0] = target CPU mask */
#define AIC3_IRQ_CFG         0x10000

#define AIC23_CAP0_NR_IRQ_MASK    0xFFFFU          /* bits[15:0]  [FACT] aic_regs.h:31 */
#define AIC23_CAP0_LAST_DIE_SHIFT 24
#define AIC23_CAP0_LAST_DIE_MASK  (0xFU << AIC23_CAP0_LAST_DIE_SHIFT)  /* bits[27:24] */

/*---------------------------------------------------------------------------
 * vGIC CPU Interface constants (Apple HW implements GICv3 sysreg interface)
 * [FACT] hv_vgic.h:177-195, hv_vgic.c:49-59
 *
 * The M1 CPU interface exposes:
 *   - 8 List Registers (ICH_LR0_EL2 .. ICH_LR7_EL2)  [FACT] hv_vgic.c:57
 *   - 32 priority levels (5 bits)                      [FACT] hv_vgic.c:51
 *   - 16-bit virtual INTID space                       [FACT] hv_vgic.c:52
 *   - GICv4 / NMI NOT supported                       [FACT] hv_vgic.c:58-59
 *   - Legacy GICv2 (MMIO CPU interface) NOT present   [FACT] hv_vgic.c:55
 *   - Affinity 3 always 0; aff2/1/0 valid             [FACT] hv_vgic.c:53
 *---------------------------------------------------------------------------*/
#define ICH_LR_VIRTUAL_MASK     0xFFFFU
#define ICH_LR_VIRTUAL_SHIFT    0
#define ICH_LR_PRIORITY_MASK    0xFFU
#define ICH_LR_PRIORITY_SHIFT   48
#define ICH_LR_HW_SHIFT         61
#define ICH_LR_HW               (1ULL << 61)
#define ICH_LR_GRP1             (1ULL << 60)
#define ICH_LR_STATE_PENDING    (1ULL << 62)
#define ICH_LR_STATE_ACTIVE     (1ULL << 63)
#define ICH_LR_PHYSICAL_SHIFT   32
#define ICH_LR_PHYSICAL_MASK    0x1FFFUL
#define ICH_LR_MAINTENANCE_IRQ  (1ULL << 41)

/* SGI routing via ICC_SGI1R_EL1  [FACT] hv_vgic.h:198-209, hv_exc.c:469-507 */
#define ICH_SGI_IRQ_SHIFT        24
#define ICH_SGI_IRQ_MASK         0xFU
#define ICH_SGI_TARGETLIST_MASK  0xFFFFU
#define ICH_SGI_IRQMODE_SHIFT    40

/*---------------------------------------------------------------------------
 * vGIC MMIO base addresses (as used by m1n1)
 * 36-bit PA platforms (T8103 / M1): dist=0xF00000000, redist=0xF10000000
 * 42-bit PA platforms (M1 Pro+):    dist=0x5000000000, redist=0x5100000000
 * [FACT] hv_vgic.c:74-82
 *---------------------------------------------------------------------------*/
#define VGIC_DIST_BASE_36BIT     0xF00000000ULL
#define VGIC_REDIST_BASE_36BIT   0xF10000000ULL
#define VGIC_ITS_BASE_36BIT      0xF20000000ULL
#define VGIC_DIST_BASE_42BIT     0x5000000000ULL
#define VGIC_REDIST_BASE_42BIT   0x5100000000ULL
#define VGIC_ITS_BASE_42BIT      0x5200000000ULL

/*---------------------------------------------------------------------------
 * GICv3 Distributor register offsets (for HAL Extension ACPI/GICD table)
 * [FACT] hv_vgic.h:16-116
 *---------------------------------------------------------------------------*/
#define GICD_CTLR        0x0000
#define GICD_TYPER       0x0004
#define GICD_IIDR        0x0008
#define GICD_ISENABLER0  0x0100   /* +4*n : enable IRQ n*32..n*32+31 */
#define GICD_ICENABLER0  0x0180   /* +4*n : disable IRQ */
#define GICD_ISPENDR0    0x0200
#define GICD_ICPENDR0    0x0280
#define GICD_IPRIORITYR0 0x0400   /* +4*n : 8-bit priority per IRQ */
#define GICD_ICFGR0      0x0C00   /* +4*n : level(0) / edge(2) config */
#define GICD_IROUTER32   0x6100   /* +8*n : 64-bit affinity route for SPI n */
