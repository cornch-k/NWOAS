# Mac mini(J274, t8103) 내장 스피커 하드웨어 경로 조사

작성: 2026-11-16 · 읽기 전용 조사(하드웨어 실행 없음)

## 0. 결론 요약

- **메모리 → 스피커 경로는 SIO 코프로세서 펌웨어 없이 CPU가 직접 다룰 수 있습니다.** Asahi Linux는 ADMAC을 직접 레지스터로 구동하고, SIO는 DisplayPort 오디오에만 씁니다(근거 §1.3).
- 경로: **메모리 버퍼 → DART-sio(스트림 2) → ADMAC TX 채널 → MCA 클러스터(SERDES TXA) → MCA I2S 포트 0 → TAS5770L(I2C1 주소 0x31) → 스피커.**
- 앰프는 공개 자료가 없는 TAS5770L이지만, Asahi와 m1n1 모두 **TAS2770 레지스터 배치로 다룹니다**(근거 §2.5).
- **가장 큰 위험은 앰프 리셋 직후 디지털 볼륨이 0 dB(최대)라는 점입니다.** 소리를 내기 전에 볼륨 레지스터부터 낮춰야 합니다(§3).
- m1n1 저장소에 이미 **Mac mini 스피커 재생 실험 스크립트**(`proxyclient/experiments/speaker_amp.py`, MIT 라이선스)가 있습니다. 이것이 "m1n1 쪽에서만 소리 내기" 시험의 출발점입니다(§4).

## 출처 표기

| 약칭 | 대상 | 판(버전) |
|---|---|---|
| `L:` | Asahi Linux 커널 `https://github.com/AsahiLinux/linux/blob/6ba0637d1ed9a2e3639dd3fc9eca6cd0483a6434/<경로>#L<줄>` | asahi 브랜치, 커밋 `6ba0637d1ed9` (2026-11-05) |
| `S:` | speakersafetyd `https://github.com/AsahiLinux/speakersafetyd/blob/a87e6da5627150edd3ac45c622739c5998ebc3a1/<경로>` | 커밋 `a87e6da5` (2026-10-28) |
| `M:` | 로컬 m1n1 트리 `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-gpu-hv2/<경로>` | git 저장소 아님(커밋 정보 없음) |
| `ADT:` | 실기 ADT `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin` | 2026-11-16 채취 |

[가설]은 검증되지 않은 추정입니다. 표기가 없는 문장은 위 출처에 그대로 있는 사실입니다.

---

## 1. 메모리에서 스피커까지의 경로

### 1.1 장치 주소·인터럽트 한눈에 보기

| 블록 | 물리 주소 | 크기 | AIC 인터럽트 | 근거 |
|---|---|---|---|---|
| ADMAC(admac-sio) | 0x2_3820_0000 | 0x34000 | 626 (출력 번호 1) | `L:arch/arm64/boot/dts/apple/t8103.dtsi#L930-L942`, ADT `admac-sio` reg·interrupts=0x272 |
| DART-sio | 0x2_3500_4000 | 0x4000 | 635 | `L:t8103.dtsi#L749-L756`, ADT `dart-sio` interrupts=0x27b |
| MCA 클러스터 0~5 | 0x2_3840_0000 + 0x4000×n | 0x4000씩 | 619+n | `L:t8103.dtsi#L986-L1017`, ADT `mca0`/`mca1` reg·interrupts(0x26b, 0x26c) |
| MCA 스위치(DMA 어댑터) | 0x2_3830_0000 + 0x8000×n (+0x4000은 B쪽) | 0x30000 | — | `L:sound/soc/apple/mca.c#L110-L120`, ADT `mca1` reg 2·3번째 항목(0x2_3830_8000, 0x2_3830_C000) |
| MCA 클럭 선택(먹스) | 0x2_3B04_00D4 | 0x18 (클러스터당 4바이트) | — | ADT `mca-switch` reg 3번째 항목, `M:src/clk.c#L9-L37` |
| NCO(클럭 발생기) 채널 0~4 | 0x2_3B04_4000 + 0x4000×n | 0x14000 | — | `L:t8103.dtsi#L1020-L1025`, ADT `nco` reg |
| I2C1 | 0x2_3501_4000 | 0x4000 | 628 | `L:t8103.dtsi#L771-L782`, ADT `i2c1` reg·interrupts=0x274 |
| GPIO(AP) | 0x2_3C10_0000 | 0x100000 | — | `L:t8103.dtsi#L1068-L1076` |
| PMGR 전원 상태 레지스터 | 0x2_3B70_0000 + 오프셋 | — | — | `L:t8103-pmgr.dtsi` |

### 1.2 스피커 쪽 연결 정보

| 항목 | 값 | 근거 |
|---|---|---|
| 앰프 | TAS5770L, I2C1 주소 0x31, `compatible = "ti,tas5770l", "ti,tas2770"` | `L:arch/arm64/boot/dts/apple/t8103-j274.dts#L88-L98`, ADT `i2c1/audio-tas5770L-speaker` reg=0x31 |
| 앰프 종료(SDZ) 핀 | AP GPIO 181, 높음(1)=동작 | `L:t8103-j274.dts#L92`, ADT `function-reset` = `<gpio 노드, 'GPIO', 0xb5(=181), 0x00010001>` |
| 앰프 인터럽트 | AP GPIO 182 (ADT에만 있음, Asahi는 안 씀) | ADT `interrupts = b6000000 01000000`, `interrupt-parent` = `/arm-io/gpio` |
| 스피커가 붙은 I2S 포트 | **포트 0** (Asahi DT `sound-dai = <&mca 0>`) | `L:t8103-j274.dts#L120-L129`, `M:proxyclient/experiments/speaker_amp.py#L54`(`cl_no = 0`) |
| 이어폰 단자 코덱(참고) | CS42L83, I2C2 0x48, I2S 포트 2 | `L:t8103-j274.dts#L100-L113,L131-L140` |
| I2C1 핀 | SCL=GPIO 201, SDA=GPIO 199, 주변기능 1 | `L:t8103.dtsi#L1094-L1097`, ADT `gpio-iic_scl`=0xc9, `gpio-iic_sda`=0xc7 |
| Asahi의 SIO 사용 | ADMAC 노드에는 메일박스가 없고, `sio` 노드는 `status = "disabled"`, SIO DMA는 `dpaudio`(DP 오디오)만 씀 | `L:t8103.dtsi#L918-L970` |

