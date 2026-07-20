/*
 * uart_hw.h — Apple S5L (Samsung-derived) UART 하드웨어 레지스터 정의
 *
 * 근거 소스: m1n1_windows/src/uart_regs.h (전 항목 검증됨)
 *            m1n1_windows/src/uart.c, hv_vuart.c (비트 사용 행동 검증됨)
 *
 * 빌드 상태: WDK 미설치 — 컴파일 불가. 스켈레톤 목적 파일.
 * 정직성 표기 범례:
 *   [검증] = m1n1 소스 file:line에서 직접 확인
 *   [추론] = m1n1 소스 행동에서 설계 추론
 *   [미확인] = 외부 지식 또는 Samsung 데이터시트 기반, m1n1에서 미확인
 */

#pragma once

#include <ntddk.h>

/* =========================================================
 * MMIO 기반 주소
 * [검증] uart.c:13  UART_CLOCK = 24000000 Hz
 * [검증] 시스템 문서 UART0 기반: 0x235200000 (4KB 블록 크기 0x4000)
 * [추론] ADT /arm-io/uart0 또는 /arm-io/uart6 경로로 동적 발견(uart.c:22-36)
 * ========================================================= */
#define APPLE_UART0_BASE        0x235200000ULL
#define APPLE_UART_MMIO_SIZE    0x4000UL

/* =========================================================
 * 클록
 * [검증] uart.c:13
 * ========================================================= */
#define APPLE_UART_CLOCK_HZ     24000000UL

/* =========================================================
 * 레지스터 오프셋
 * [검증] uart_regs.h:3-12 (전 항목)
 * ========================================================= */
#define ULCON       0x000   /* Line Control Register */
#define UCON        0x004   /* Control Register */
#define UFCON       0x008   /* FIFO Control Register */
#define UTRSTAT     0x010   /* TX/RX Status Register */
#define UERSTAT     0x014   /* Error Status Register */
#define UFSTAT      0x018   /* FIFO Status Register */
#define UTXH        0x020   /* TX Holding Register (write byte here) */
#define URXH        0x024   /* RX Holding Register (read byte from here) */
#define UBRDIV      0x028   /* Baud Rate Divisor */
#define UFRACVAL    0x02C   /* Fractional Baud Rate Value */

/* =========================================================
 * UCON — Control Register (offset 0x004)
 * [검증] uart_regs.h:14-22 (비트 정의)
 * [검증] hv_vuart.c:71-87 (IRQ 모드 판별 로직)
 * ========================================================= */
#define UCON_TXTHRESH_ENA   (1UL << 13)  /* TX threshold interrupt enable */
#define UCON_RXTHRESH_ENA   (1UL << 12)  /* RX threshold interrupt enable */
#define UCON_RXTO_ENA       (1UL << 9)   /* RX timeout interrupt enable */
#define UCON_TXMODE_MASK    (3UL << 2)   /* [3:2] TX mode */
#define UCON_RXMODE_MASK    (3UL << 0)   /* [1:0] RX mode */

#define UCON_TXMODE_OFF     0UL          /* TX disabled */
#define UCON_TXMODE_IRQ     1UL          /* TX interrupt mode */
#define UCON_RXMODE_OFF     0UL          /* RX disabled */
#define UCON_RXMODE_IRQ     1UL          /* RX interrupt mode */

/* IRQ 모드 활성화 값 (TX+RX 모두 IRQ, timeout 포함) */
#define UCON_IRQ_ALL    (UCON_TXTHRESH_ENA | UCON_RXTO_ENA | \
                         (UCON_TXMODE_IRQ << 2) | (UCON_RXMODE_IRQ << 0))

/* =========================================================
 * UTRSTAT — TX/RX Status Register (offset 0x010)
 * [검증] uart_regs.h:23-28 (비트 정의)
 * [검증] uart.c:47 (TXBE 폴링), uart.c:58 (RXD 폴링), uart.c:120 (TXE 폴링)
 * [검증] uart.c:129 (IRQ 클리어: TXTHRESH|RXTHRESH|RXTO 쓰기)
 * [검증] hv_vuart.c:60-61 (TXBE|TXE 항상 세트)
 * ========================================================= */
#define UTRSTAT_RXTO        (1UL << 9)   /* RX timeout (W1C — 쓰면 클리어) */
#define UTRSTAT_TXTHRESH    (1UL << 5)   /* TX FIFO below threshold (W1C) */
#define UTRSTAT_RXTHRESH    (1UL << 4)   /* RX FIFO above threshold (W1C) */
#define UTRSTAT_TXE         (1UL << 2)   /* TX shift register empty (flush 완료) */
#define UTRSTAT_TXBE        (1UL << 1)   /* TX buffer empty (다음 바이트 쓰기 가능) */
#define UTRSTAT_RXD         (1UL << 0)   /* RX data ready */

/* IRQ 클리어 마스크 (uart.c:129) */
#define UTRSTAT_IRQ_CLEAR   (UTRSTAT_TXTHRESH | UTRSTAT_RXTHRESH | UTRSTAT_RXTO)

/* =========================================================
 * UFSTAT — FIFO Status Register (offset 0x018)
 * [검증] uart_regs.h:30-33
 * [검증] hv_vuart.c:64-69 (RX FIFO 카운트 에뮬)
 * [미확인] BIT(24): hv_vuart.c:165의 HACK 주석 — SAM5250 vs 8900 호환성 비트.
 *          Windows 드라이버가 어떤 시맨틱을 기대하는지 미확인.
 * ========================================================= */
#define UFSTAT_TXFULL       (1UL << 9)   /* TX FIFO full */
#define UFSTAT_RXFULL       (1UL << 8)   /* RX FIFO full */
#define UFSTAT_TXCNT_MASK   (0xFUL << 4) /* [7:4] TX FIFO byte count (max 15) */
#define UFSTAT_RXCNT_MASK   (0xFUL << 0) /* [3:0] RX FIFO byte count (max 15) */
#define UFSTAT_TXCNT_SHIFT  4
#define UFSTAT_RXCNT_SHIFT  0

/* [미확인] SAM5250 TX-busy 비트 — Windows 드라이버가 이 비트를 확인할 수 있음 */
#define UFSTAT_SAM5250_TXBUSY   (1UL << 24)

/* =========================================================
 * UBRDIV / UFRACVAL — 보드레이트 설정
 * [검증] uart.c:112  UBRDIV = ((UART_CLOCK / baudrate + 7) / 16) - 1
 * [추론] UFRACVAL 사용 예시 없음 — m1n1은 초기화하지 않음 (펌웨어 기본값 사용)
 *
 * 예시 (115200 baud):
 *   UBRDIV = ((24000000 / 115200 + 7) / 16) - 1 = 12
 *   실제 보드레이트: 24000000 / (16 * 13) = 115384 bps (오차 0.16%)
 * ========================================================= */
#define APPLE_UART_UBRDIV_FOR_BAUD(baud) \
    (((APPLE_UART_CLOCK_HZ / (baud) + 7) / 16) - 1)

/* 공통 보드레이트 UBRDIV 값 */
#define UBRDIV_9600     (APPLE_UART_UBRDIV_FOR_BAUD(9600))    /* = 155 */
#define UBRDIV_115200   (APPLE_UART_UBRDIV_FOR_BAUD(115200))  /* = 12  */
