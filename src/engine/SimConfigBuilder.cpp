// SimConfigBuilder.cpp — 구 MainWindow::SimStart 의 합성 설정 조립부 분리(SoC).
#include "SimConfigBuilder.h"

WatchSynthStreamConfig SimConfigBuilder::build(const SimConfigParams &p)
{
    WatchSynthStreamConfig cfg;
    if (p.realistic) watch_synth_stream_realistic_config(&cfg);
    else             watch_synth_stream_clean_config(&cfg);

    cfg.bph                     = p.bph;
    cfg.sample_rate_hz          = p.sampleRateHz;
    cfg.beat_error_ms           = -p.beatErrorMs;   // 합성기 규약: 음수로 전달
    cfg.pcm_peak_amplitude      = 0.40;             // 정규화 float PCM 디지털 출력 레벨(합성 도메인 상수)
    cfg.watch_amplitude_degrees = p.watchAmplitudeDeg;
    cfg.lift_angle_degrees      = p.liftAngleDeg;
    cfg.rate_error_s_per_day    = p.rateErrorSecPerDay;
    return cfg;
}