**MCA 구조(사실, `L:sound/soc/apple/mca.c#L1-L21` 머리말):** MCA는 같은 모양의 "클러스터" 6개로 되어 있고, 클러스터마다 클럭 부모·SYNC(프레임 클럭) 발생기·SERDES 4개(TXA/RXA/TXB/RXB)·전용 I2S 포트를 가집니다. **I2S 포트는 아무 클러스터에나 연결할 수 있습니다.** 그래서 "데이터를 만드는 클러스터(Linux 용어로 frontend)"와 "핀으로 나가는 포트(backend)"를 따로 고릅니다.

- Asahi: 스피커 포트 0에 frontend로 `mca-pcm-0`(Primary, 비트클럭=64×샘플레이트) 또는 `mca-pcm-1`(Secondary, 256×)을 연결합니다. 사용자 공간이 `Speaker Playback Mux`로 고릅니다(`L:sound/soc/apple/macaudio.c#L150-L229, L1523-L1530`).
- m1n1 실험: 클러스터 0을 frontend 겸 포트 0으로 씁니다(`M:proxyclient/experiments/speaker_amp.py#L54-L101`).
- macOS(ADT): `mca1/mca1a` 아래에 스피커가 있고, `dma-channels` 첫 값이 0x2c(44), `admac-sio`의 `channels-offset`이 0x28(40)이므로 **ADMAC 채널 4(=클러스터 1의 TXA)** 를 씁니다. 즉 macOS는 클러스터 1을 frontend로 씁니다. [가설] `function-i2s_route`(`'i2sR', 'aud2', 'ipn0', 02 02 02 00`)가 클러스터 1 → 포트 연결을 뜻하는 것으로 보이나 형식은 해독하지 못했습니다.

### 1.3 ADMAC(오디오 DMA)

**SIO 펌웨어가 필요 없습니다.** Linux `apple-admac.c`는 ADMAC 레지스터를 직접 씁니다. 채널 방향은 고정이며 **짝수=TX(메모리→장치), 홀수=RX** 입니다(`L:drivers/dma/apple-admac.c#L233-L237`). Asahi DT의 채널 배정은 `tx{n}a = 4n`, `rx{n}a = 4n+1`, `tx{n}b = 4n+2`, `rx{n}b = 4n+3` 입니다(`L:t8103.dtsi#L1003-L1014`). 따라서 **클러스터 0 TXA = 채널 0, 클러스터 1 TXA = 채널 4** 입니다.

| 레지스터(ADMAC 기준 오프셋) | 용도 | 근거 |
|---|---|---|
| 0x0000 TX_START / 0x0004 TX_STOP | 비트 `1 << (채널/2)` 를 써서 TX 채널 시작/정지 | `L:apple-admac.c#L44-L47, L420-L459` |
| 0x0094 / 0x0098 | TX/RX SRAM 크기(읽기) | `#L51-L52` |
| 0x8000 + 0x200×ch | 채널 제어. 비트0=링 초기화(1 썼다가 0) | `#L54-L55, L461-L468` |
| 0x8010/0x8020 + 0x200×ch + 4×idx | 채널 인터럽트 상태/마스크(idx=IRQ 출력 번호). 비트0 완료, 비트6 오류 | `#L86-L87, L37-L38` |
| 0x8040 + 0x200×ch | 버스 폭: 비트3:0 워드(0=8,1=16,2=32비트), 비트7:4 프레임(0x10=2워드, 0x20=4워드) | `#L62-L70` |
| 0x8050 + 0x200×ch | SRAM 할당: 상위16 크기, 하위16 시작. Linux는 2 KiB 단위로 나눠 줌 | `#L72-L74, L159-L191` |
| 0x8054 + 0x200×ch | FIFO: 상위16 한계, 하위16 문턱. Linux 값 = 0x30×워드크기 / 0x18×워드크기 (32비트면 0xC0 / 0x60) | `#L76-L78, L810-L812` |
| 0x8064 / 0x8070 / 0x8074 + 0x200×ch | 남은 바이트 / 서술자 링 상태 / 보고 링 상태(비트8 빔, 9 가득, 10 오류, 1:0 쓰기 칸, 5:4 읽기 칸) | `#L31-L35, L57-L60` |
| 0x10000 + (ch/2)×4 + (ch&1)×0x4000 | **서술자 쓰기 창**: 주소 하위32, 상위32, 길이, 플래그(비트16 = 완료 알림) 순서로 4번 씀. 링 깊이 4 | `#L80, L296-L316, L322-L332` |
| 0x10100 + … | 보고 읽기 창(4워드씩) | `#L81, L609-L629` |

- ADT도 같은 FIFO 값을 담고 있습니다. `mca1a/dma-channels`의 첫 항목 `2c 21 00 c0 60 …`(채널 44, ?, 0, 0xC0, 0x60) [가설: 뒤의 두 값이 FIFO 한계·문턱].
- IRQ 출력: ADT `irq-destinations`를 4글자씩 풀면 `SCPU, AIC , …`이고 `irq-destination-index = 1` 입니다. Linux도 출력 1번(AIC 626)을 씁니다(`L:t8103.dtsi#L934-L937`). [가설] 출력 0은 SIO CPU로 가는 선입니다.
- ADMAC 두 번째 reg(0x2_3D2A_8000, 8바이트)의 용도는 찾지 못했습니다.

### 1.4 DART-sio(입출력 주소 변환)

| 사실 | 근거 |
|---|---|
| ADMAC은 DART-sio의 **스트림 2** 뒤에 있습니다 | `L:t8103.dtsi#L939`(`iommus = <&sio_dart 2>`), ADT `dart-sio/mapper-admac` reg=2 |
| 같은 DART를 SIO(스트림 0), AES(스트림 1)도 씁니다 | ADT `mapper-sio` reg=0, `mapper-aes` reg=1 |
| 버퍼 매핑은 일반 DMA 매핑(DART 페이지 테이블)으로 합니다 | Linux는 DMA API(`L:sound/soc/apple/mca.c` `snd_pcm_set_managed_buffer`), m1n1 실험은 `DART.initialize()` 후 `iomap(stream 2, …)`(`M:proxyclient/m1n1/hw/admac.py#L383-L389`) |
| 우회(bypass)는 하드웨어가 `PARAMS2`(DART+0x04) 비트0을 세울 때만 지원됩니다. 스트림별 TCR = DART+0x100+4×스트림, 우회 비트 = 비트8·비트12 | `L:drivers/iommu/apple-dart.c#L48-L49, L85-L88, L1474-L1478, L1602` |
| DART-sio 전원 = SIO_CPU 도메인 | ADT `dart-sio` clock-gates 0x141 → 부모 SIO_CPU(0x23) |

