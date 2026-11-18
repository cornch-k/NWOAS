# 2772(P36) 결과 — Edge 메모리 부족·GPU 프로세스 재시작, K35 화면 표시 실패, Edge 기록 파일

작성 2026-11-19 03:5x. 2772는 **빌드만 됐고 실기 설치·시험은 아직 안 했습니다** [빌드만]. 맥미니는 건드리지 않았습니다(와이파이 시험 중).

## 결론

| 문제 | 결론 | 구분 |
|---|---|---|
| Edge의 ANGLE `GL_OUT_OF_MEMORY`와 `--gpu-recent-crash-count=1` | 같은 원인 하나입니다. ANGLE이 4x 렌더 대상을 표준 표본 패턴(Quality 0xffffffff)으로 만들고, 우리 D3D11 UMD가 Quality 0만 받아 거부 → 메모리 부족으로 바뀌어 ANGLE에 전달 → WebGL 문맥 손실 → Chromium이 GPU 프로세스를 스스로 끝내고 다시 띄움. 2772에서 이 값을 받게 고쳤습니다 | 원인 [사실], 고친 효과 [빌드만] |
| K35 스왑체인 표시 8개 실패(0x887a0001) | 시험 프로그램 잘못입니다. 첫 표시를 "일부 영역만 바뀜" 표시(Present1+더러운 영역)로 했는데, DXGI가 이것을 어느 그래픽 장치에서나 거부합니다. 우리 드라이버 문제가 아닙니다. 고친 시험 프로그램(nwgpuprobe9)은 VM에서 실패 0입니다 | [사실, VM 실험] |
| Edge GPU 프로세스 안에서 UMD 기록 파일이 안 생김 | GPU 샌드박스 때문입니다. 샌드박스를 끄면(`--disable-gpu-sandbox`) 같은 환경 변수 기록이 생깁니다. 샌드박스를 켠 채 기록하는 방법(낮은 무결성 폴더)은 K38s로 시험합니다 | 샌드박스가 막음 [사실], 낮은 무결성이 원인 [가설] |

## 1. Edge 메모리 부족과 GPU 프로세스 재시작 — 원인은 표준 표본 패턴 거부

결론: **2771 K38t 기록에 거부된 호출 하나가 그대로 남아 있고, 공개 소스로 Edge 로그 줄까지 이어집니다.**

### 1.1 거부된 호출(2771 K38t, 03:25:13) [사실]

`reject-d3d11-render-target hr=0x80004001 ... 0000001c 00000028 00000000 00000001 00000001 ffffff04 00000000 00000003`

| 값 | 뜻 |
|---|---|
| 0x1c | 형식 R8G8B8A8_UNORM |
| 0x28 | 렌더 대상(0x20) + 셰이더 읽기(0x8) (d3d10umddi.h 값 확인) |
| 0 / 1 / 1 | 기타 플래그 없음, 단계 1, 배열 1 |
| ffffff04 | 표본 4개, Quality 0xffffffff(= `D3D11_STANDARD_MULTISAMPLE_PATTERN`) |
| 0 / 3 | DEFAULT 사용, 2D 텍스처 |

바로 뒤에 `reject-seterror fn=CreateResource line=892 hr=0x80004001`, `reject-create-error-as-oom`(P32가 E_NOTIMPL을 E_OUTOFMEMORY로 바꿈)이 이어집니다.

### 1.2 사슬

| 단계 | 근거 | 구분 |
|---|---|---|
| ANGLE이 4x면 표준 패턴을 요청 | ANGLE `Renderer11::getSampleDescQuality`: 표본 수 > 0이고 FL 10_1 이상이면 `D3D11_STANDARD_MULTISAMPLE_PATTERN` | [사실, 공개 소스] |
| 4x를 고른 이유 | 2771부터 RGBA8 등에 2x·4x를 알림(P35). WebGL 페이지(`webgl.html`)는 기본값 antialias로 문맥을 만듦 | [사실] |
| 우리 UMD 렌더 대상 조건이 `Quality == 0`만 받음 | `build-native-asahi-state.py` 렌더 대상·깊이 조건 | [사실, 코드] |
| ANGLE 오류 위치 | `ResourceManager11::allocate`의 `CreateResource` → `ANGLE_TRY_HR`, 로그 `allocate:492 ... HRESULT: 0x8007000E` | [사실] |
| GPU 프로세스 종료 | Chromium `GpuChannelManager::OnContextLost`: ANGLE(패스스루 디코더)이면 `force_restart` → `MaybeExitOnContextLost` | [사실, 공개 소스] |
| 시각 | 같은 run의 세 시험에서 OOM과 새 GPU 프로세스 사이 간격이 짧고 충돌 보고 0 | [사실] |

