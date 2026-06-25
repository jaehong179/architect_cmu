// rate_error_cli — CaptureController::processSamples (src/engine/CaptureController.cpp) 의
//  "WAV 입력 → tg_process → A 이벤트 → TIC/TOC 의 Rate Error" 경로만 떼어낸 Qt-free 콘솔 도구.
//  출력 CSV(TIC,TOC)는 MeasurementEngine::computeRateError (src/engine/MeasurementEngine.cpp) 가
//   계산하는 WrappedRateError(InstTimingErrorMs 를 ±ERROR_RATE_Y_SCALE 로 wrap한 값, ms) 를
//   같은 HaveStartTime/TicTocBeatNumber/ZeroOffset 로직으로 재현해 기록한다.
//   RLS/이상치 검출/평균 로직은 표시값(WrappedRateError) 계산에 영향이 없으므로 제외.
#include "Timegrapher.h"
#include "WavReader.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr size_t DETECTOR_NUMBER_OF_SAMPLES = 4096;

struct Options {
    std::string inputWav;
    std::string outputCsv;
    bool   manualBph   = false;
    int    bph         = 0;
    double hpfCutoffHz = 200.0;   // tg_config_default 와 동일한 기본값
};

void printUsage(const char *argv0)
{
    std::fprintf(stderr,
        "usage: %s <input.wav> <output.csv> [--bph N] [--hpf HZ]\n"
        "  --bph N  manual BPH (default: auto-detect, like the app's BPH-Auto mode)\n"
        "  --hpf HZ high-pass filter cutoff Hz (default 200.0)\n",
        argv0);
}