- **DART-sio가 우회를 지원하는지는 소스로 알 수 없습니다.** 실기에서 0x2_3500_4004를 읽어 비트0을 보면 확정됩니다(읽기만). [가설] ADT의 `bypass = 0x00008000`, `sids = 0x8007`이 관련 값일 수 있습니다.
- 우회가 안 되면 Windows 드라이버(또는 하이퍼바이저)가 스트림 2용 페이지 테이블을 직접 만들어야 합니다. m1n1에는 이미 C로 된 DART 코드(`M:src/dart.c`, `dart_init_adt`/`dart_map`)가 있습니다.

### 1.5 MCA 클러스터·포트 레지스터

| 위치 | 레지스터 | 의미 | 근거 |
|---|---|---|---|
| 클러스터+0x000 | STATUS | 비트0 MCLK 켜기 | `L:mca.c#L47-L48` |
| +0x004 | MCLK_CONF | 비트11:8 분주(Linux는 1) | `#L49-L50, L708` |
| +0x100 | SYNCGEN_STATUS | 비트0 프레임 클럭 발생기 켜기 | `#L52-L53` |
| +0x104 | SYNCGEN_MCLK_SEL | 클럭 공급자일 때 `클러스터번호+1` | `#L54-L55, L316` |
| +0x108 / +0x10C | HI/LO 주기 | Linux: `비율/2-1`, `(비율+1)/2-1`(비율=비트클럭/샘플레이트) | `#L56-L57, L701-L705` |
| +0x300 | TXA STATUS | 비트0 켜기, 비트1 리셋 | `#L73, L86-L88` |
| +0x304 | TXA CONF | 비트3:0 슬롯수-1, 비트8:4 폭(0x40=16, 0xC0=24, 0x100=32비트), 비트10 BCLK 극성, 비트12~14 TX용 미상 비트(Linux는 셋 다 1), 비트18:16 SYNC 선택(=클러스터+1) | `#L91-L103, L420-L463` |
| +0x308 | TXA BITSTART | I2S면 1, 왼쪽 정렬이면 0 | `#L104, L584-L587` |
| +0x30C~+0x318 | TXA 슬롯 마스크 4개 | `0xffffffff, ~mask, 0xffffffff, ~mask` (0=사용 슬롯) | `#L106, L466-L480` |
| 포트(클러스터 n 블록)+0x600 | PORT_ENABLES | 비트2:1 클럭 출력, 비트3 TX 데이터 | `#L59-L61` |
| +0x604 | PORT_CLOCK_SEL | 비트11:8 = 클럭을 주는 클러스터+1 | `#L62-L63, L837-L838` |
| +0x608 | PORT_DATA_SEL | `1 << (2×클러스터)` = 그 클러스터의 TXA | `#L64-L66, L815-L816` |
| 스위치+0x8000×n | DMA 어댑터 A | 비트4:0 TX LSB 패딩(=32-샘플비트), 6:5 TX 채널(Linux 2), 12:8 RX 패딩, 14:13 RX 채널(Linux 2), 22:20 채널수(최대 4) | `#L110-L117, L683-L699` |
| 0x2_3B04_00D4 + 4×n | MCA 클럭 먹스 | 비트27:24 = 5+min(4,n) → 클러스터 n이 NCO n을 씀(5번은 NCO 4 공유) | `M:src/clk.c#L9-L37` |

**시작 순서(Linux `mca_fe_early_trigger`, `L:mca.c#L197-L238`):** SYNC 선택을 0 → 7로 바꿈 → SERDES 리셋 비트만 1 → 50 µs 대기 → SYNC 선택 0 → 클러스터+1 → 100 µs 대기 → ADMAC 시작 → SERDES 켜기 비트 1(`#L240-L253`).

**주의(사실):** m1n1의 `clk_init()`(MCA 클럭 먹스 설정)은 `kboot_boot()` 안에서만 불립니다(`M:src/kboot.c#L2814-L2818`). [가설] 하이퍼바이저로 Windows를 띄우는 경로에서는 이 함수가 안 불릴 수 있으므로, Windows 드라이버가 먹스를 직접 써 주는 편이 안전합니다(같은 값을 다시 써도 무해).

### 1.6 클럭(NCO)

| 항목 | 값 | 근거 |
|---|---|---|
| NCO 입력 클럭 | 900 MHz | ADT `/arm-io/nco` clock-ids[0]=0x148 → `/arm-io clock-frequencies[72]` = 900,000,000; `L:arch/arm64/boot/dts/apple/t8103-jxxx.dtsi#L303-L305` |
| 채널 레지스터 | +0 제어(비트31 켜기), +4 분주(비트12:2 = LFSR로 바꾼 정수부, 비트1:0 소수부), +8 INC1, +12 INC2, +16 누산기 초기값(1<<31) | `L:drivers/clk/clk-apple-nco.c`(라이선스 GPL-2.0-only OR MIT), `M:proxyclient/m1n1/hw/nco.py` |
| 계산 | `div = 2·fin/fout`, `inc1 = 2·fin − div·fout`, `inc2 = inc1 − fout`; 분주 정수부는 다항식 0xa01·초기값 0x7ff인 11비트 갈루아 LFSR 표로 변환 | 같은 파일 `applnco_set_rate` |
| 필요한 비트클럭 | 48 kHz × 64 = 3.072 MHz(Primary), × 256 = 12.288 MHz(Secondary·m1n1 실험) | `L:macaudio.c#L200-L229`, `M:speaker_amp.py#L66` |
| 비트클럭 상한 | 24.576 MHz (TAS2770 한계 27.1 MHz) | `L:macaudio.c#L48-L56` |

NCO는 ADT에 `clock-gates`가 없고 Asahi DT에도 전원 도메인이 없습니다.

### 1.7 형식·속도·슬롯

| 항목 | MCA | TAS2770 드라이버 | 근거 |
|---|---|---|---|
| 샘플레이트 | 8 k~192 kHz | 44.1/48/96/192 kHz (88.2/176.4도 설정 함수에 있음) | `L:mca.c#L1234`, `L:sound/soc/codecs/tas2770.c#L295-L338, L587-L590` |
| 샘플 형식 | S16_LE, S24_LE(32비트 칸), S32_LE | S16, S20_3, S24, S32 | `L:mca.c#L124-L126`, `tas2770.c#L584-L585` |
| 채널 | 1~32 | 1~2 | 같은 곳 |
| 기본 데이터 형식 | I2S, CPU가 BCLK·LRCLK 공급, 두 클럭 반전(IB_IF), GATED | 같은 형식 받음 | `L:macaudio.c#L38-L45`, `tas2770.c#L356-L434` |
| 스피커 슬롯 | J274는 앰프 1개 → 2채널 배정, 마스크 0b11, 슬롯 폭 32비트 → TAS 왼쪽 슬롯 0, 오른쪽 슬롯 1 | | `L:macaudio.c#L662-L708, L974-L993` |
| 전류·전압 감지 | I 감지 TX 슬롯 0, V 감지 TX 슬롯 2 (감지 frontend는 16비트×16슬롯) | | `L:t8103-j274.dts#L94-L95`, `L:macaudio.c#L1055-L1063` |

