# MCU_Signal — TI_MCU 기반 핀·신호 연결 도구

MSPM0G3507SPMR의 주소선 10개, TX_EN, RX_EN, DAC(LIGHT_SET) 총 13개 신호를 공통 JSON 정의에서 Altium용 넷 라벨 삽입 스크립트와 PSpice용 PWL 신호원으로 생성합니다. Python 표준 라이브러리만 사용합니다.

기준 FW: https://github.com/lovengirl/TI_MCU/tree/869e4cc84f9586cf94acd9b9e71ced40c42c6fc3

이 패키지는 독립적인 추가 도구입니다. 원본 TI_MCU FW·SysConfig·회로도는 수정하지 않습니다. Altium·PSpice에서의 실행 검증, MCU 라이팅, PCB 변경은 하지 않았습니다.

## 빠른 실행

이 폴더에서 실행합니다. Python 3.10 이상을 권장하며 이번 검증 환경은 Python 3.15입니다.

```powershell
python bridge.py
python -m unittest discover -s tests -v
```

별도 시나리오는 signals.json을 복사해 사용합니다.

```powershell
python bridge.py --config my_scenario.json --out generated_custom
```

`generated`는 생성 결과 폴더이므로 같은 경로로 실행하면 생성 파일을 덮어씁니다. Altium에서 이 폴더의 원본 스크립트를 직접 편집하기보다는 `templates/apply_signals.pas.in`을 수정하고 재생성하세요.

## 파일 구성

| 파일 | 용도 |
|---|---|
| signals.json | 핀·넷·기본 코드·타이밍·전기적 가정의 공통 정의 |
| bridge.py | 입력 검증, 이벤트와 PWL, Altium 스크립트 생성 |
| reference/ | 기준 커밋의 원본 FW 일부. SHA-256으로 변경 감지 |
| templates/apply_signals.pas.in | Altium DelphiScript 템플릿 |
| generated/pinmap.csv | 물리 핀·포트·FW 이름·회로 넷 대응표 |
| generated/events.csv | 변경된 신호의 논리값 기록. LIGHT_SET만 DAC 코드 단위 |
| generated/adc_windows.csv | ADC 트리거·획득 종료·결과 준비 시각 |
| generated/mcu_sources.lib | 13개 PWL 소스와 직렬 출력저항을 갖춘 서브회로 |
| generated/demo.cir | 출력 연결 확인용 예제 회로 |
| generated/SignalBridge.PrjScr | Altium 스크립트 프로젝트 |
| generated/apply_signals.pas | AuditSignals / ApplySignals 진입점 |
| generated/waveforms.json | 시간(ns)·전압(V)으로 변환한 PWL 점 |
| generated/definition.resolved.json | 해당 실행의 실제 설정 스냅샷 |
| generated/generation_report.json | 모델 가정·FW 버전·설정 해시·생성 요약 |
| VALIDATION.md | 수행한 검사와 수행하지 않은 검사 |

## 핀 대응

| 넷 | 포트 | 물리 핀 |
|---|---|---|
| TX_ADDR0..4 | PB0..4 | 47, 48, 50, 51, 52 |
| RX_ADDR0..4 | PB6..10 | 58, 59, 60, 61, 62 |
| TX_EN | PB5 | 53 |
| RX_EN | PB11 | 63 |
| LIGHT_SET | PA15 | 8 |

물리 핀과 PINCM 번호는 다릅니다. 초기 구현은 이 기준 핀 배치에 고정하며 다른 핀으로 JSON만 변경하면 거부합니다. 핀 재배치에는 실제 FW·SysConfig와 심볼/패키지를 함께 검토해야 합니다.

## Altium에 반영하기

