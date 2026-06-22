// VisionPreprocess.h — inference_webcam.py 전처리 파이프라인의 C++ 구현.
//   단계: center-crop → bilinear resize(kImgSize²) → CLAHE(LAB L채널) → BGR/RGB 정규화.
//   OpenCV 비의존: 동일 알고리즘/계수를 직접 구현(요구사항: OpenCV 회피).
#ifndef VISION_PREPROCESS_H
#define VISION_PREPROCESS_H

#include <cstdint>
#include <vector>

namespace vision {

// 입력: RGB888 프레임(rgb, width×height, 행 stride 바이트).
// 출력: 모델 입력 텐서(NHWC, float [0,1], kImgSize*kImgSize*3) → out.
//   crop 좌표는 kCamWidth/kCamHeight 기준이며, 실제 프레임 크기에 비례 스케일된다.
void preprocessFrame(const std::uint8_t *rgb, int width, int height, int stride,
                     std::vector<float> &out);

} // namespace vision

#endif // VISION_PREPROCESS_H