ADMAC 쪽 버퍼는 32비트 워드, 프레임 1워드(모노)로 두고, 24비트 샘플이면 DMA 어댑터 TX 패딩을 8로 둡니다(`L:mca.c#L677`, `M:speaker_amp.py#L62-L63, L101`).

---

## 2. 최소 초기화 순서

### 2.1 전원 도메인(PMGR)

PMGR 레지스터 1개 = 4바이트. 비트3:0 목표 상태(0xF 켜짐, 0x4 클럭 끔, 0 전원 끔), 비트7:4 실제 상태, 비트10 장치 끄기, 비트31 리셋(`L:drivers/pmdomain/apple/pmgr-pwrstate.c#L20-L36`, 라이선스 GPL-2.0-only OR MIT). 부모부터 켜야 합니다.

| 이름 | ADT 장치 번호 | 주소 | 부모 | 근거 |
|---|---|---|---|---|
| SIO_BUSIF | 0x21 | 0x2_3B70_01C0 | — | ADT pmgr devices, `L:t8103-pmgr.dtsi#L214-L220` |
| SIO | 0x22 | 0x2_3B70_01C8 | SIO_BUSIF | `#L222-L229` |
| PMS | 0x11 | 0x2_3B70_0140 | (항상 켜짐) | `#L78-L85` |
| SIO_CPU (DART-sio용) | 0x23 | 0x2_3B70_01D0 | SIO (+UART_P, SPI_P, DPA0) | `#L231-L238` |
| AUDIO_P | 0x2e | 0x2_3B70_0228 | SIO | `#L327-L334` |
| SIO_ADMA | 0x2f | 0x2_3B70_0230 | SIO, PMS, AUDIO_P | `#L336-L343` |
| MCA0 / MCA1 | 0x3f / 0x40 | 0x2_3B70_02B8 / 0x2_3B70_02C0 | AUDIO_P, SIO_ADMA, `apple,externally-clocked` | `#L489-L507` |
| I2C1 | 0x28 | 0x2_3B70_01F8 | SIO | `#L273-L280` |
| GPIO | 0xf | 0x2_3B70_0130 | — | ADT pmgr devices |

- ADMAC과 MCA는 **AUDIO_P 리셋**을 공유합니다(`L:t8103.dtsi#L941, L996`). m1n1 실험은 AUDIO_P에 `장치 끄기=1 → 리셋=1 → 리셋=0 → 장치 끄기=0` 순으로 리셋합니다(`M:speaker_amp.py#L41-L46`).
- MCA 클러스터 전원은 "클럭이 이미 들어오는 상태에서" 켜야 합니다. Linux 주석: 클럭이 꺼진 장치로 보이면 전원 드라이버가 오류를 냅니다(`L:mca.c#L301-L305`). 즉 NCO를 먼저 켜고 MCA 클러스터 전원을 켭니다.
- m1n1 실험은 위 도메인을 ADT `clock-gates` 기준으로 켭니다: `i2c1`, `admac-sio`, `dart-sio`, `mca-switch`(`M:speaker_amp.py#L36-L39`).

### 2.2 클럭

1. MCA 클럭 먹스: 0x2_3B04_00D4 + 4n의 비트27:24 = 5+n(n=사용 클러스터) (`M:src/clk.c`).
2. NCO 채널 n: 비율에 맞춰 분주·INC1·INC2·누산기 초기값을 쓰고 비트31로 켭니다(§1.6).
3. 클러스터: MCLK_CONF 분주 1, SYNCGEN_MCLK_SEL = n+1, HI/LO 주기, SYNCGEN 켜기, STATUS MCLK 켜기(`L:mca.c#L290-L321, L700-L709`).

### 2.3 앰프 리셋 핀(ADT `function-reset` 해독)

- ADT 값 `66000000 4f495047 b5000000 01000100` = (phandle 0x66 = `/arm-io/gpio`, 'GPIO', 핀 181, 0x00010001). [가설] 마지막 값은 극성·초기값 같은 플래그로 보이나 해독하지 못했습니다. Asahi는 같은 핀을 "높음=동작"인 `shutdown-gpios`로 씁니다(`L:t8103-j274.dts#L92`).
- 핀 레지스터 = 0x2_3C10_0000 + 4×181 = **0x2_3C10_02D4**. 비트0 데이터, 비트3:1 모드(1=출력), 비트6:5 주변기능, 비트9 입력 켜기, 비트11:10 구동세기, 비트18:16 IRQ 그룹(`L:drivers/pinctrl/pinctrl-apple-gpio.c#L45-L66`).
- m1n1 실험은 0x76a02(출력, 0) → 0x76a03(출력, 1)을 씁니다(`M:speaker_amp.py#L103-L105`). 이 값을 풀면 모드=1, 입력켜기=1, 구동세기=2, IRQ 그룹=7입니다. [권장] 값을 통째로 덮어쓰지 말고 읽은 값에서 비트0만 바꾸는 편이 안전합니다.
- Linux 전원 순서: SDZ=1 → 1~2 ms 대기 → 소프트 리셋(레지스터 0x01 ← 0x01) → 1~2 ms 대기(`L:tas2770.c#L34-L46, L720-L759`).

### 2.4 I2C1(PA Semi 계열 컨트롤러) 레지스터 쓰기

| 레지스터 | 오프셋 | 비트 | 근거 |
|---|---|---|---|
| MTXFIFO | 0x00 | 7:0 데이터, 8 START, 9 STOP, 10 READ | `L:drivers/i2c/busses/i2c-pasemi-core.c#L23-L35` |
| MRXFIFO | 0x04 | 7:0 데이터, 8 비었음 | `#L37-L38` |
| SMSTA | 0x14 | 27 전송 끝(XEN), 21 NACK, 23 시간초과 … (1을 써서 지움) | `#L40-L49` |
| CTL | 0x1C | 7:0 클럭 분주, 8 자동 풀기, 9/10 TX/RX FIFO 리셋, 11 켜기(Apple 전용) | `#L51-L55` |

