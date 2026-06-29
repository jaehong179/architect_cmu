#include "SoundImageWidget.h"
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

SoundImageWidget::SoundImageWidget(QWidget *parent)
    : QWidget{parent}
{
    setMouseTracking(true);
}

SoundImageWidget::~SoundImageWidget()
{
    delete image;   // CreateImage 로 할당된 캔버스 해제(소멸자 누락 시 위젯 파괴마다 누수)
}

// 고정 크기 캔버스를 생성한다(위젯 크기와 무관). 렌더러는 이 캔버스에 그리고,
//  paintEvent 가 위젯 영역에 맞춰 스케일링해 표시한다. resize 시 캔버스를 재생성하지
//  않으므로 렌더러 포인터가 안정적이고 누적 이미지가 보존된다.
void SoundImageWidget::CreateImage(int w, int h)
{
    delete image;
    image = nullptr;
    if (w <= 0 || h <= 0) return;
    image = new QImage(w, h, QImage::Format_ARGB32);
    image->fill(QColor(24, 24, 31));
    resetView();
}

QImage * SoundImageWidget::GetImage(void)
{
    return image;
}

void SoundImageWidget::setLiveColumn(int column)
{
    mLiveColumn = column;
}

void SoundImageWidget::setOverlayMarkers(std::vector<SoundImageRenderer::OverlayMarker> markers)
{
    mOverlayMarkers = std::move(markers);   // 갱신은 DrawImage()/update() 가 담당
}

void SoundImageWidget::DrawImage(void)
{
    followLiveColumn();
    update();
}

void SoundImageWidget::setBeatPeriodMs(double ms)
{
    if (ms > 0.0 && ms != mBeatPeriodMs) { mBeatPeriodMs = ms; update(); }
}

void SoundImageWidget::resetZoom(void)
{
    resetView();   // 확대/이동 초기화 → 전체 보기(fit)
}

// 축 라벨 여백을 뺀 이미지 표시 영역.
QRectF SoundImageWidget::plotRect() const
{
    const double w = qMax(0.0, (double)width()  - kMarginLeft - kMarginRight);
    const double h = qMax(0.0, (double)height() - kMarginTop  - kMarginBottom);
    return QRectF(kMarginLeft, kMarginTop, w, h);
}

SoundImageWidget::Viewport SoundImageWidget::computeViewport() const
{
    Viewport vp;
    if (!image || image->isNull()) return vp;

    const double iw = image->width();
    const double ih = image->height();
    const QRectF pr = plotRect();
    const double ww = pr.width();
    const double wh = pr.height();
    if (iw <= 0.0 || ih <= 0.0 || ww <= 0.0 || wh <= 0.0) return vp;

    const double fitScale = qMin(ww / iw, wh / ih);
    vp.displayScale = fitScale * mViewScale;

    const QSizeF drawnSize(iw * vp.displayScale, ih * vp.displayScale);
    // 가로는 좌측 정렬(사운드프린트는 왼→오로 시간 진행이라 왼쪽부터 시작해야 자연스럽다 — 가운데
    //  정렬 시 왼쪽에 빈 여백이 생김). 세로는 그대로 가운데 정렬. (plotRect 기준 오프셋)
    const QPointF topLeft(pr.left(), pr.top() + (wh - drawnSize.height()) * 0.5);
    vp.destRect = QRectF(topLeft, drawnSize);

    const double visibleW = iw / mViewScale;
    const double visibleH = ih / mViewScale;
    vp.sourceRect = QRectF(mViewOffset.x(), mViewOffset.y(), visibleW, visibleH);
    return vp;
}

QPointF SoundImageWidget::widgetToImage(const QPointF &widgetPos, const Viewport &vp) const
{
    if (!vp.destRect.contains(widgetPos) || vp.destRect.width() <= 0.0 || vp.destRect.height() <= 0.0)
        return mViewOffset + QPointF(vp.sourceRect.width() * 0.5, vp.sourceRect.height() * 0.5);

    const double u = (widgetPos.x() - vp.destRect.left()) / vp.destRect.width();
    const double v = (widgetPos.y() - vp.destRect.top()) / vp.destRect.height();
    return QPointF(vp.sourceRect.left() + u * vp.sourceRect.width(),
                   vp.sourceRect.top() + v * vp.sourceRect.height());
}

void SoundImageWidget::clampOffset()
{
    if (!image || image->isNull()) return;

    const double iw = image->width();
    const double ih = image->height();
    const double visibleW = iw / mViewScale;
    const double visibleH = ih / mViewScale;

    double maxOx = qMax(0.0, iw - visibleW);
    double maxOy = qMax(0.0, ih - visibleH);

    // live head가 아직 캔버스 끝에 닿지 않았어도, 갱신 위치까지 스크롤 가능하게 한다.
    if (mLiveColumn >= 0 && visibleW < iw) {
        const double liveTrail = static_cast<double>(mLiveColumn) + 1.0;
        maxOx = qMax(maxOx, qMax(0.0, liveTrail - visibleW));
    }

    // 끝/시작 픽셀을 뷰 중앙까지 옮길 수 있도록 약간의 overscroll 허용.
    const double overshootX = visibleW * 0.5;
    const double overshootY = visibleH * 0.5;
    mViewOffset.setX(qBound(-overshootX, mViewOffset.x(), maxOx + overshootX));
    mViewOffset.setY(qBound(-overshootY, mViewOffset.y(), maxOy + overshootY));
}