| 시험(2771) | 첫 GPU 프로세스 OOM 시각 | 새 GPU 프로세스 첫 기록 | 간격(ms) | 충돌 보고(개) | FATAL(개) |
|---|---|---|---|---|---|
| K38 기본 | 03:23:03.042 | 03:23:03.209 | 167 | 0 | 0 |
| K38t 기본 + 샌드박스 끔 | 03:25:13.041 | 03:25:13.194 | 153 | 0 | 0 |
| K38d `--use-angle=d3d11on12` | 03:26:29.319 | 03:26:29.834 | 515 | 0 | 0 |

### 1.3 고친 것(P36, 2772) [빌드만]

| 곳 | 2771 | 2772 |
|---|---|---|
| D3D11 렌더 대상 | Quality 0만 | 0 또는 0xffffffff(표준 패턴) |
| D3D11 깊이·스텐실 | Quality 0만 | 0 또는 0xffffffff |
| D3D12 2D 텍스처(`TextureBytes`) | Quality 0만 | 0 또는 0xffffffff |
| 중앙 패턴 0xfffffffe | 거부 | 거부(그대로) |
| 형식 답·품질 단계 질의 | P35 | 그대로 |

- 표준 패턴을 품질 0으로 받아도 되는 근거: Asahi Gallium 드라이버는 표본 위치로 `u_default_get_sample_position`(D3D·Vulkan 표준 위치)을 씁니다(Mesa 9aa1215f `agx_pipe.c:1812`) [사실, 소스]. 실제 GPU가 그 위치로 찍는지는 실기로 본 적이 없습니다 [가설].
- D3D11On12 경로(K38d)의 첫 GPU 프로세스 OOM도 같은 원인일 것으로 봅니다 [가설]. 그 실행에서는 D3D12 할당 기록이 걸러져 보이지 않았습니다. K38dg가 `d3d12_alloc_fail` 개수를 셉니다.
- 2771 K38t의 두 번째 GPU 프로세스에는 이 거부가 없었습니다. 페이지가 문맥을 다시 만들지 않은 것으로 보이지만 확인하지 않았습니다 [가설].

### 1.4 빌드 [빌드만]

| 항목 | 값 |
|---|---|
| 스크립트 | `build-p36.sh`(Parallels VM, 실행 이름 j274g4l, 5단계 모두 rc=0) |
| 판 | 30.0.2772.0 |
| 결과물 | `out/j274-full-20261119-0346-p36` |
| 차이 | `P36-driver.diff`(2개 파일, 52줄) |
| 생성 소스 확인 | VM `asahi-native-arm64-j274g4l\...\d3d10umd\Resource.cpp`에 P36 줄 3개 |
| InfVerif | ERROR(1322) 1개(UMD를 System32로 복사) — 2771과 같음, 2771은 pnputil 설치됨 |
| signtool verify | 오류 1(시험 서명) — 2771과 같음 |

| 파일 | 크기(바이트) | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 651,496 | `cac0dabb81b712f5e375bb4ed22118244a529c58904858be01df3e1c04f059f5` |
| AppleAgxRenderAdmissionUmd.dll | 13,522,432 | `8647b5df6627d2b9b2f795e043ed4a308576fb65d67d45836a4505c48b96b496` |
| AppleAgxRenderAdmission.arm64only.inf | 3,613 | `bf89c9c42503987842b79a8a30d2daca1c0dabf216e890b44c68f4817638e96d` |
| appleagxrenderadmission.cat | 3,254 | `15144633a9b74b79c62e79bb9a0a0577fb46c8042b0b86dd209a3d0d35f029d9` |
| nwgpuprobe9-arm64.exe | 253,440 | `a9c36591237230433d9b6975769134a3908a304461c26309ebf95a536978551d` |
| edgelog3.ps1 | 4,992 | `9e6fc002757ca7a04d21e43440841767721e20b31d1287957f3acf8f27eb448a` |

## 2. K35 스왑체인 표시 실패 — 시험 프로그램 잘못

결론: **nwgpuprobe8은 새 스왑체인의 첫 표시를 Present1+더러운 영역으로 했고, DXGI는 그것을 어느 장치에서나 0x887a0001로 거부합니다. 2771 실기의 실패 8개는 드라이버 결과가 아닙니다.**

Parallels VM 사용자 세션(사용자 작업으로 실행, K35와 같은 방식)에서 잰 값입니다 [사실].

