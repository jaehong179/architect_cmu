# TimeGrapher (architect_cmu)

기계식 시계의 똑딱 소리를 마이크로 듣고 **진폭·보율(rate)·비트 오차**를 실시간으로
측정·그래프로 보여 주는 Qt6/C++ 앱. Windows / Raspberry Pi(Linux) / macOS 지원.

## 📖 문서

**→ [docs/README.md](docs/README.md) 에서 시작하세요 (문서 허브)**

| 문서 | 내용 |
|---|---|
| [아키텍처](docs/ARCHITECTURE.md) | 재구조화 전/후 · Context/Module/C&C 뷰 · 패턴·전술 |
| [신호 흐름](docs/SIGNAL_FLOW.md) | 소리 → 그래프 단계별 그림·시퀀스 |
| [성능 측정](docs/PERFORMANCE.md) | 지표 카탈로그 · 측정/분석 방법 |

## 🔧 빌드 (Ubuntu/Pi)

```bash
sudo apt update
sudo apt install libglu1-mesa-dev libx11-xcb-dev build-essential cmake libasound2-dev

cmake -S . -B build -G Ninja
cmake --build build --target TimeGrapher
```

Windows(MinGW)·macOS 빌드 및 폴더 구조는 [docs/README.md](docs/README.md) 참고.