void SoundImageWidget::followLiveColumn()
{
    if (mUserViewLocked || mLiveColumn < 0 || !image || image->isNull()) return;

    const double iw = image->width();
    const double visibleW = iw / mViewScale;
    if (visibleW >= iw) return;

    const double liveEdge = static_cast<double>(mLiveColumn) + 1.0;
    const double viewLeft = mViewOffset.x();
    const double viewRight = viewLeft + visibleW;
    const double margin = qMax(8.0, visibleW * 0.12);

    if (liveEdge > viewRight - margin) {
        mViewOffset.setX(liveEdge - visibleW + margin);
        clampOffset();
        return;
    }

    // 측정 초반: live head가 왼쪽에 있을 때 뷰가 오른쪽 빈 영역에 고정되지 않도록.
    if (liveEdge < viewLeft + margin) {
        mViewOffset.setX(qMax(0.0, liveEdge - margin));
        clampOffset();
    }
}

void SoundImageWidget::resetView()
{
    mViewScale = 1.0;
    mViewOffset = QPointF();
    mPanning = false;
    mUserViewLocked = false;
    unsetCursor();
    update();
}

void SoundImageWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 31));

    if (!image || image->isNull()) return;

    const Viewport vp = computeViewport();
    if (vp.destRect.isEmpty() || vp.sourceRect.isEmpty()) return;

    // 고정 캔버스(1019×654)를 위젯/확대 배율에 맞춰 키울 때 최근접 보간이면 픽셀이 블록으로 깨진다.
    //  bilinear 보간(SmoothPixmapTransform)으로 그래스를 부드럽게 업스케일.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(vp.destRect, *image, vp.sourceRect);

    // 마커(A/C)는 저해상 이미지에 굽지 않고 표시 시점에 벡터로 또렷하게 덧그린다 → 확대해도 선명·정확.
    //  크기를 배율에 비례시키면 3px 마커가 확대 시 거대한 블록이 되어 '뭉개진' 띠로 보인다. 그래서
    //  배율과 무관한 '고정 작은 점'으로 그린다 → 확대해도 작은 점(각 비트 구분), 축소 시엔 겹쳐 선으로 보임.
    if (!mOverlayMarkers.empty() && vp.sourceRect.width() > 0.0 && vp.sourceRect.height() > 0.0) {
        constexpr double kDotPx = 4.0;   // 마커 점 한 변(위젯 픽셀, 고정)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::Antialiasing, false);              // 또렷한 사각 에지
        painter.setClipRect(vp.destRect);                                  // 레터박스 여백으로 번지지 않게
        painter.setPen(Qt::NoPen);
        for (const SoundImageRenderer::OverlayMarker &m : mOverlayMarkers) {
            const double u = (m.x + 0.5 - vp.sourceRect.left()) / vp.sourceRect.width();
            const double v = (m.y + 0.5 - vp.sourceRect.top())  / vp.sourceRect.height();
            const double cx = vp.destRect.left() + u * vp.destRect.width();
            const double cy = vp.destRect.top()  + v * vp.destRect.height();
            painter.fillRect(QRectF(cx - kDotPx * 0.5, cy - kDotPx * 0.5, kDotPx, kDotPx),
                             QColor::fromRgba(m.color));
        }
        painter.setClipping(false);
    }

    drawAxes(painter, vp);   // x(시간)·y(비트 내 ms) 눈금·단위
}

// '좋은' 눈금 간격(1·2·5 ×10^k) 선택.
static double niceTickStep(double rough)
{
    if (rough <= 0.0) return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(rough)));
    const double n = rough / mag;
    const double m = (n < 1.5) ? 1.0 : (n < 3.5) ? 2.0 : (n < 7.5) ? 5.0 : 10.0;
    return m * mag;
}