1. 프로젝트 복사본에 전용 MCU 시트를 만듭니다.
2. MSPM0G3507SPMR LQFP64 심볼 하나만 놓습니다. 라이브러리 참조 이름에 `MSPM0G3507`이 포함되어야 합니다. 단일 파트에 대상 13핀 전부가 보여야 합니다.
3. 이 첫 구현은 아직 연결하지 않은 템플릿 시트용입니다. 기존 배선·버스·포트·전원 심볼·시트 심볼·정션이 있으면 중단합니다. 하네스나 다른 전기적 연결 객체도 없는 빈 템플릿을 사용하세요.
4. generated/SignalBridge.PrjScr을 열고, MCU SchDoc을 활성화한 뒤 MCU 부품 하나를 선택합니다.
5. Altium의 Run Script에서 `AuditSignals`를 실행합니다. 핀 번호·위치와 대상 넷을 표시하며 변경하지 않습니다. 실제 품번·패키지와 맞는지 확인합니다.
6. `ApplySignals`를 실행하면 13개 핀의 전기적 접속점에 넷 라벨을 삽입합니다. 동일 라벨은 다시 만들지 않습니다. 다른 라벨이나 중복 라벨이 있으면 변경 전에 중단합니다.
7. 회로도를 컴파일하고 ERC 및 내보낸 핀→넷 연결 결과를 확인한 뒤 저장합니다. 반복 실행 후 라벨 수가 증가하지 않는지도 확인하세요.
8. 외부 드라이버/MUX 시트는 같은 넷을 프로젝트의 넷 식별 범위 설정에 맞게 연결합니다. 계층 포트는 직접 맞춰야 합니다.

스크립트는 연결된 기존 회로의 넷을 일괄 재명명하거나 배선을 이동하지 않습니다. 회로도에 실제 넷 라벨을 만들지만 PSpice 전압원이나 동작 파형을 Altium에 넣지는 않습니다. 회로도 전원 핀·다른 주변장치 연결·PCB ECO·자동 배선도 범위 밖입니다.

Altium 런타임에서 컴파일/실행하지 않았습니다. 버전별 API 차이와 심볼 좌표/표현 차이는 실제 환경에서 확인해야 합니다. 삽입 중 런타임 오류가 나면 저장하지 말고 Undo 또는 복사본을 다시 여세요. 이 스크립트는 자동 저장하지 않습니다.

## PSpice에 연결하기

처음에는 generated/demo.cir을 PSpice의 회로 파일 실행 기능으로 실행해 파형을 확인합니다. GUI 메뉴와 실행 방식은 설치 버전에 맞추세요. Capture 회로도 파일(.dsn)은 제공하지 않습니다.

실제 회로에는 다음 라이브러리 포함과 인스턴스를 넣습니다. 아래 핀 순서를 지켜야 합니다.

```spice
.INC "mcu_sources.lib"
XMCU TX_ADDR0 TX_ADDR1 TX_ADDR2 TX_ADDR3 TX_ADDR4
+ RX_ADDR0 RX_ADDR1 RX_ADDR2 RX_ADDR3 RX_ADDR4
+ TX_EN RX_EN LIGHT_SET 0 TI_MCU_STIM
```

Capture에서는 이 서브회로에 맞춘 14단자 심볼과 PSpiceTemplate 설정이 필요합니다. VSS가 마지막 단자입니다. 위 인스턴스를 시뮬레이션 입력 파일에 넣는 방법으로도 사용할 수 있습니다. 라이브러리 경로는 실행 폴더를 기준으로 조정합니다. 기존 .END 앞에 포함해야 하며 전체 라이브러리에 별도의 .END를 추가하지 않습니다.

각 넷은 이상적인 PWL 소스에 직렬 출력저항을 붙여 구동합니다. 내부 NDRV_* 노드에 부하를 연결하지 말고 외부 넷 단자에 MUX/드라이버 회로를 연결합니다. 같은 노드에 또 다른 출력 드라이버를 동시에 연결하면 안 됩니다. 후속 IBIS 모델을 쓸 때는 해당 신호의 PWL+저항 구성을 교체하거나 모델 입력측으로 옮겨야 합니다.

demo.cir의 1GΩ/5pF 부하는 연결 확인용입니다. 실제 회로·PCB 기생성분·ADC 모델을 대체하지 않습니다. .TRAN의 최대 간격 1µs는 기본 동작 확인용이며 빠른 에지·SI 분석용으로 검증한 설정이 아닙니다. 실제 관심 대역과 수렴 검증에 맞춰 조정하세요.

## FW 동작의 재현 범위

센서 상태 머신의 정상 완료 경로를 Python으로 재현합니다. 실제 FW 바이너리를 실행하거나 CCS 주변장치를 에뮬레이션하지 않습니다.

채널 0~19마다:

1. Tx 차단 → Rx 차단 → Tx/Rx 주소를 같은 값으로 변경.
2. 채널 DAC 코드 변경 → Rx 활성화.
3. 50µs 대기 후 OFF 4회 취득, 10µs 간격.
4. Tx 활성화 → 50µs 대기 → ON 4회 취득.
5. DMA 완료에 해당하는 시점에 Tx 차단.
6. 다음 채널로 진행. 마지막에는 Rx 차단·DAC 0.

