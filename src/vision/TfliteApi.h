// TfliteApi.h — TensorFlow Lite C API 동적 로더 (런타임 LoadLibrary/dlopen).
//
//   왜 동적 로드인가:
//     - TFLite C API 는 순수 C ABI 라 빌드 컴파일러(MSVC)와 소비 컴파일러(MinGW)가 달라도
//       LoadLibrary/GetProcAddress 로 안전하게 호출된다(정적 링크 시의 ABI 불일치 회피).
//     - Windows: 다운로드한 prebuilt tensorflowlite_c.dll
//       Linux/RPi5: 소스 빌드한 libtensorflowlite_c.so
//   PIMPL 로 C API 헤더를 이 헤더에서 숨긴다(소비자는 C API 헤더 불필요).
#ifndef VISION_TFLITE_API_H
#define VISION_TFLITE_API_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace vision {

class TfliteApi
{
public:
    TfliteApi();
    ~TfliteApi();

    TfliteApi(const TfliteApi &) = delete;
    TfliteApi &operator=(const TfliteApi &) = delete;

    // 공유 라이브러리를 로드하고 필요한 심볼을 해석한다. 실패 시 false + lastError().
    //   libraryName 이 비면 플랫폼 기본 이름(tensorflowlite_c.dll / libtensorflowlite_c.so).
    bool loadLibrary(const std::string &libraryName = std::string());

    // 모델 버퍼(.tflite 바이트)로 인터프리터를 생성/할당한다. 버퍼는 호출자가 보관해야 한다.
    bool initModel(const void *modelData, std::size_t modelSize, int numThreads);

    // 입력(NHWC float, inputCount 개)을 넣고 추론 → output 채움. 성공 시 true.
    bool invoke(const float *input, std::size_t inputCount, std::vector<float> &output);

    bool isReady() const;
    int  inputElementCount() const;   // 모델 입력 텐서 원소 수(검증용), 실패 시 -1
    int  outputElementCount() const;  // 모델 출력 텐서 원소 수, 실패 시 -1

    const std::string &lastError() const { return mError; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    std::string           mError;
};

} // namespace vision

#endif // VISION_TFLITE_API_H