| 시험 프로그램 | 장치 | 통과(개) | 실패(개) | 비고 |
|---|---|---|---|---|
| nwgpuprobe8 | VM 자체 그래픽 드라이버 | 세지 않음 | 8 | 실기와 같은 8개, 같은 0x887a0001 |
| nwgpuprobe8 | WARP | 세지 않음 | 9 | 실기와 같음(출력 없음 1개 포함) |
| nwgpuprobe9 | VM 자체 그래픽 드라이버 | 154 | 0 | |
| nwgpuprobe9 | WARP | 150 | 0 | |

| 호출 | VM 드라이버 | WARP | 판단 |
|---|---|---|---|
| 첫 표시를 Present1+더러운 영역으로(v8 방식) | 0x887a0001(8/8) | 0x887a0001(8/8) | DXGI 규칙 — 이제 참고 줄 |
| 전체 화면 Present 3번 | 통과(8/8) | 통과(8/8) | 셈에 넣음 |
| 그 뒤 Present1+더러운 영역, FLIP_SEQUENTIAL·SEQUENTIAL | 통과(3/3) | 통과(3/3) | 셈에 넣음 |
| 그 뒤 Present1+더러운 영역, FLIP_DISCARD | 0x887a0001(4/4) | 0x887a0001(4/4) | DXGI 규칙 — 참고 줄 |
| WARP 출력 열거 | 해당 없음 | 출력 없음 | WARP는 원래 화면 출력이 없음 — 참고 줄 |

고친 것: `nwoas_scripts/gpucov/probe/nwgpuprobe_chromium.inc`(Swap, Outputs), 빌드 이름 nwgpuprobe9(`build-probe.cmd`), 작업 `K35g-probe9-chromium.cmd`.

## 3. Edge GPU 프로세스 기록 파일 — 샌드박스가 막음

결론: **샌드박스를 끄면 기록이 생기고, 켜면 안 생깁니다.** 2770 K36b의 "흔적 폴더"가 비어 있던 것도 같은 이유로 봅니다.

| 시험 | GPU 샌드박스 | 기록 방식 | 기록 줄 수 |
|---|---|---|---|
| 2770 K37 | 켬 | 환경 변수 → `C:\NWOAS\gpucov\edge-umd.log` | 파일 없음 |
| 2770 K36b | 켬 | 레지스트리 + 흔적 폴더 `C:\NWOAS\umdtrace-seen` | 흔적 0 |
| 2771 K38t | 끔 | 환경 변수 | 6,705 |
| 2771 K38d | 끔 | 환경 변수 | 4,320 |

- 2771 K38t·K38d의 `SEEN files=0`은 고장이 아닙니다. 환경 변수 기록이 켜지면 레지스트리 확인 코드가 돌지 않습니다(`umd_runtime_device.c` `AdmissionUmdTraceConfig`) [사실, 코드].
- 막는 구조 [가설]: Chromium 윈도우즈 GPU 샌드박스는 낮은 무결성 수준과 제한 토큰으로 돕니다. 보통 무결성의 `C:\NWOAS` 아래에는 쓸 수 없습니다.
- 샌드박스를 켠 채 쓰는 방법: K38s가 `C:\NWOAS\umdtrace-low`를 만들고 낮은 무결성 표시와 Everyone·RESTRICTED·ALL APPLICATION PACKAGES 쓰기 권한을 준 뒤, 그 안으로 환경 변수 기록을 보냅니다. VM에서 폴더 준비와 Edge 실행 순서가 도는 것까지 확인했습니다(VM에는 우리 UMD가 없어 기록 파일 자체는 실기에서만 확인 가능).
- 이것도 막히면 다음 후보는 ETW(이벤트 추적)입니다. 진단만 할 때는 `--disable-gpu-sandbox`가 이미 됩니다(평소 사용에는 쓰지 않음).

## 4. 실기 명령(맥미니가 비었을 때)