- 분주 = ⌈기준클럭 / (16 × 버스속도)⌉, 4 이상(`L:i2c-pasemi-platform.c#L24-L36`). 기준클럭은 24 MHz(`clkref`, `L:t8103.dtsi#L390-L395`)이므로 100 kHz면 15입니다. m1n1은 4를 씁니다(`M:proxyclient/m1n1/hw/i2c.py#L177`) [가설: 약 375 kHz].
- **레지스터 1개 쓰기 순서**(m1n1 `I2C.write_reg`, `M:proxyclient/m1n1/hw/i2c.py#L173-L181`, MIT):
  1. CTL에 FIFO 리셋 비트 세움, SMSTA에 0xFFFFFFFF(상태 지움)
  2. CTL = 켜기(비트11) | 분주
  3. MTXFIFO ← `(0x31<<1) | START`
  4. MTXFIFO ← 레지스터 번호
  5. MTXFIFO ← 값(마지막 바이트에 STOP)
  6. SMSTA의 XEN(비트27)을 기다림 → NACK(비트21) 확인
- m1n1에는 C 구현도 있습니다(`M:src/i2c.c`, `i2c_smbus_write`/`i2c_smbus_read8`).

### 2.5 TAS5770L 레지스터(TAS2770 배치 기준)

TAS5770L은 공개 부품이 아니며 TAS2770과 레지스터가 비슷하다는 것이 m1n1 실험 주석의 판단입니다(`M:speaker_amp.py#L114-L119`). Asahi도 `ti,tas2770` 드라이버를 그대로 씁니다. 모든 레지스터는 페이지 0(레지스터 0x00 = 페이지 선택)에 있습니다.

| 번호 | 이름 | 의미 | 리셋 기본값 | 근거 |
|---|---|---|---|---|
| 0x01 | SW_RST | 비트0 소프트 리셋 | 0x00 | `L:sound/soc/codecs/tas2770.h#L17-L19` |
| 0x02 | PWR_CTRL | 비트1:0: 0=동작, 1=음소거, 2=종료. 비트2 V감지 끔, 비트3 I감지 끔 | **0x0E**(종료, 감지 끔) | `tas2770.h#L21-L25`, `tas2770.c#L161-L164, L803` |
| 0x03 | PLAY_CFG_REG0 | 비트4:0 앰프 이득: 11 dB + 0.5 dB×값, 0~0x14(21 dB) | 0x10(19 dB) | `tas2770.c#L768, L774-L775, L804` |
| 0x05 | PLAY_CFG_REG2 | **디지털 볼륨: 값 v → −0.5·v dB. 0x00 = 0 dB(최대), 0xC9 = −100.5 dB** | **0x00(0 dB)** | `tas2770.h#L31-L32`, `tas2770.c#L769, L772-L773, L806` |
| 0x0A | TDM_CFG_REG0 | 비트5 44.1k계열, 비트3:1 속도대역(0x6=44.1/48k), 비트0 FSYNC 극성 | — | `tas2770.h#L36-L45` |
| 0x0B | TDM_CFG_REG1 | 비트5:1 수신 시작 오프셋, 비트0 수신 클럭 가장자리 | 0x02 | `#L48-L52` |
| 0x0C | TDM_CFG_REG2 | 비트5:4 모노 소스(0 I2C주소, 1 왼쪽, 2 오른쪽, 3 (L+R)/2), 비트3:2 워드 길이(16/20/24/32), 비트1:0 슬롯 길이(16/24/32) | 0x0A | `#L54-L63`, `M:proxyclient/m1n1/hw/codecs/__init__.py#L28-L48` |
| 0x0D | TDM_CFG_REG3 | 비트7:4 오른쪽 슬롯, 비트3:0 왼쪽 슬롯 | 0x10 | `#L64-L68` |
| 0x0F / 0x10 | TDM_CFG_REG5/6 | 비트6 V/I 감지 송신 켜기, 비트5:0 송신 슬롯 | — | `#L78-L88` |
| 0x1B | BO_PRV_REG0 | 저전압(브라운아웃) 방지 | — | `#L94-L95` |
| 0x24/0x25 | 래치 인터럽트 | 0x24 비트1 과전류, 비트0 과열 | — | `#L108-L118` |
| 0x29/0x2A | 온도 | `(MSB<<4 | LSB>>4)/16 − 93` °C | — | `tas2770.c#L615-L647` |

**Apple 전용 ADT 값과의 관계:**

| ADT 속성 | 원값 | 해석 | 확실도 |
|---|---|---|---|
| `bop-config` | `82 06 50 04 00…` | m1n1 실험이 0x1B ← `01 82 06`, 0x16 ← `50 04`를 씀(`M:speaker_amp.py#L126-L127`). 바이트 0~1 → 레지스터 0x1C~0x1D, 바이트 2~3 → 0x16~0x17 | [가설] 값이 일치함. 각 비트 의미는 모름 |
| `speaker-config` | `00 14 00 00` | 0x14 = 앰프 이득 값 20(21 dB). Asahi J274 설정의 `gain = 20`과 같음(`L:macaudio.c#L1547-L1549`) | [가설] |
| `speaker-protection` | `00000000` | 의미 모름 | — |
| `iboot-audio-volume` | 비어 있음 | 의미 모름 | — |
| `external-power-provider` | phandle 0xe3 = `/arm-io/mca1/mca1a` | macOS에서 앰프 전원이 MCA1 쪽에 묶여 있다는 뜻으로 보임 | [가설] |

**재생 시작 최소 순서(사실은 각 출처, 순서 조합은 [가설]):**

1. 앰프가 종료 상태(0x02=0x0E)인지 확인.
2. **0x05 ← 안전한 볼륨(§3), 0x03 ← 낮은 이득(예: 0x00)** — 반드시 동작 전에.
3. 0x0A(속도·FSYNC 극성), 0x0B(오프셋·가장자리), 0x0C(모노 소스·워드·슬롯 길이), 0x0D(슬롯) 설정. Linux 형식(I2S+IB_IF)이면 0x0B의 비트0=1, 오프셋=1(`tas2770.c#L372-L431`).
   - m1n1 실험 값(다른 클럭 설정과 짝): 0x08←0x40(의미 모름), 0x0A←`06 00 1a`(48k, 오프셋 0, 왼쪽·24비트 워드·32비트 슬롯), 0x0D←0x00(`M:speaker_amp.py#L124-L128`).
