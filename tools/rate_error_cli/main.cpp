// rate_error_cli — CaptureController::processSamples (src/engine/CaptureController.cpp) 의
//  "WAV 입력 → tg_process → A/C 이벤트" 경로만 떼어낸 Qt-free 콘솔 도구.
//  출력 CSV(type,peak)는 WaveEvent(src/ui/tabs/MeasurementModel.h) 의 type/peak 멤버와 동일한 의미:
//   type = "A"(unlock) 또는 "C"(drop/lock), peak = 그 이벤트 시점의 엔벨로프 피크값(tg_event_t.peak_value).
#include "Timegrapher.h"
#include "WavReader.h"
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
    std::fprintf(csv, "type,peak\n");

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

    auto handleResult = [&](const tg_result_t &r) {
        for (size_t i = 0; i < r.num_events; i++) {
            const tg_event_t &ev = r.events[i];
            if (ev.type == TG_EVENT_A) {
                aEventCount++;
                std::fprintf(csv, "A,%.6f\n", (double)ev.peak_value);
            } else if (ev.type == TG_EVENT_C) {
                cEventCount++;
                std::fprintf(csv, "C,%.6f\n", (double)ev.peak_value);
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
    std::fclose(csv);

    std::fprintf(stderr, "done: A-events=%llu C-events=%llu -> %s\n",
                 (unsigned long long)aEventCount, (unsigned long long)cEventCount, opt.outputCsv.c_str());
    return 0;
}
