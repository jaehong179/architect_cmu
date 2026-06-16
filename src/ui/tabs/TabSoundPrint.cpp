#include "TabSoundPrint.h"
#include "SoundImageWidget.h"
#include <QVBoxLayout>

// 사운드 이미지 이벤트 마커 한 변의 픽셀 수(정사각) — 구 MainWindow SND_PIXEL_SIZE.
static constexpr int kMarkerPixelSize = 3;
// 이벤트 타입(WaveEvent.type) — tg_event_type_t 와 동일 값.
static constexpr int kEventA = 1;   // unlock (impulse)
static constexpr int kEventC = 2;   // drop/lock

TabSoundPrint::TabSoundPrint(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    mImage = new SoundImageWidget(this);
    lay->addWidget(mImage);
    // 위젯이 resize로 QImage를 재생성하면 렌더러를 새 이미지에 재바인딩(use-after-free 방지).
    connect(mImage, &SoundImageWidget::imageRecreated, this, [this] { tryInitOrRebind(); });
}

// 구 MainWindow::buildSoundImageConfig 와 동일한 표시 설정.
SoundImageRenderer::Config TabSoundPrint::makeConfig(int sampleRateHz) const
{
    SoundImageRenderer::Config cfg;
    cfg.bph                     = 0.0;                 // 초기엔 미지(동기 후 setBph)
    cfg.sample_rate_hz          = sampleRateHz;
    cfg.sound_color             = qRgba(255, 0, 0, 255);
    cfg.background_color        = qRgba(255, 255, 255, 255);
    cfg.vertical_time_direction = SoundImageRenderer::TimeStartsAtTopMovesDown;
    cfg.warmup_columns          = 2;
    cfg.anchor_columns          = 12;
    cfg.gamma                   = 0.5f;
    cfg.live_preview_current_column = true;
    return cfg;
}

void TabSoundPrint::tryInitOrRebind()
{
    QImage *img = mImage->GetImage();
    if (!img || img->isNull()) { mInitialized = false; return; }  // 아직 미배치(0크기) → 다음 호출에 재시도
    if (mSampleRateHz <= 0) return;
    if (!mRenderer.initialize(img, makeConfig(mSampleRateHz))) { mInitialized = false; return; }
    mRenderer.reset();
    mInitialized = true;
    mHasBph = false;                                  // (재)초기화 후 다음 동기 프레임에서 BPH 재적용
}

void TabSoundPrint::onResetSession()
{
    mInitialized = false;   // 다음 onWave에서 현재 이미지/샘플레이트로 재초기화
    mHasBph = false;
}

void TabSoundPrint::onMeasurement(const MeasurementSnapshot &snap)
{
    if (snap.sampleRateHz > 0) mSampleRateHz = snap.sampleRateHz;
}

void TabSoundPrint::onWave(const WaveBlock &wave)
{
    if (wave.sampleRateHz > 0) mSampleRateHz = wave.sampleRateHz;
    if (!mInitialized) tryInitOrRebind();
    if (!mInitialized) return;                        // 이미지 미준비 → 이 슬라이스 건너뜀

    // 원신호(정류 전 PCM)를 렌더러에 주입 — 폴딩 사운드 이미지 갱신.
    if (wave.raw && wave.rawN > 0)
        mRenderer.processSamples(wave.raw, (std::size_t)wave.rawN);

    // 비트 동기 확정 시 BPH 1회 적용 → 이후 컬럼 렌더링 시작.
    if (!mHasBph && wave.synced) {
        mHasBph = true;
        mRenderer.setBph(wave.bph);
    }

    // A=green / C=blue 이벤트 마커 (BPH 확정 후에만; 위치는 표시 해상 인덱스 markSample).
    if (mHasBph && wave.events) {
        for (int i = 0; i < wave.numEvents; ++i) {
            const WaveEvent &e = wave.events[i];
            if (e.type == kEventA)
                mRenderer.markAEventAbsoluteSampleIndex(e.markSample, qRgba(0, 255, 0, 255), kMarkerPixelSize);
            else if (e.type == kEventC)
                mRenderer.markCEventAbsoluteSampleIndex(e.markSample, qRgba(0, 0, 255, 255), kMarkerPixelSize);
        }
    }
    mImage->DrawImage();
}