bool parseArgs(int argc, char **argv, Options &opt)
{
    if (argc < 3) return false;
    opt.inputWav  = argv[1];
    opt.outputCsv = argv[2];
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--bph" && i + 1 < argc) {
            opt.manualBph = true; opt.bph = std::atoi(argv[++i]);
        } else if (a == "--hpf" && i + 1 < argc) {
            opt.hpfCutoffHz = std::atof(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    if (!parseArgs(argc, argv, opt)) { printUsage(argv[0]); return 1; }

    WavReader wav;
    std::string err;
    if (!wav.open(opt.inputWav, err)) {
        std::fprintf(stderr, "failed to open %s: %s\n", opt.inputWav.c_str(), err.c_str());
        return 1;
    }

    FILE *csv = std::fopen(opt.outputCsv.c_str(), "w");
    if (!csv) {
        std::fprintf(stderr, "failed to open %s for writing\n", opt.outputCsv.c_str());
        return 1;
    }

    const int sampleRate = (int)wav.sampleRate();

    tg_config_t cfg;
    tg_config_default(&cfg);
    cfg.sample_rate = sampleRate;
    if (opt.manualBph) { cfg.bph_mode = TG_BPH_MODE_MANUAL; cfg.manual_bph = opt.bph; }
    else                 cfg.bph_mode = TG_BPH_MODE_AUTO;
    cfg.suppress_pre_sync_events = true;     // CaptureController::createDetectors 와 동일
    cfg.hpf_cutoff_hz = opt.hpfCutoffHz;

    tg_context_t *ctx = tg_init(&cfg);
    if (!ctx) {
        std::fprintf(stderr, "tg_init failed\n");
        std::fclose(csv);
        return 1;
    }

    std::vector<float> block(DETECTOR_NUMBER_OF_SAMPLES);
    uint64_t aEventCount = 0, cEventCount = 0;

    // [TIC/TOC] MeasurementEngine::computeRateError (src/engine/MeasurementEngine.cpp:122-208) 의
    //  HaveStartTime/TicTocBeatNumber/ZeroOffset 상태머신과 WrappedRateError 계산을 그대로 재현한다.
    constexpr double ERROR_RATE_Y_SCALE = 10.0;   // MeasurementEngine.cpp 와 동일
    bool   ticTocHaveStartTime = false;
    bool   ticTocHaveZeroOffset = false;
    double ticTocStartTime = 0.0;
    double ticTocZeroOffsetValue = 0.0;
    uint64_t ticTocBeatNumber = 0;
    std::vector<double> ticRateError, tocRateError;

    auto wrapInToRange = [](double number, double lower, double upper) {
        const double rangeSize = upper - lower;
        const double shifted = number - lower;
        double wrapped = std::fmod(shifted, rangeSize);
        if (wrapped < 0) wrapped += rangeSize;
        return wrapped + lower;
    };

    auto handleAEvent = [&](double aEventSample, bool haveValidBph, double bph) {
        if (!haveValidBph && ticTocHaveStartTime) {
            ticTocHaveStartTime = false;
        } else if (haveValidBph && !ticTocHaveStartTime) {
            ticTocHaveStartTime = true;
            ticTocBeatNumber = 0;
            ticTocStartTime = aEventSample / (double)sampleRate;
            ticTocHaveZeroOffset = false;
            ticTocZeroOffsetValue = 0.0;
        }
        if (haveValidBph && ticTocHaveStartTime) {
            const double timeMeasured = aEventSample / (double)sampleRate;
            const double expectedTimeTarget = 3600.0 / bph;
            ticTocBeatNumber++;
            const bool isTic = ((ticTocBeatNumber - 1) & 1) == 0;

            double instTimingErrorMs =
                ((ticTocStartTime + ticTocBeatNumber * expectedTimeTarget) - timeMeasured) * 1000.0;
            if (!ticTocHaveZeroOffset) {
                ticTocHaveZeroOffset = true;
                ticTocZeroOffsetValue = -instTimingErrorMs;
            }
            instTimingErrorMs += ticTocZeroOffsetValue;

            const double wrappedRateError =
                wrapInToRange(instTimingErrorMs, -ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
            (isTic ? ticRateError : tocRateError).push_back(wrappedRateError);
        }
    };

    auto handleResult = [&](const tg_result_t &r) {
        for (size_t i = 0; i < r.num_events; i++) {
            const tg_event_t &ev = r.events[i];
            if (ev.type == TG_EVENT_A) {
                aEventCount++;
                const double aEventSample = ev.sample_index + ev.sub_sample_offset;
                handleAEvent(aEventSample, r.sync_status == TG_SYNC_SYNCED, r.detected_bph);
            } else if (ev.type == TG_EVENT_C) {
                cEventCount++;
            }
        }
    };

    for (;;) {
        const size_t n = wav.readSamples(block.data(), block.size());
        if (n == 0) break;
        tg_result_t result;
        if (tg_process(ctx, block.data(), n, &result) != 0) {
            std::fprintf(stderr, "tg_process failed\n");
            break;
        }
        handleResult(result);
        if (n < block.size()) break; // 마지막(짧은) 블록까지 처리했으면 종료
    }

    tg_result_t flushResult;
    if (tg_flush(ctx, &flushResult) == 0) handleResult(flushResult);

    tg_destroy(ctx);

    // [TIC/TOC] 같은 행에 i번째 TIC, i번째 TOC 의 WrappedRateError(ms)를 나란히 기록(개수가 다르면 남는 칸은 빈 칸).
    std::fprintf(csv, "TIC,TOC\n");
    const size_t rowCount = std::max(ticRateError.size(), tocRateError.size());
    for (size_t i = 0; i < rowCount; i++) {
        if (i < ticRateError.size()) std::fprintf(csv, "%.6f", ticRateError[i]);
        std::fprintf(csv, ",");
        if (i < tocRateError.size()) std::fprintf(csv, "%.6f", tocRateError[i]);
        std::fprintf(csv, "\n");
    }
    std::fclose(csv);

    std::fprintf(stderr, "done: A-events=%llu C-events=%llu -> %s\n",
                 (unsigned long long)aEventCount, (unsigned long long)cEventCount, opt.outputCsv.c_str());
    return 0;
}
