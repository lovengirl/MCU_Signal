# 검증 기록

검증일: 2026-11-01 (Asia/Seoul)

기준 FW 커밋: 869e4cc84f9586cf94acd9b9e71ced40c42c6fc3

Python 3.15에서 `python bridge.py` 및 `python -m unittest discover -s tests -v` 실행 성공. 7개 테스트 통과.

| 검사 | 결과 |
|---|---|
| 원본 FW 스냅샷 해시와 변경 감지 | 통과 |
| 주소 변경 전 TX_EN/RX_EN 차단 및 Tx/Rx 채널 0~19 일치 | 통과 |
| 160개 ADC 취득 시점, 10µs 간격, OFF/ON별 Tx 상태 | 통과 |
| 2프레임 재생과 마지막 Tx/Rx/DAC 차단 | 통과 |
| 채널별 DAC 전압 환산과 PWL 시간 단조 증가 | 통과 |
| 잘못된 핀·DAC 범위·시간·전기 특성 거부 | 통과 |
| PSpice 13개 소스·출력저항 및 Altium 템플릿 생성 | 통과 |

이 검사는 변환기의 논리와 생성 구조를 검증합니다. 실제 FW 바이너리의 시간 정밀 재현, PSpice 파서의 입력 승인, Altium DelphiScript의 컴파일과 실제 넷 연결은 검증하지 않았습니다.

실제 환경에서 남은 확인:

1. Altium 복사본에서 AuditSignals/ApplySignals 실행, 13개 물리 핀의 연결 결과를 컴파일한 넷리스트로 대조.
2. 스크립트 반복 실행 시 중복 라벨이 생성되지 않는지 확인.
3. PSpice에서 demo.cir 및 실제 부하 회로 실행. 핀 파형과 사건 기록 비교.
4. 실제 보드에서 에지·출력 임피던스·DAC 응답·타이밍 지터 측정 또는 적합한 모델 확보.
5. 측정 조건을 반영한 SI/전자기 모델 수렴과 실측 비교.
