// DiagPreprocess.h — t1/t3 윈도우 → 모델 입력(signal + features) 변환.
//   src/tinyml/inference_diag.py 의 make_signal / compute_features / 표준화를 C++ 로 1:1 포팅.
//   외부 라이브러리(OpenCV/FFTW) 의존 없음 — 길이 64 고정이라 직접 DFT 로 충분하다.
#ifndef DIAG_PREPROCESS_H
#define DIAG_PREPROCESS_H

#include <vector>

namespace diag {

// 윈도우 t1/t3(각 길이 n) → signal 텐서 버퍼(n*3, 채널순 [t1,t3,t1-t3] 정규화).
//   레이아웃: index = i*3 + c (TFLite (1,n,3) 메모리 순서와 동일).
void makeSignal(const double *t1, const double *t3, int n, std::vector<float> &signalOut);

// 윈도우 t1/t3(각 길이 n) → 표준화된 14차원 feature 버퍼.
//   computeFeatures() 후 (x - kFeatureMean) / kFeatureStd 적용(inference_diag.py 동일).
void computeFeaturesStandardized(const double *t1, const double *t3, int n,
                                 std::vector<float> &featOut);

} // namespace diag

#endif // DIAG_PREPROCESS_H