와이파이 시험이 끝난 뒤에만 돌립니다. 지금 세션의 `.out`·`.link`는 가장 최근 `nwoas_scripts/logs/launcher-*.out`과 그 안의 `Log:` 줄(+`.link`)입니다.

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
# 설치(I34) → 시험 키트 전달(nwgpuprobe9, edgelog3) → D17b → 재시작(D28) → 시험 키트
bash agx-s248/run-2772-chain.sh <지금 launcher .out> <지금 .link>
# 맥미니가 이미 프록시 화면에 있을 때(설치만)
LAUNCHER=agxdcp-s259-guest-test.sh bash agx-s248/run-install-2772-norestart.sh
# 설치된 세션에서 키트만 다시
KIT="K40 K41 K25b K21 K35g K38g K38tg K38dg K38s K41" LAUNCHER=agxdcp-s259-guest-test.sh bash gpucov/run-gpucov.sh run <link>
```

되돌리기: 화면이 반복해서 멈추면 2771 설치 스크립트(`run-install-2771.sh`)로 돌아갑니다. MSAA만 끄려면 D60(`D3d11Msaa`=0).

### 통과 기준

| 작업 | 통과 기준 | 2771 값 |
|---|---|---|
| I34 | `PNPUTIL EXIT 0`, `DEV1 OK`, `VC ... 30.0.2772.0` | 2771 같은 형식 |
| K41(앞·뒤) | `dwm_events=0` | 0 |
| K25b | driver pass=17 fail=0 | 17/0 |
| K21 | Cinebench 화면 내용 있음 | 있음 |
| K35g | driver fail=0, warp fail=0 | (v8) 8, 9 |
| K38g | `fatals=0`, `gpu_log_pids=1`, `max_recent_crash=0`, `oom_lines=0` | 0, 2, 1, 1 |
| K38tg | `rt_refusals=0`, `create_as_oom=0`, `gpu_log_pids=1`, `oom_lines=0` | 1, 1, 2, 1 |
| K38dg | `d3d12_alloc_fail=0`, `oom_lines=0`, `gpu_log_pids=1` | 그때는 세지 않음, 1, 2 |
| K38s | `ENVTRACE lines>0`이고 `pids=`에 GPU 프로세스 번호 | 해당 없음(새 시험) |

## 5. 아침에 화면에서 볼 것(콘치님)

설치된 판이 2771이면 판 번호만 2771로 읽으시면 됩니다. Edge 버전에 따라 항목 이름이 조금 다를 수 있습니다.

1. Edge를 바탕화면 아이콘으로 그냥 엽니다. `:(` 화면 없이 새 탭이 그려지는지, 깜박이며 한 번 사라졌다 다시 그려지는지 봅니다.
2. 주소창에 `edge://gpu`를 엽니다.
   - 맨 위 "Graphics Feature Status": Canvas, Compositing, WebGL, WebGL2가 "Hardware accelerated"인지.
   - GPU 장치 줄(GPU0)과 "GL_RENDERER" 줄에 Apple AGX(우리 드라이버 이름)와 판 30.0.2772.0(또는 2771)이 있는지. "Microsoft Basic Render Driver"나 SwiftShader가 보이면 우리 드라이버를 안 쓰는 것입니다.
   - "Display type"이 ANGLE_D3D11인지.
   - "Problems Detected"에 적힌 줄.
   - 맨 아래 "Log Messages"에 `GL_OUT_OF_MEMORY`, "GPU process" 관련 줄이 있는지.
3. 주소창에 `file:///C:/NWOAS/gpucov/webgl.html`을 엽니다. 10초쯤 뒤 초록 글자에 `NWGL webgl_draw=PASS`, `NWGL done=1`, `fps=`가 나오는지 봅니다. 2771에서는 이 페이지에서 GPU 프로세스가 한 번 다시 떴습니다 [사실]. 2772에서는 다시 뜨지 않기를 기대합니다 [가설].

## 6. 바꾼 파일

| 파일 | 내용 |
|---|---|
| `drivers/apple-agx/mesa/scripts/build-native-asahi-state.py` | 렌더 대상·깊이 조건에 표준 패턴 |
| `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp` | `TextureBytes`에 표준 패턴 |
| `build-p36.sh`, `P36-driver.diff`, `PATCHES.md` P36 장 | 빌드·기록 |
| `nwoas_scripts/gpucov/probe/nwgpuprobe_chromium.inc`, `build-probe.cmd`, `bin/nwgpuprobe9-*.exe` | 시험 프로그램 v3 |
| `nwoas_scripts/gpucov/ps/edgelog3.ps1` | 로그 기준 GPU 프로세스 수(`gpu_log_pids`), 최대 재시작 횟수, OOM·문맥 손실 줄, 기록 경로 선택. VM에서 2771 로그 조각으로 2/1/1이 나오는 것 확인 |
| `nwoas_scripts/gpucov/jobs/K35g-*.cmd`, `K38g-*`, `K38tg-*`, `K38dg-*`, `K38s-*` | 새 작업 |
| `nwoas_scripts/file-delivery-s243/manifests/agx-j274-2772.json`, `jobs/agx-j274-2772-pkg.cmd`, `jobs/gpucov12-j274-20261119.cmd` | 전달 |
| `nwoas_scripts/agx-s248/jobs/I34-install-2772.cmd`, `run-install-2772.sh`, `run-install-2772-norestart.sh`, `run-2772-chain.sh` | 설치·연쇄 실행 |
