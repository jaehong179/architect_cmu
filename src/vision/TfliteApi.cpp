// TfliteApi.cpp — TFLite C API 동적 로더 구현.
#include "TfliteApi.h"

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

    TfLiteModel       *model       = nullptr;
    TfLiteInterpreter *interpreter = nullptr;

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
    return static_cast<int>(d->tensorByteSize(t) / sizeof(float));
}

int TfliteApi::outputElementCount() const
{
    if (!d->interpreter) return -1;
    const TfLiteTensor *t = d->getOutputTensor(d->interpreter, 0);
    if (!t) return -1;
    return static_cast<int>(d->tensorByteSize(t) / sizeof(float));
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
    if (d->copyFromBuffer(in, input, inputCount * sizeof(float)) != kTfLiteOk) {
        mError = "TfLiteTensorCopyFromBuffer failed (size mismatch?)";
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
    const std::size_t outCount = d->tensorByteSize(out) / sizeof(float);
    output.resize(outCount);
    if (d->copyToBuffer(out, output.data(), outCount * sizeof(float)) != kTfLiteOk) {
        mError = "TfLiteTensorCopyToBuffer failed";
        return false;
    }
    return true;
}

} // namespace vision