4. MCA 클럭(BCLK·FSYNC)을 먼저 내보냄. Linux 주석: 앰프는 음소거 해제 시점에 클럭이 있어야 함(`L:mca.c#L785-L790`). m1n1 주석: 앰프는 클럭을 잃으면 스스로 종료 상태로 감(`M:speaker_amp.py#L120-L121`).
5. 0x02 ← 0x0C(동작, 감지 끔) 또는 0x00(동작, 감지 켬).
6. 끝낼 때: 0x02 ← 0x0D(음소거) → 0x0E(종료) → SDZ=0(`M:speaker_amp.py#L142-L146`).

### 2.6 인터럽트(첫 시험에는 필요 없음)

ADMAC AIC 626, MCA 클러스터 619~624, I2C1 628, DART-sio 635, 앰프 GPIO 182. 첫 시험은 ADMAC 링 상태(0x8070)와 I2C SMSTA를 폴링하면 됩니다(m1n1 실험도 폴링만 씀).

---

## 3. 스피커 안전

### 3.1 Asahi가 하는 일

**(가) 데몬이 없을 때 커널이 볼륨을 사실상 무음으로 묶습니다.** `L:sound/soc/apple/macaudio.c#L231-L262`:

```c
	case AMP_TAS5770:
		if (unlock)
			max = TAS5770_0DB;
		else
			max = 1; //TAS5770_DB(ma->cfg->safe_vol);
```

- `TAS5770_0DB = 201`(`#L92-L96`). 컨트롤 값 c와 레지스터 0x05의 관계는 `레지스터 = 0xC9 − c`, 0.5 dB 단위입니다(`tas2770.c#L769-L773`, 반전 컨트롤). 따라서 **잠금 상태 상한 c≤1 → 레지스터 ≥ 0xC8 → −100 dB 이하(사실상 무음)**, 해제 상태 상한 c≤201 → 0 dB입니다.
- 주석으로 막힌 원래 의도는 `safe_vol = −20` → c = 161 → **레지스터 0x28(−20 dB)** 이었습니다(`#L1543-L1549`, 표 주석 `#L1607` "general -20dB safe level standard").

**(나) 해제 조건(`#L271-L321, L1403-L1487`):** 사용자 공간 프로세스가 `Speaker Volume Unlock` 컨트롤을 잠그고(`lock`), 재생 중 **250 ms마다** 마법값 `0xdec1be15`를 써야 합니다(`#L58-L60, L1418, L1431`). 시간이 넘거나 다른 프로세스가 볼륨 컨트롤을 잡으면 즉시 다시 잠깁니다.

**(다) 스피커 링크 자체:** 모델 설정의 `enable_speakers`가 거짓이면 `please_blow_up_my_speakers` 없이는 스피커를 아예 만들지 않습니다(`#L602-L607`). J274는 `enable_speakers = true, AMP_TAS5770, 1W, 모노, gain 20, safe_vol −20`입니다(`#L1547-L1549`).

**(라) speakersafetyd(사용자 공간 열 모델):** V/I 감지 신호로 코일·자석 온도를 계산해 볼륨을 줄입니다. J274 설정(`S:conf/apple/j274.conf`):

| 항목 | 값 |
|---|---|
| 주변 온도 가정 `t_ambient` | 46.0 °C |
| 코일 한계 `t_limit` / 여유 `t_headroom` | 140.0 / 40.0 °C |
| 열저항 코일/자석 `tr_coil`/`tr_magnet` | 40.0 / 60.0 °C/W |
| 시정수 코일/자석 | 3.7 / 250 s |
| 공칭 임피던스 `z_nominal` | 4.6 Ω |
| 감지 채널 I/V | 0 / 1 |

계산식(`S:src/types.rs#L302-L310`): 연속 허용 전력 = (140−46)/(40+60) = **0.94 W**, 최악 피크 전력 = 10^(이득/10)/4.6×2, 최소 감쇠 = 10·log10(허용/피크).

| 앰프 이득(레지스터 0x03) | 풀스케일 출력 [가설: dBV는 풀스케일 사인 기준] | 4.6 Ω 연속 전력 | speakersafetyd 최소 감쇠 |
|---|---|---|---|
| 0x00 (11 dB) | 3.55 Vrms | 2.74 W | −7.6 dB |
| 0x10 (19 dB, 리셋값) | 8.91 Vrms | 17.3 W | −15.6 dB |
| 0x14 (21 dB, J274 설정) | 11.2 Vrms | 27.4 W | **−17.7 dB** |

→ Asahi의 "−20 dB" 기준은 21 dB 이득에서 열 모델 최악값(−17.7 dB)보다 조금 더 보수적인 값입니다.

### 3.2 첫 시험용 보수적 상한(열 모델 없음) [가설·권장]

| 항목 | 권장값 | 이유 |
|---|---|---|
| 앰프 이득 0x03 | 0x00 (11 dB, 최저) | 풀스케일 2.74 W로 제한 |
| 디지털 볼륨 0x05 | **0x50 (−40 dB) 이상**에서 시작, 0x3C(−30 dB)까지만 | 디지털 볼륨 리셋값이 0 dB이므로 반드시 먼저 씀 |
| 시험 신호 | 1 kHz 사인, −20 dBFS, 0.5~1 s | |
| 합산(풀스케일 대비) | −60 dB → 약 2.7 µW / −50 dB → 약 27 µW | 연속 허용 0.94 W의 1/10,000 이하 |
| 감지 | 꺼도 됨(0x02=0x0C) | 열 모델이 없으므로 쓸 곳이 없음 |
| Windows 드라이버 상한(열 모델 전) | 이득 0x14일 때 0x05 ≥ 0x28(−20 dB), 이득을 낮추면 그만큼 완화 가능 | Asahi 기준과 같음 |

참고로 m1n1 실험은 디지털 볼륨 0 dB·이득 0x00 상태에서 입력 자체를 `sox … gain -63`으로 −63 dB 낮춰 넣습니다(`M:speaker_amp.py#L11, L131-L132`). 스피커 감도를 모르므로 실제 소리 크기는 계산하지 못했습니다.

---

## 4. m1n1 쪽에서만 "삑" 소리 내기(실행하지 않음)

### 4.1 필요한 것

