// TfliteApi.cpp — TFLite C API 동적 로더 구현.
#include "TfliteApi.h"

#include <cmath>
#include <cstdint>

// ── 최소 C API 타입 선언 ──────────────────────────────────────────────────────
//   전체 c_api.h 를 include 하면 일부 inline 함수가 라이브러리 심볼(TfLiteOperator* 등)에
//   대한 링크 참조를 만들어 동적 로드 취지와 충돌한다. 여기서는 불투명 포인터로만
//   다루므로 구조체 정의가 필요 없고, 어떤 TFLite 버전과도 ABI 안전하다(역참조 안 함).
extern "C" {
typedef struct TfLiteModel              TfLiteModel;
typedef struct TfLiteInterpreterOptions TfLiteInterpreterOptions;
typedef struct TfLiteInterpreter        TfLiteInterpreter;
typedef struct TfLiteTensor             TfLiteTensor;
typedef enum { kTfLiteOk = 0 }          TfLiteStatus;
// 양자화 파라미터(스칼라 affine): real = (q - zero_point) * scale.
//   실제 TFLite ABI 와 동일한 {float, int32} 레이아웃이라 값 반환이 ABI 안전하다.
typedef struct TfLiteQuantizationParams { float scale; int32_t zero_point; } TfLiteQuantizationParams;
}

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace vision {

namespace {

#if defined(_WIN32)
using LibHandle = HMODULE;
inline LibHandle openLib(const char *name) { return ::LoadLibraryA(name); }
inline void *symLib(LibHandle h, const char *name)
{
    return reinterpret_cast<void *>(::GetProcAddress(h, name));
}
inline void closeLib(LibHandle h) { if (h) ::FreeLibrary(h); }
const char *kDefaultLibName = "tensorflowlite_c.dll";
#else
using LibHandle = void *;
inline LibHandle openLib(const char *name) { return ::dlopen(name, RTLD_NOW | RTLD_LOCAL); }
inline void *symLib(LibHandle h, const char *name) { return ::dlsym(h, name); }
inline void closeLib(LibHandle h) { if (h) ::dlclose(h); }
const char *kDefaultLibName = "libtensorflowlite_c.so";
#endif

// 해석할 C API 함수 포인터 타입들.
using PFN_ModelCreate          = TfLiteModel *(*)(const void *, size_t);
using PFN_ModelDelete          = void (*)(TfLiteModel *);
using PFN_OptionsCreate        = TfLiteInterpreterOptions *(*)();
using PFN_OptionsSetNumThreads = void (*)(TfLiteInterpreterOptions *, int32_t);
using PFN_OptionsDelete        = void (*)(TfLiteInterpreterOptions *);
using PFN_InterpreterCreate    = TfLiteInterpreter *(*)(const TfLiteModel *, const TfLiteInterpreterOptions *);
using PFN_InterpreterDelete    = void (*)(TfLiteInterpreter *);
using PFN_AllocateTensors      = TfLiteStatus (*)(TfLiteInterpreter *);
using PFN_GetInputTensor       = TfLiteTensor *(*)(const TfLiteInterpreter *, int32_t);
using PFN_CopyFromBuffer       = TfLiteStatus (*)(TfLiteTensor *, const void *, size_t);
using PFN_Invoke               = TfLiteStatus (*)(TfLiteInterpreter *);
using PFN_GetOutputTensor      = const TfLiteTensor *(*)(const TfLiteInterpreter *, int32_t);
using PFN_CopyToBuffer         = TfLiteStatus (*)(const TfLiteTensor *, void *, size_t);
using PFN_TensorByteSize       = size_t (*)(const TfLiteTensor *);
using PFN_TensorType           = int (*)(const TfLiteTensor *);                       // TfLiteType
using PFN_TensorQuantParams    = TfLiteQuantizationParams (*)(const TfLiteTensor *);  // scale/zero_point

// TfLiteType enum 값(c_api_types.h). 여기선 필요한 것만 상수로 둔다(헤더 의존 회피).
constexpr int kTfLiteFloat32 = 1;
constexpr int kTfLiteUInt8   = 3;
constexpr int kTfLiteInt8    = 9;

// 텐서 타입별 원소 바이트 크기(원소 수 환산용).
inline int typeElemSize(int t)
{
    switch (t) {
        case kTfLiteInt8:
        case kTfLiteUInt8:   return 1;
        default:             return 4;   // float32 등
    }
}

} // namespace

struct TfliteApi::Impl
{
    LibHandle lib = nullptr;

    PFN_ModelCreate          modelCreate          = nullptr;
    PFN_ModelDelete          modelDelete          = nullptr;
    PFN_OptionsCreate        optionsCreate        = nullptr;
    PFN_OptionsSetNumThreads optionsSetNumThreads = nullptr;
    PFN_OptionsDelete        optionsDelete        = nullptr;
    PFN_InterpreterCreate    interpreterCreate    = nullptr;
    PFN_InterpreterDelete    interpreterDelete    = nullptr;
    PFN_AllocateTensors      allocateTensors      = nullptr;
    PFN_GetInputTensor       getInputTensor       = nullptr;
    PFN_CopyFromBuffer       copyFromBuffer       = nullptr;
    PFN_Invoke               invoke               = nullptr;
    PFN_GetOutputTensor      getOutputTensor      = nullptr;
    PFN_CopyToBuffer         copyToBuffer         = nullptr;
    PFN_TensorByteSize       tensorByteSize       = nullptr;
    PFN_TensorType           tensorType           = nullptr;
    PFN_TensorQuantParams    tensorQuantParams    = nullptr;

    TfLiteModel       *model       = nullptr;
    TfLiteInterpreter *interpreter = nullptr;

    std::vector<uint8_t> inBuf;    // int8/uint8 입력 양자화 임시 버퍼(재사용)
    std::vector<uint8_t> outBuf;   // int8/uint8 출력 원본 임시 버퍼(재사용)

    ~Impl()
    {
        if (interpreter && interpreterDelete) interpreterDelete(interpreter);
        if (model && modelDelete) modelDelete(model);
        closeLib(lib);
    }
};

TfliteApi::TfliteApi() : d(std::make_unique<Impl>()) {}
TfliteApi::~TfliteApi() = default;

bool TfliteApi::loadLibrary(const std::string &libraryName)
{
    const char *name = libraryName.empty() ? kDefaultLibName : libraryName.c_str();
    d->lib = openLib(name);
    if (!d->lib) {
        mError = std::string("Failed to load TFLite library: ") + name;
        return false;
    }

    struct Bind { void **slot; const char *name; };
    const Bind binds[] = {
        { reinterpret_cast<void **>(&d->modelCreate),          "TfLiteModelCreate" },
        { reinterpret_cast<void **>(&d->modelDelete),          "TfLiteModelDelete" },
        { reinterpret_cast<void **>(&d->optionsCreate),        "TfLiteInterpreterOptionsCreate" },
        { reinterpret_cast<void **>(&d->optionsSetNumThreads), "TfLiteInterpreterOptionsSetNumThreads" },
        { reinterpret_cast<void **>(&d->optionsDelete),        "TfLiteInterpreterOptionsDelete" },
        { reinterpret_cast<void **>(&d->interpreterCreate),    "TfLiteInterpreterCreate" },
        { reinterpret_cast<void **>(&d->interpreterDelete),    "TfLiteInterpreterDelete" },
        { reinterpret_cast<void **>(&d->allocateTensors),      "TfLiteInterpreterAllocateTensors" },
        { reinterpret_cast<void **>(&d->getInputTensor),       "TfLiteInterpreterGetInputTensor" },
        { reinterpret_cast<void **>(&d->copyFromBuffer),       "TfLiteTensorCopyFromBuffer" },
        { reinterpret_cast<void **>(&d->invoke),               "TfLiteInterpreterInvoke" },
        { reinterpret_cast<void **>(&d->getOutputTensor),      "TfLiteInterpreterGetOutputTensor" },
        { reinterpret_cast<void **>(&d->copyToBuffer),         "TfLiteTensorCopyToBuffer" },
        { reinterpret_cast<void **>(&d->tensorByteSize),       "TfLiteTensorByteSize" },
        { reinterpret_cast<void **>(&d->tensorType),           "TfLiteTensorType" },
        { reinterpret_cast<void **>(&d->tensorQuantParams),    "TfLiteTensorQuantizationParams" },
    };
    for (const Bind &b : binds) {
        *b.slot = symLib(d->lib, b.name);
        if (!*b.slot) {
            mError = std::string("Missing TFLite symbol: ") + b.name;
            return false;
        }
    }
    return true;
}

bool TfliteApi::initModel(const void *modelData, std::size_t modelSize, int numThreads)
{
    if (!d->lib || !d->modelCreate) {
        mError = "Library not loaded";
        return false;
    }
    d->model = d->modelCreate(modelData, modelSize);
    if (!d->model) {
        mError = "TfLiteModelCreate failed (invalid model buffer?)";
        return false;
    }

    TfLiteInterpreterOptions *opts = d->optionsCreate();
    if (!opts) {
        mError = "TfLiteInterpreterOptionsCreate failed";
        return false;
    }
    if (numThreads > 0)
        d->optionsSetNumThreads(opts, numThreads);

    d->interpreter = d->interpreterCreate(d->model, opts);
    d->optionsDelete(opts);
    if (!d->interpreter) {
        mError = "TfLiteInterpreterCreate failed";
        return false;
    }
    if (d->allocateTensors(d->interpreter) != kTfLiteOk) {
        mError = "TfLiteInterpreterAllocateTensors failed";
        return false;
    }
    return true;
}

bool TfliteApi::isReady() const
{
    return d->interpreter != nullptr;
}

int TfliteApi::inputElementCount() const
{
    if (!d->interpreter) return -1;
    const TfLiteTensor *t = d->getInputTensor(d->interpreter, 0);
    if (!t) return -1;
    return static_cast<int>(d->tensorByteSize(t) / typeElemSize(d->tensorType(t)));
}

int TfliteApi::outputElementCount() const
{
    if (!d->interpreter) return -1;
    const TfLiteTensor *t = d->getOutputTensor(d->interpreter, 0);
    if (!t) return -1;
    return static_cast<int>(d->tensorByteSize(t) / typeElemSize(d->tensorType(t)));
}

bool TfliteApi::invoke(const float *input, std::size_t inputCount, std::vector<float> &output)
{
    if (!d->interpreter) {
        mError = "Interpreter not initialized";
        return false;
    }
    TfLiteTensor *in = d->getInputTensor(d->interpreter, 0);
    if (!in) {
        mError = "No input tensor";
        return false;
    }

    // ── 입력 주입(타입별) ─────────────────────────────────────────────────
    //   float32 모델: float 버퍼를 그대로 복사.
    //   int8/uint8 모델(완전 정수 양자화): q = round(real/scale) + zero_point 로 양자화 후 복사.
    const int inType = d->tensorType(in);
    if (inType == kTfLiteFloat32) {
        if (d->copyFromBuffer(in, input, inputCount * sizeof(float)) != kTfLiteOk) {
            mError = "TfLiteTensorCopyFromBuffer failed (size mismatch?)";
            return false;
        }
    } else if (inType == kTfLiteInt8 || inType == kTfLiteUInt8) {
        const TfLiteQuantizationParams q = d->tensorQuantParams(in);
        const float scale = (q.scale != 0.0f) ? q.scale : 1.0f;
        d->inBuf.resize(inputCount);
        if (inType == kTfLiteInt8) {
            auto *dst = reinterpret_cast<int8_t *>(d->inBuf.data());
            for (std::size_t i = 0; i < inputCount; ++i) {
                long v = std::lround(input[i] / scale) + q.zero_point;
                v = (v < -128) ? -128 : (v > 127 ? 127 : v);
                dst[i] = static_cast<int8_t>(v);
            }
        } else {
            auto *dst = reinterpret_cast<uint8_t *>(d->inBuf.data());
            for (std::size_t i = 0; i < inputCount; ++i) {
                long v = std::lround(input[i] / scale) + q.zero_point;
                v = (v < 0) ? 0 : (v > 255 ? 255 : v);
                dst[i] = static_cast<uint8_t>(v);
            }
        }
        if (d->copyFromBuffer(in, d->inBuf.data(), inputCount) != kTfLiteOk) {
            mError = "TfLiteTensorCopyFromBuffer failed (quantized input)";
            return false;
        }
    } else {
        mError = "Unsupported input tensor type";
        return false;
    }

    if (d->invoke(d->interpreter) != kTfLiteOk) {
        mError = "TfLiteInterpreterInvoke failed";
        return false;
    }
    const TfLiteTensor *out = d->getOutputTensor(d->interpreter, 0);
    if (!out) {
        mError = "No output tensor";
        return false;
    }

    // ── 출력 읽기(타입별) ─────────────────────────────────────────────────
    //   int8/uint8 모델: real = (q - zero_point) * scale 로 역양자화해 float 확률로 복원.
    const int outType = d->tensorType(out);
    const std::size_t outBytes = d->tensorByteSize(out);
    if (outType == kTfLiteFloat32) {
        const std::size_t outCount = outBytes / sizeof(float);
        output.resize(outCount);
        if (d->copyToBuffer(out, output.data(), outCount * sizeof(float)) != kTfLiteOk) {
            mError = "TfLiteTensorCopyToBuffer failed";
            return false;
        }
    } else if (outType == kTfLiteInt8 || outType == kTfLiteUInt8) {
        const std::size_t outCount = outBytes;   // 1 byte/elem
        d->outBuf.resize(outCount);
        if (d->copyToBuffer(out, d->outBuf.data(), outCount) != kTfLiteOk) {
            mError = "TfLiteTensorCopyToBuffer failed (quantized output)";
            return false;
        }
        const TfLiteQuantizationParams q = d->tensorQuantParams(out);
        output.resize(outCount);
        if (outType == kTfLiteInt8) {
            const auto *src = reinterpret_cast<const int8_t *>(d->outBuf.data());
            for (std::size_t i = 0; i < outCount; ++i)
                output[i] = (static_cast<int>(src[i]) - q.zero_point) * q.scale;
        } else {
            const auto *src = reinterpret_cast<const uint8_t *>(d->outBuf.data());
            for (std::size_t i = 0; i < outCount; ++i)
                output[i] = (static_cast<int>(src[i]) - q.zero_point) * q.scale;
        }
    } else {
        mError = "Unsupported output tensor type";
        return false;
    }
    return true;
}

} // namespace vision