최초 트리거는 샘플 타이머 시작 후 한 주기가 지난 시점으로 가정합니다. 각 ADC 획득 구간은 트리거부터 2µs입니다. adc_windows.csv는 회로 파형을 후처리할 때 사용할 시각 정보이며 ADC 입력 회로나 샘플앤드홀드 자체를 구현하지 않습니다.

기본값에는 100µs 시작 지연, GPIO 호출 간 100ns 간격, ADC 변환 500ns, 프레임 간 10µs 대기를 가정합니다. 인터럽트·메인 지연 기본값은 0입니다. 이 가정까지 포함한 기본 생성 종료시각은 3.8222ms이며 실제 FW 프레임 주기가 아닙니다.

기본 전기적 가정은 3.3V, 디지털 에지 5ns, 출력저항 33Ω, DAC 전환 1µs·출력저항 100Ω입니다. TI 데이터시트 보장값이나 실측값이 아닙니다. DAC 목표 전압은 code×VREF/4096으로 변환합니다. 코드 512는 이상적으로 0.4125V입니다. DAC 글리치·비선형·실제 정착·MCU 패키지·전원 전류·동시 스위칭 효과는 포함하지 않습니다.

따라서 이 첫 버전으로 절대 방사량(RE)을 예측했다고 판단하면 안 됩니다. 전기 모델·PCB/전원/케이블 모델과 검증이 추가되어야 합니다.

## 설정 변경

- `dac_codes`: 채널 0~19의 코드. 0~4095.
- `scenario.frames`: 1~100 프레임의 유한 파형.
- `timing_ns.select/light/sample_period`: 분석할 타이밍. 현재 UART에서는 이 값 변경을 지원하지 않습니다.
- `timing_ns.*_assumed`: 실제 타이밍 확보 전의 시나리오 가정.
- `electrical`: 실측/적합한 모델에 맞춰 바꿀 전기 특성.

이 도구에서 값을 바꿔도 FW는 바뀌지 않습니다. DAC 코드·타이밍을 FW에 적용하려면 지원되는 UART DAC 명령 또는 별도의 FW 수정이 필요합니다. 연속 프레임은 고정한 분석 시나리오이며 UART 지터·설정 변경·STOP/ESTOP·오류 경로는 아직 재현하지 않습니다.

FW가 바뀌면 기준 소스와 해시만 무조건 갱신하지 말고 상태 머신과 타이머 설정을 재검토해야 합니다. 생성 보고서의 FW 커밋과 설정 해시로 결과를 구분하세요.

## 기존 TI_MCU 저장소에 추가

이 폴더를 TI_MCU 저장소의 `tools/signal_bridge`로 복사하면 독립적으로 사용할 수 있습니다. 현재 CCS 프로젝트나 FW 파일은 수정할 필요가 없습니다. 이 저장소는 TI_MCU에서 파생한 신호 연결 도구를 별도로 관리합니다. 전체 CCS 프로젝트는 원본 TI_MCU 저장소에 있습니다.

## 참고

- Altium 스크립팅: https://www.altium.com/documentation/altium-designer/scripting
- Altium Schematic API: https://www.altium.com/documentation/altium-dxp-developer/schematic-api-design-objects-interfaces-reference
- Cadence PWL 안내: https://community.cadence.com/cadence_technology_forums/pcb-design/f/pspice/57573/how-do-you-use-piece-wise-linear-pwl-voltage-and-current-sources-for-simulations

## 검증 결과 요약

2026-11-01, Python 3.15에서 변환기 실행 및 7개 테스트가 통과했습니다.

- FW 스냅샷 무결성과 변경 감지
- 주소 전환 전 Tx/Rx 차단 및 20채널 순서
- OFF/ON ADC 160회와 샘플 간격
- 다중 프레임 및 종료 시 안전 상태
- DAC 전압 환산과 PWL 시간 순서
- 잘못된 설정 거부
- PSpice 소스·Altium 스크립트 생성 구조

[상세 검증 기록](VALIDATION.md) · [테스트 실행 로그](test_results.txt)

Altium 및 PSpice 내부 컴파일·실행, 실제 MCU 타이밍과 전기 특성 검증은 아직 수행하지 않았습니다. 이 결과는 SI/RE 해석 정확도 검증을 의미하지 않습니다.
