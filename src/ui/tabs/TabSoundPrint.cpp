#include "TabSoundPrint.h"
#include "LegendBox.h"
#include "SoundImageWidget.h"
#include <QVBoxLayout>

// 사운드 이미지 이벤트 마커 한 변의 픽셀 수(정사각).
static constexpr int kMarkerPixelSize = 3;
// 이벤트 타입(WaveEvent.type) — tg_event_type_t 와 동일 값.
static constexpr int kEventA = 1;   // unlock (impulse)
static constexpr int kEventC = 2;   // drop/lock
// 고정 캔버스 크기(위젯 크기와 독립) — SoundImage 기하 1019×654.
//  너비=표시할 비트 컬럼 수, 높이=비트 주기 내 시간 해상도. paintEvent 가 위젯에 맞춰 스케일링.
static constexpr int kCanvasWidth  = 1019;
static constexpr int kCanvasHeight = 654;

TabSoundPrint::TabSoundPrint(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(makeLegendBox(QStringLiteral(
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td valign='top'><b>Display&nbsp;:</b></td><td>folded sound print · "
        "horizontal=successive beats · vertical=phase within beat (top to bottom) · "
        "wheel=zoom · drag=pan (locks follow) · dbl-click=reset/follow live</td></tr>"
        "<tr><td valign='top'><b>Envelope&nbsp;:</b></td><td>"
        "<font color='#ff3333'><b>■ Rectified</b></font> folded |raw-DC| envelope (gamma), "
        "brighter=louder · same path as Rate/Scope scope <i>Rectified</i></td></tr>"
        "<tr><td valign='top'><b>Onset&nbsp;:</b></td><td>"
        "<font color='#00ff00'><b>■ A (unlock)</b></font> beat-onset marker · each onset alternates "
        "<font color='#c82828'><b>Tic Rate</b></font> / "
        "<font color='#2850c8'><b>Toc Rate</b></font> in Rate/Scope</td></tr>"
        "<tr><td valign='top'><b>Impulse&nbsp;:</b></td><td>"
        "<font color='#0000ff'><b>■ C (drop/lock)</b></font> lock-face impulse peak within the beat cycle "
        "(escapement drop onto locking face)</td></tr>"
        "</table>"), this));
    mImage = new SoundImageWidget(this);
    lay->addWidget(mImage, 1);
    // 고정 캔버스를 즉시 생성 → 탭 가시성/크기와 무관하게 항상 누적·즉시 표시(앞부분 손실·전환 지연 방지).
    mImage->CreateImage(kCanvasWidth, kCanvasHeight);
}

// 표시 설정.
SoundImageRenderer::Config TabSoundPrint::makeConfig(int sampleRateHz) const
{
    SoundImageRenderer::Config cfg;
    cfg.bph                     = 0.0;                 // 초기엔 미지(동기 후 setBph)
    cfg.sample_rate_hz          = sampleRateHz;
    cfg.sound_color             = qRgba(255, 51, 51, 255);
    cfg.background_color        = qRgba(24, 24, 31, 255);
    cfg.vertical_time_direction = SoundImageRenderer::TimeStartsAtTopMovesDown;
    cfg.warmup_columns          = 2;
    cfg.anchor_columns          = 12;
    cfg.gamma                   = 0.5f;
    cfg.live_preview_current_column = true;
    cfg.draw_markers_into_image = false;   // 마커는 위젯에서 표시 시점 또렷 오버레이로(확대해도 선명)
    return cfg;
}

void TabSoundPrint::ensureRenderer(quint64 originSample)
{
    if (mInitialized) return;
    if (mSampleRateHz <= 0) return;
    QImage *img = mImage->GetImage();
    if (!img || img->isNull()) return;                // 고정 캔버스 — 생성자 이후 항상 유효
    if (!mRenderer.initialize(img, makeConfig(mSampleRateHz))) return;
    // [측정 대기] 절대 샘플 원점 = 이 블록 raw[0] 의 절대 인덱스(wave.rawStart).
    //  웜업이 앞선 오디오를 소비해 첫 블록이 0이 아닌 위치에서 시작해도 마커(절대 markSample)와
    //  렌더러의 절대 샘플 클럭이 일치한다 → 마커가 어긋나 사운드프린트가 틀어지는 현상 방지.
    mRenderer.reset(originSample);
    mInitialized = true;
    mHasBph = false;                                  // (재)초기화 후 다음 동기 프레임에서 BPH 적용
}

void TabSoundPrint::onResetSession()
{
    mInitialized = false;   // 다음 onWave에서 고정 캔버스로 재초기화(누적 비움)
    mHasBph = false;
    if (mImage) {
        mImage->clearImage();                // 캔버스 즉시 비움 → 다음 onWave 전에도 잔상 없음
        mImage->resetZoom();                 // 새 측정 시작 → 확대/이동 초기화(전체 보기)
        mImage->setOverlayMarkers({});       // 이전 세션 마커 제거
        mImage->setBeatPeriodMs(0.0);        // BPH 재확정 전까지 눈금 숫자 보류
    }
}

void TabSoundPrint::onMeasurement(const MeasurementSnapshot &snap)
{
    if (snap.sampleRateHz > 0) mSampleRateHz = snap.sampleRateHz;
}

void TabSoundPrint::onWave(const WaveBlock &wave)
{
    if (wave.sampleRateHz > 0) mSampleRateHz = wave.sampleRateHz;
    ensureRenderer(wave.rawStart);
    if (!mInitialized) return;                        // 샘플레이트 미확정 → 이 슬라이스 건너뜀

    // [정지/탭전환 강건성] 전역 정지 전환 등으로 일부 블록이 이 탭에 전달되지 않으면(TabManager 가
    //  정지 중 onWave 를 게이트) 절대 샘플 클럭에 갭이 생긴다. 렌더러는 연속 입력을 가정하므로,
    //  블록의 절대 시작 인덱스(rawStart)로 위상을 재동기해 resume 시 fold 가 위/아래로 깨지는 것을
    //  막는다. 연속이면 no-op. (마커도 같은 절대 클럭이라 함께 정렬 유지)
    mRenderer.resyncAbsolutePosition(wave.rawStart);

    // 원신호(정류 전 PCM)를 렌더러에 주입 — 폴딩 사운드 이미지 갱신.
    if (wave.raw && wave.rawN > 0)
        mRenderer.processSamples(wave.raw, (std::size_t)wave.rawN);

    // 비트 동기 확정 시 BPH 1회 적용 → 이후 컬럼 렌더링 시작.
    if (!mHasBph && wave.synced) {
        mHasBph = true;
        mRenderer.setBph(wave.bph);
        if (wave.bph > 0.0) mImage->setBeatPeriodMs(3600000.0 / wave.bph);   // 한 비트 ms → 축 눈금
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
    // 폴딩 누적(processSamples)·마커 등록은 위에서 항상 수행(숨김에도 이미지 완성도 유지).
    //  단, 오버레이 마커 벡터 빌드 + 재그리기는 보일 때만 — 숨김 탭 부하↓(표시 전환 시 위젯이 자동 repaint).
    if (mHasBph && isVisible()) {
        mImage->setLiveColumn(mRenderer.currentColumn());
        mImage->setOverlayMarkers(mRenderer.overlayMarkers());   // 또렷 마커 좌표 갱신
        mImage->DrawImage();
    }
    if (isVisible())
        mImage->DrawImage();
}

void TabSoundPrint::onShown()
{
    if (mInitialized && mImage)
        mImage->DrawImage();
}