- 이미 있는 스크립트: `M:proxyclient/experiments/speaker_amp.py`(MIT). 순서는 전원 4개 켜기 → AUDIO_P 리셋 → I2C1 준비 → DART-sio 초기화(스트림 2) → ADMAC 채널 0 리셋·32비트·1워드 → NCO 0 = 48 kHz×256 → MCA 클러스터 0(SYNC 주기 LO=0xFE, SERDES 32비트 슬롯 1개, 마스크 0xFFFF_FFFE) → 포트 0 클럭·데이터 켜기 → DMA 어댑터 0x102048 → GPIO 181 리셋 해제 → 버퍼 제출·채널 시작 → SERDES 켜기 → TAS 설정 → 0x02←0x0C → 재생 → 0x0D → 0x0E.
- 실행 조건: 맥미니가 m1n1 **프록시 모드**(게스트 미실행)이고 개발용 맥이 USB로 붙어 있어야 합니다. 하이퍼바이저로 Windows가 돌고 있는 중에 같은 장치를 건드리면 소유권이 겹칩니다.
- 스크립트에 추가해야 할 것 [권장]:
  1. 0x02←0x0C 전에 **0x05 ← 0x50(−40 dB)** 쓰기(원 스크립트는 0x05를 안 씀 → 0 dB).
  2. 입력 대신 1 kHz 사인 0.5 s를 직접 만들어 넣기(24비트, −20 dBFS).
  3. GPIO 181은 읽은 값에서 비트0만 바꾸기.
  4. 끝나면 0x0E, SDZ=0, ADMAC 정지, AUDIO_P 리셋.
  5. `clk_init()`이 안 불렸을 수 있으므로 MCA 클럭 먹스(0x2_3B04_00D4) 확인·설정.

### 4.2 위험

| 위험 | 내용 | 줄이는 법 |
|---|---|---|
| 스피커 과열·손상 | 디지털 볼륨 리셋값 0 dB, 이득 최대 21 dB면 풀스케일 27 W(허용 0.94 W) | §3.2 값, 짧은 시간 |
| 잡음·팝 | 클럭 없이 동작 상태로 가면 큰 클릭 | 클럭 먼저, 음소거→동작 순서 |
| DMA 오류 | DART-sio 스트림 2 설정 잘못 → 주소 변환 오류. 스트림 0/1(SIO·AES)을 건드리면 다른 기능 영향 | 스트림 2만 다룸 |
| I2C 버스 멈춤 | 앰프 응답 없음·NACK | CTL FIFO 리셋, AUDIO_P/I2C1 전원 리셋으로 회복 |
| 잘못된 GPIO | 181이 아닌 핀을 바꾸면 다른 장치 영향 | ADT `function-reset`에서 다시 확인 |
| 손상 위험 판단 | [가설] 위 값이면 물리 손상 가능성은 매우 낮음. 다만 스피커 감도·실제 한계를 모르므로 "안전하다"고 단정하지는 않음 | |

**이 시험이 증명하는 것과 아닌 것:** 소리가 나면 "레지스터 정보와 경로가 맞다"는 것만 증명됩니다. Windows 드라이버가 동작한다는 뜻은 아닙니다.

---

## 5. 라이선스 메모

| 출처 | 라이선스 | Windows 드라이버에 쓰는 법 |
|---|---|---|
| `mca.c`, `macaudio.c`, `apple-admac.c`, `apple-dart.c`, `i2c-pasemi-*.c`, `pinctrl-apple-gpio.c` | GPL-2.0-only | **코드 복사 금지.** 이 문서처럼 레지스터 주소·비트 의미 같은 사실만 정리해 새로 작성 |
| `tas2770.c`/`.h` | GPL-2.0 | 같음 |
| `clk-apple-nco.c`, `pmgr-pwrstate.c`, `apple-sio.c` | GPL-2.0-only OR MIT | MIT 조건으로 쓸 수 있음(저작권 고지 유지) |
| Asahi DT(`t8103*.dtsi`, `t8103-j274.dts`) | GPL-2.0+ OR MIT | MIT 조건으로 사실 인용 가능 |
| m1n1(`speaker_amp.py`, `hw/*.py`, `src/clk.c`, `src/i2c.c`, `src/dart.c`) | MIT | 고지 유지 시 재사용 가능 |
| speakersafetyd | 2.0.2까지 MIT, 이후 GPL-2.0(`S:README.md#L64-L67`), `j274.conf`는 GPL-2.0-only 표기 | 열 모델 계수 같은 수치·식은 사실로 참고, 코드 복사 금지 |

---

## 6. 확인하지 못한 것

1. TAS5770L 데이터시트가 없어 TAS2770과 같은 배치인지 직접 확인하지 못했습니다(Asahi·m1n1이 그렇게 쓴다는 것만 확인).
2. m1n1 실험이 쓰는 레지스터 0x08←0x40의 의미.
3. `bop-config`, `speaker-config`, `speaker-protection`, `function-reset` 마지막 값의 정확한 뜻.
4. DART-sio의 우회 지원 여부(실기 0x2_3500_4004 비트0 읽기로 확인 가능).
5. Windows 진입 시점에 iBoot/m1n1이 남겨 둔 상태: SIO 펌웨어가 돌고 있는지(ADT `pre-loaded = 1`), ADMAC·DART-sio·MCA 클럭 먹스가 이미 설정됐는지.
6. ADT `function-i2s_route` 형식과 macOS가 클러스터 1을 쓰는 이유.
7. ADMAC 두 번째 reg(0x2_3D2A_8000), MCA 스위치 네 번째 reg(0x2_3850_0000)의 용도.
8. 앰프 이득 dB 값이 풀스케일 출력 전압(dBV) 기준인지, 스피커 감도(실제 소리 크기).
9. m1n1 `speaker_amp.py`가 실제 J274에서 검증된 이력(저장소에는 결과 기록이 없음). Asahi는 J274 스피커를 켜 두고 배포 중입니다(`enable_speakers = true`).

## 실기기 결과 (2026-11-16 20:09, S255)

- `nwoas_scripts/sound-s255/beep_tone.py`를 D4 프록시(하이퍼바이저 올리기 전)에서 실행. 증폭기 볼륨 0x50(−40 dB) 되읽기 일치, 이득 0x00, 440 Hz 0.7초 소리 3번(−20/−10/−4 dBFS), 끝난 뒤 증폭기 0x02 = 0xe(꺼짐).
- MCA 클럭 먹스는 이미 0x85100000~0x89100000(5+n)으로 설정돼 있었음(바꿀 필요 없었음).
- `/arm-io/mca0`, `/arm-io/mca1`에는 clock-gates가 없음(pmgr 오류 줄, 무해).
- [사실] 콘치님이 비프음 3번을 모두 들음(가장 작은 −60 dB 포함) → 내장 스피커 경로(클러스터 0, ADMAC 채널 0, DART-sio 스트림 2, TAS5770L 0x31) 동작 확인.