// 축 눈금/라벨/단위. y=한 비트 안의 시간(ms, 위=0 → 아래=beat period),
//  x=가시 창의 경과 시간(s, 좌→우). 캔버스를 fold 한 상대 스케일이라 절대 0 위치는 fold 원점.
void SoundImageWidget::drawAxes(QPainter &painter, const Viewport &vp) const
{
    if (!image || vp.destRect.isEmpty() || vp.sourceRect.isEmpty()) return;

    const double ih = image->height();
    const QRectF dst = vp.destRect;
    const QRectF src = vp.sourceRect;
    const QColor axisCol(190, 190, 198);
    const QColor tickCol(120, 120, 130);

    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont f = painter.font(); f.setPointSizeF(8.0); painter.setFont(f);

    // ── y축: 비트 내 시간(ms) ──
    if (mBeatPeriodMs > 0.0) {
        const double topMs = (src.top()) / ih * mBeatPeriodMs;
        const double botMs = (src.top() + src.height()) / ih * mBeatPeriodMs;
        const double step  = niceTickStep((botMs - topMs) / 6.0);
        painter.setPen(QPen(tickCol, 1.0));
        const double startMs = std::ceil(topMs / step) * step;
        for (double ms = startMs; ms <= botMs + 1e-6; ms += step) {
            const double row = ms / mBeatPeriodMs * ih;
            const double y = dst.top() + (row - src.top()) / src.height() * dst.height();
            if (y < dst.top() - 0.5 || y > dst.bottom() + 0.5) continue;
            painter.setPen(QPen(tickCol, 1.0));
            painter.drawLine(QPointF(dst.left() - 4, y), QPointF(dst.left(), y));
            painter.setPen(axisCol);
            painter.drawText(QRectF(0, y - 7, kMarginLeft - 6, 14),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(ms, 'f', 0));
        }
        painter.setPen(axisCol);
        painter.save();
        painter.translate(11, dst.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-80, -10, 160, 14), Qt::AlignCenter,
                         QStringLiteral("time in beat (ms)"));
        painter.restore();
    }

    // ── x축: 가시 창 경과 시간(s) ──
    if (mBeatPeriodMs > 0.0) {
        const double periodS = mBeatPeriodMs / 1000.0;   // 1 컬럼(=1 비트) = periodS 초
        const double spanS = src.width() * periodS;
        const double step  = niceTickStep(spanS / 7.0);
        painter.setPen(QPen(tickCol, 1.0));
        for (double s = 0.0; s <= spanS + 1e-6; s += step) {
            const double col = src.left() + s / periodS;
            const double x = dst.left() + (col - src.left()) / src.width() * dst.width();
            if (x < dst.left() - 0.5 || x > dst.right() + 0.5) continue;
            painter.setPen(QPen(tickCol, 1.0));
            painter.drawLine(QPointF(x, dst.bottom()), QPointF(x, dst.bottom() + 4));
            painter.setPen(axisCol);
            const QString lbl = (step < 1.0) ? QString::number(s, 'f', 1) : QString::number(s, 'f', 0);
            painter.drawText(QRectF(x - 24, dst.bottom() + 5, 48, 13),
                             Qt::AlignHCenter | Qt::AlignTop, lbl);
        }
        painter.setPen(axisCol);
        painter.drawText(QRectF(dst.left(), height() - 14, dst.width(), 13),
                         Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("time across window (s) →"));
    }
}

void SoundImageWidget::wheelEvent(QWheelEvent *event)
{
    if (!image || image->isNull()) {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    const Viewport vp = computeViewport();
    const QPointF anchor = widgetToImage(event->position(), vp);

    const double factor = delta > 0 ? kWheelFactor : (1.0 / kWheelFactor);
    const double newScale = qBound(kMinViewScale, mViewScale * factor, kMaxViewScale);
    if (qFuzzyCompare(newScale, mViewScale)) {
        event->accept();
        return;
    }

    const double iw = image->width();
    const double ih = image->height();
    const double newVisibleW = iw / newScale;
    const double newVisibleH = ih / newScale;

    double u = 0.5, v = 0.5;
    if (vp.destRect.contains(event->position()) && vp.destRect.width() > 0.0 && vp.destRect.height() > 0.0) {
        u = (event->position().x() - vp.destRect.left()) / vp.destRect.width();
        v = (event->position().y() - vp.destRect.top()) / vp.destRect.height();
    }

    mViewScale = newScale;
    mViewOffset.setX(anchor.x() - u * newVisibleW);
    mViewOffset.setY(anchor.y() - v * newVisibleH);
    clampOffset();
    followLiveColumn();
    update();
    event->accept();
}

void SoundImageWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || mViewScale <= 1.0) {
        QWidget::mousePressEvent(event);
        return;
    }

    const Viewport vp = computeViewport();
    if (!vp.destRect.contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }

    mPanning = true;
    mPanStartWidget = event->position();
    mPanStartOffset = mViewOffset;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void SoundImageWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!mPanning) {
        if (mViewScale > 1.0) {
            const Viewport vp = computeViewport();
            setCursor(vp.destRect.contains(event->position()) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    const Viewport vp = computeViewport();
    if (vp.displayScale <= 0.0) {
        event->accept();
        return;
    }

    const QPointF delta = event->position() - mPanStartWidget;
    mViewOffset.setX(mPanStartOffset.x() - delta.x() / vp.displayScale);
    mViewOffset.setY(mPanStartOffset.y() - delta.y() / vp.displayScale);
    clampOffset();
    update();
    event->accept();
}

void SoundImageWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (mPanning && event->button() == Qt::LeftButton) {
        mPanning = false;
        mUserViewLocked = true;
        setCursor(mViewScale > 1.0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SoundImageWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
