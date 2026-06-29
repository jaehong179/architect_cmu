#ifndef SOUNDIMAGEWIDGET_H
#define SOUNDIMAGEWIDGET_H
// 폴딩 사운드 이미지 표시 위젯.
//  캔버스(QImage)는 '고정 크기'로 한 번 생성하고(위젯 크기와 독립), paintEvent 가 위젯
//  영역에 맞춰 스케일링해 그린다. 위젯 resize 가 캔버스를 재생성하지 않으므로
//  (1) 렌더러가 캐싱한 포인터가 무효화되지 않고(use-after-free 방지),
//  (2) 탭 전환 시 누적 이미지가 보존되어 warmup/anchor 재시작·앞부분 손실이 없다.
//  마우스 휠=확대/축소, 드래그=팬, 더블클릭=전체 보기 리셋.
//  렌더러 live 컬럼을 받으면(측정 중) 자동으로 갱신 위치를 따라간다(수동 팬 전까지).
#include "SoundImageRenderer.h"   // OverlayMarker
#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

class SoundImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SoundImageWidget(QWidget *parent = nullptr);
    ~SoundImageWidget() override;                    // 캔버스(image) 해제 — 위젯 파괴 시 누수 방지
    void     CreateImage(int width, int height);   // 고정 크기 캔버스 생성(위젯 크기와 무관). 1회 호출.
    void     DrawImage(void);                       // 재그리기 요청(update)
    void     setLiveColumn(int column);             // 현재 기록 중인 컬럼(렌더러 write head)
    // 표시 시점 또렷 오버레이로 그릴 마커(이미지 좌표). 저해상 캔버스를 bilinear 업스케일해도
    //  마커는 매 프레임 벡터로 선명하게 다시 그린다(이미지에 굽지 않음).
    void     setOverlayMarkers(std::vector<SoundImageRenderer::OverlayMarker> markers);
    void     setBeatPeriodMs(double ms);            // 한 비트 길이(ms) — y축 ms·x축 시간 눈금 산출용
    void     resetZoom(void);                        // 새 측정 시작 시 확대/이동 초기화(전체 보기)
    QImage  *GetImage(void);                         // 렌더러가 그릴 대상 캔버스
    void     paintEvent(QPaintEvent *event) override;
    void     wheelEvent(QWheelEvent *event) override;
    void     mousePressEvent(QMouseEvent *event) override;
    void     mouseMoveEvent(QMouseEvent *event) override;
    void     mouseReleaseEvent(QMouseEvent *event) override;
    void     mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    struct Viewport {
        QRectF destRect;
        QRectF sourceRect;
        double displayScale = 1.0;
    };

    QRectF   plotRect() const;                       // 축 여백을 뺀 실제 이미지 표시 영역
    void     drawAxes(QPainter &painter, const Viewport &vp) const;   // x/y 눈금·단위 표시
    Viewport computeViewport() const;
    QPointF  widgetToImage(const QPointF &widgetPos, const Viewport &vp) const;
    void     clampOffset();
    void     followLiveColumn();
    void     resetView();
    void     selectAt(const QPointF &widgetPos);     // 클릭한 비트(컬럼) 선택 → 초록↔파랑 간격 표시
    bool     findPairAtColumn(double colX, SoundImageRenderer::OverlayMarker &green,
                              SoundImageRenderer::OverlayMarker &blue) const;
    void     drawSelection(QPainter &painter, const Viewport &vp) const;

    QImage  *image = nullptr;
    double   mViewScale  = 1.0;   // 1.0 = 전체 fit, >1 = 확대
    QPointF  mViewOffset;           // 이미지 좌표(픽셀) 뷰 좌상단
    int      mLiveColumn = -1;      // 렌더러 write head (-1 = 미전달)
    bool     mUserViewLocked = false; // 수동 팬 이후 live 자동 추적 중단
    bool     mPanning    = false;
    QPointF  mPanStartWidget;
    QPointF  mPanStartOffset;
    bool     mLeftDown   = false;   // 좌클릭 누름 중(클릭/드래그 구분용)
    bool     mPressMoved = false;   // 누른 뒤 이동 발생(드래그) → 클릭 아님
    QPointF  mPressWidgetPos;
    bool     mHasSelection = false; // 비트 점 선택됨 → 간격 표시
    double   mSelColX    = 0.0;      // 선택된 이미지 컬럼 x
    std::vector<SoundImageRenderer::OverlayMarker> mOverlayMarkers;   // 또렷 마커(이미지 좌표)
    double   mBeatPeriodMs = 0.0;   // 한 비트 길이(ms); 0 = 미확정(눈금 숫자 생략)

    // 축 라벨 여백(픽셀): 왼쪽=y 눈금, 아래=x 눈금.
    static constexpr int kMarginLeft   = 48;
    static constexpr int kMarginBottom = 26;
    static constexpr int kMarginTop    = 6;
    static constexpr int kMarginRight  = 8;

    static constexpr double kMinViewScale = 1.0;
    static constexpr double kMaxViewScale = 20.0;
    static constexpr double kWheelFactor    = 1.15;
};

#endif // SOUNDIMAGEWIDGET_H