## 7. V/I 감지 경로와 열 모델 (2026-11-18, S259 — 읽기 조사, 실기기 미검증)

결론: 감지는 같은 I2S 포트 0의 반대 방향(앰프 SDOUT → MCA RX)입니다. Asahi는 캡처에 **RXB**와 ADMAC `rx{n}b` 채널을 씁니다. 클러스터 0이면 **ADMAC 채널 3**, DMA 어댑터 **B**(switch1 + 0x8000×n + 0x4000)입니다. speakersafetyd는 저항이 아니라 **V×I 전력**으로 2단 열 모델을 돌립니다.

| 사실 | 값 | 근거 |
|---|---|---|
| 캡처 직렬기 | RXB(클러스터+0x400). RXA(+0x200)는 `USE_RXB_FOR_CAPTURE`로 안 씀 | `L:sound/soc/apple/mca.c` 44, 79-83행 |
| RX 직렬기 레지스터 | +0x00 STATUS(EN, RST), +0x04 포트 마스크, +0x08 CONF, +0x0c BITSTART, +0x10/+0x14 슬롯 마스크 | `mca.c` 86-108행, `M:proxyclient/m1n1/hw/mca.py` `MCARXSerdesRegs`(MIT) |
| RX CONF | NCHANS=슬롯−1, 폭, SYNC_SEL=클러스터+1, UNK1·UNK2·NO_DATA_FEEDBACK(비트15) 켜고 UNK3(비트14) 끔 | `mca.c` `mca_configure_serdes` |
| RX 슬롯 마스크 | [0]=0xffffffff, [1]=~(쓰는 슬롯) | 같은 함수 |
| DMA 채널 이름 | 캡처 = `rx%db` → 채널 4n+3 | `mca.c` 1028-1035행, `L:t8103.dtsi` 1010-1015행 |
| DMA 어댑터 | 캡처는 어댑터 B. 값 = NCHANS(22:20)·TX/RX_NCHANS=2·패드 32−폭 | `mca.c` 677-699행(어댑터 B는 697행) |
| 감지 프런트엔드 | 16슬롯 × 16비트, rx_mask 0xffff, 2채널 S16 | `L:macaudio.c` 1055-1063행 |
| 앰프 감지 슬롯 | imon 0, vmon 2, `ti,sdout-zero-fill` | `L:t8103-j274.dts` 94-96행 |
| speakersafetyd 채널 | `is_chan = 0`, `vs_chan = 1`, 2채널 → 앰프 슬롯 번호는 8비트 단위 [가설: 슬롯 단위], I = 프레임 비트 0..15, V = 16..31 | `S:conf/apple/j274.conf` |
| 앰프 감지 레지스터 | 0x0f 비트6 V 송신 켬 + 슬롯, 0x10 비트6 I 송신 켬 + 슬롯, 0x0e 비트5 keeper·비트4 fill(0 = 빈 슬롯 0 출력)·비트3:1 오프셋·비트0 가장자리(Asahi 이름 `TX_EDGE_FALLING`) | `L:tas2770.h` 71-87행, `tas2770.c` 225-248, 501-571행 |
| 감지 켜기 | 0x02 비트3(I)·비트2(V)를 0으로. 바꿀 때는 종료 상태를 거쳐야 함 | `tas2770.c` 161-189행 주석 |
| 감지 전체 눈금 | I 3.75 A, V 14 V (s16 32768) | `S:conf/apple/j274.conf` |
| 열 모델 수치 | tr_coil 40, tr_magnet 60 °C/W, tau 3.7 / 250 s, t_limit 140, t_headroom 40, t_window 20, t_hysteresis 5, t_ambient 46 °C, z_nominal 4.6 Ω, a_t 0.0037 | `S:conf/apple/j274.conf` |
| 모델 식 | 표본마다 코일 목표 = 자석 + P·tr_coil, 자석 목표 = 주변 + P·tr_magnet, 1차 필터. 감쇠 = min_gain × (T − (t_limit − t_window)) / t_window, min_gain = 10·log10(0.94 W / (10^(이득/10)/4.6×2)) | `S:src/types.rs` `run_model`, `Speaker::new` |
| 첫 부팅 가정 | 코일 = t_limit − t_window − 1 = 119 °C, 자석 = 정적 분배 | `S:src/types.rs` `Speaker::new` |
| 저항 사용 | 이 판의 speakersafetyd는 a_t를 읽기만 하고 모델에 쓰지 않음 | `S:src/types.rs` 전체 |

NWOAS 선택과 이유(코드: `port-j274/sound/nwoas-audio/Source/Main/nwau_core.c`, 런처 `nwoas_scripts/sound-s256/audio_module.py` S1~S4):

| 항목 | NWOAS 값 | 이유 / 확실도 |
|---|---|---|
| 감지 클러스터 | 클러스터 0이 TX와 같은 SYNC로 RXB도 돌림(Asahi는 감지 전용 프런트엔드 클러스터 2를 포트 0에 붙임) | 프레임 256비트(256fs)가 16×16비트와 같음. [가설] 같은 클러스터 RX가 동작 |
| RXB CONF | 0x1b44f (16슬롯, 16비트, BCLK_POL=1, SYNC 1) | TX가 BCLK_POL=1로 동작 중이라 같은 극성 [가설] |
| BITSTART / 앰프 TX 오프셋 | 0 / 0 | 앰프 RX 오프셋 0(0x0b=0x00)과 맞춤 [가설] |
| 앰프 0x0e | 0x21 | keeper·zero fill(Asahi IDLE_ZERO), 오프셋 0, 가장자리 비트 1 [가설: 가장자리]. 잘못이면 V/I 값이 엉망 → 판정에서 걸러짐 |
| ADMAC RX | 16비트 워드, 2워드 프레임, burst 0x600030, SRAM 배정은 리셋값 그대로 | 리셋값 SRAM으로 RX가 도는지는 [가설], 런처가 값을 기록 |
| 어댑터 B | 0x205050 | Asahi 식(S16 2채널) |
| 모델 | 묶음(1024 프레임) 단위 정확한 1차 단계, 정수 연산 | 단위 시험: 표본 단위 식과 25분 동안 차이 0.002 °C 미만 |
| 기대 V | 보낸 디지털 신호 × 10^((11 + 0.5·이득 − 0.5·볼륨)/20) Vrms | [가설] 이득 dB가 풀스케일 사인 dBV. P1 시험에서 비율로 확인 |
