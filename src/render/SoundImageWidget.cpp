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
    mHasSelection = false;   // 전체 보기로 돌아가면 선택 해제
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

    // 확대 시 destRect 가 plot 영역보다 커진다 → 이미지/마커를 plot 영역에 클리핑해 축 여백을 침범하지
    //  않게 한다(안 하면 이미지가 x/y 축 라벨 위를 덮어 안 보임).
    const QRectF pr = plotRect();

    // 고정 캔버스(1019×654)를 위젯/확대 배율에 맞춰 키울 때 최근접 보간이면 픽셀이 블록으로 깨진다.
    //  bilinear 보간(SmoothPixmapTransform)으로 그래스를 부드럽게 업스케일.
    painter.setClipRect(pr);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(vp.destRect, *image, vp.sourceRect);

    // 마커(A/C)는 저해상 이미지에 굽지 않고 표시 시점에 벡터로 또렷하게 덧그린다 → 확대해도 선명·정확.
    //  크기를 배율에 비례시키면 3px 마커가 확대 시 거대한 블록이 되어 '뭉개진' 띠로 보인다. 그래서
    //  배율과 무관한 '고정 작은 점'으로 그린다 → 확대해도 작은 점(각 비트 구분), 축소 시엔 겹쳐 선으로 보임.
    if (!mOverlayMarkers.empty() && vp.sourceRect.width() > 0.0 && vp.sourceRect.height() > 0.0) {
        constexpr double kDotPx = 4.5;   // 마커 점 지름(위젯 픽셀, 고정)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::Antialiasing, true);               // 매끄러운 동그라미
        painter.setClipRect(pr);                                            // plot 영역 밖(축 여백)으로 안 번지게
        painter.setPen(Qt::NoPen);
        for (const SoundImageRenderer::OverlayMarker &m : mOverlayMarkers) {
            const double u = (m.x + 0.5 - vp.sourceRect.left()) / vp.sourceRect.width();
            const double v = (m.y + 0.5 - vp.sourceRect.top())  / vp.sourceRect.height();
            const double cx = vp.destRect.left() + u * vp.destRect.width();
            const double cy = vp.destRect.top()  + v * vp.destRect.height();
            painter.setBrush(QColor::fromRgba(m.color));
            painter.drawEllipse(QPointF(cx, cy), kDotPx * 0.5, kDotPx * 0.5);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setClipping(false);
    }

    drawSelection(painter, vp);   // 선택한 비트: 초록↔파랑 점선 + 간격(ms)
    drawAxes(painter, vp);        // x(시간)·y(비트 내 ms) 눈금·단위
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

// 축 눈금/라벨/단위. 축선은 plot 영역 가장자리에 '고정'(확대해도 항상 보임), 눈금 값만 현재
//  보이는 영역에 맞춰 바뀐다. y=한 비트 안의 시간(ms), x=가시 창의 경과 시간(s, 좌→우).
//  (캔버스를 fold 한 상대 스케일이라 y 절대 0 위치는 fold 원점)
void SoundImageWidget::drawAxes(QPainter &painter, const Viewport &vp) const
{
    if (!image || vp.destRect.isEmpty() || vp.sourceRect.isEmpty()) return;

    const double ih = image->width() > 0 ? image->height() : 0.0;
    const QRectF dst = vp.destRect;
    const QRectF src = vp.sourceRect;
    const QRectF pr  = plotRect();
    if (pr.width() <= 0.0 || pr.height() <= 0.0 || dst.width() <= 0.0 || dst.height() <= 0.0) return;
    const QColor axisCol(190, 190, 198);
    const QColor tickCol(120, 120, 130);

    // 이미지 좌표 ↔ 위젯 좌표 (확대 시 dst 가 pr 보다 큼).
    auto colToX = [&](double col){ return dst.left() + (col - src.left()) / src.width()  * dst.width();  };
    auto rowToY = [&](double row){ return dst.top()  + (row - src.top())  / src.height() * dst.height(); };
    auto xToCol = [&](double x){ return src.left() + (x - dst.left()) / dst.width()  * src.width();  };
    auto yToRow = [&](double y){ return src.top()  + (y - dst.top())  / dst.height() * src.height(); };

    // plot 영역 안에서 실제 이미지가 보이는 가장자리(여백 침범 방지).
    const double visL = qMax(pr.left(),   dst.left());
    const double visR = qMin(pr.right(),  dst.right());
    const double visT = qMax(pr.top(),    dst.top());
    const double visB = qMin(pr.bottom(), dst.bottom());

    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont f = painter.font(); f.setPointSizeF(8.0); painter.setFont(f);

    // 축선(L자): plot 가장자리에 고정.
    painter.setPen(QPen(axisCol, 1.0));
    painter.drawLine(QPointF(pr.left(), pr.top()),    QPointF(pr.left(),  pr.bottom()));   // y축
    painter.drawLine(QPointF(pr.left(), pr.bottom()), QPointF(pr.right(), pr.bottom()));   // x축

    if (mBeatPeriodMs <= 0.0 || visR <= visL || visB <= visT) return;

    // ── y축: 비트 내 시간(ms) — 현재 보이는 행 범위로 눈금 산출 ──
    {
        const double topMs = yToRow(visT) / ih * mBeatPeriodMs;
        const double botMs = yToRow(visB) / ih * mBeatPeriodMs;
        const double step  = niceTickStep((botMs - topMs) / 6.0);
        const double startMs = std::ceil(topMs / step) * step;
        for (double ms = startMs; ms <= botMs + 1e-6; ms += step) {
            const double y = rowToY(ms / mBeatPeriodMs * ih);
            if (y < visT - 0.5 || y > visB + 0.5) continue;
            painter.setPen(QPen(tickCol, 1.0));
            painter.drawLine(QPointF(pr.left() - 4, y), QPointF(pr.left(), y));
            painter.setPen(axisCol);
            painter.drawText(QRectF(0, y - 7, kMarginLeft - 6, 14),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(ms, 'f', 0));
        }
        painter.setPen(axisCol);
        painter.save();
        painter.translate(11, pr.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-80, -10, 160, 14), Qt::AlignCenter,
                         QStringLiteral("time in beat (ms)"));
        painter.restore();
    }

    // ── x축: 가시 창 경과 시간(s) — 보이는 좌측 가장자리를 0 으로 ──
    {
        const double periodS = mBeatPeriodMs / 1000.0;   // 1 컬럼(=1 비트) = periodS 초
        const double colL = xToCol(visL);
        const double spanS = (xToCol(visR) - colL) * periodS;
        const double step  = niceTickStep(spanS / 7.0);
        for (double s = 0.0; s <= spanS + 1e-6; s += step) {
            const double x = colToX(colL + s / periodS);
            if (x < visL - 0.5 || x > visR + 0.5) continue;
            painter.setPen(QPen(tickCol, 1.0));
            painter.drawLine(QPointF(x, pr.bottom()), QPointF(x, pr.bottom() + 4));
            painter.setPen(axisCol);
            const QString lbl = (step < 1.0) ? QString::number(s, 'f', 1) : QString::number(s, 'f', 0);
            painter.drawText(QRectF(x - 24, pr.bottom() + 5, 48, 13),
                             Qt::AlignHCenter | Qt::AlignTop, lbl);
        }
        painter.setPen(axisCol);
        painter.drawText(QRectF(pr.left(), height() - 14, pr.width(), 13),
                         Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("time across window (s) →"));
    }
}

// 한 비트(컬럼) 안의 초록(A)·파랑(C) 마커 쌍을 찾는다(선택된 컬럼 x 에 가장 가까운 것).
bool SoundImageWidget::findPairAtColumn(double colX,
                                        SoundImageRenderer::OverlayMarker &green,
                                        SoundImageRenderer::OverlayMarker &blue) const
{
    bool hg = false, hb = false;
    double bg = 1e18, bb = 1e18;
    for (const SoundImageRenderer::OverlayMarker &m : mOverlayMarkers) {
        const int r = qRed(m.color), g = qGreen(m.color), b = qBlue(m.color);
        const double d = std::abs(m.x - colX);
        if (g > 128 && r < 128 && b < 128) { if (d < bg) { bg = d; green = m; hg = true; } }     // A=green
        else if (b > 128 && r < 128 && g < 128) { if (d < bb) { bb = d; blue = m; hb = true; } } // C=blue
    }
    // 같은 비트로 볼 수 있게 두 점의 컬럼이 충분히 가까울 때만 쌍 성립.
    return hg && hb && std::abs(green.x - blue.x) <= 2;
}

void SoundImageWidget::selectAt(const QPointF &widgetPos)
{
    if (mOverlayMarkers.empty()) { mHasSelection = false; update(); return; }
    const Viewport vp = computeViewport();
    if (vp.destRect.isEmpty() || vp.sourceRect.isEmpty() || !vp.destRect.contains(widgetPos)) {
        mHasSelection = false; update(); return;
    }
    const QPointF img = widgetToImage(widgetPos, vp);   // 위젯 → 이미지 좌표(px)

    // 클릭 위치에서 가장 가까운 마커(이미지 거리)를 찾아 그 컬럼을 선택.
    double best = 1e18; double selX = 0.0; bool found = false;
    for (const SoundImageRenderer::OverlayMarker &m : mOverlayMarkers) {
        const double dx = m.x - img.x(), dy = m.y - img.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; selX = m.x; found = true; }
    }
    // 너무 먼 곳(빈 영역) 클릭은 선택 해제. (이미지 px 기준 ~24px 반경)
    if (!found || best > 24.0 * 24.0) { mHasSelection = false; update(); return; }
    mSelColX = selX;
    mHasSelection = true;
    update();
}

// 선택한 비트의 초록(A)·파랑(C)을 점선으로 잇고 두 점 사이 시간(ms)을 표시.
void SoundImageWidget::drawSelection(QPainter &painter, const Viewport &vp) const
{
    if (!mHasSelection || vp.destRect.isEmpty() || vp.sourceRect.isEmpty()) return;
    SoundImageRenderer::OverlayMarker g{}, b{};
    if (!findPairAtColumn(mSelColX, g, b)) return;

    auto toWidget = [&](const SoundImageRenderer::OverlayMarker &m) {
        const double u = (m.x + 0.5 - vp.sourceRect.left()) / vp.sourceRect.width();
        const double v = (m.y + 0.5 - vp.sourceRect.top())  / vp.sourceRect.height();
        return QPointF(vp.destRect.left() + u * vp.destRect.width(),
                       vp.destRect.top()  + v * vp.destRect.height());
    };
    const QPointF pg = toWidget(g), pb = toWidget(b);

    painter.setClipRect(plotRect());
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 점선 연결선(흰색).
    QPen dash(QColor(245, 245, 245), 1.4, Qt::DashLine);
    painter.setPen(dash);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(pg, pb);

    // 선택 강조 링(초록·파랑 각각).
    painter.setPen(QPen(QColor(0, 255, 0), 1.6));
    painter.drawEllipse(pg, 5.5, 5.5);
    painter.setPen(QPen(QColor(80, 120, 255), 1.6));
    painter.drawEllipse(pb, 5.5, 5.5);
    painter.setClipping(false);

    // 간격(ms): 두 점의 행 차이를 비트 길이로 환산. (A onset → C drop)
    QString label = QStringLiteral("A↔C");
    if (mBeatPeriodMs > 0.0 && image && image->height() > 0) {
        const double dMs = std::abs((double)g.y - (double)b.y) / image->height() * mBeatPeriodMs;
        label = QStringLiteral("A→C: %1 ms").arg(dMs, 0, 'f', 2);
    }
    const QPointF mid = (pg + pb) * 0.5;
    QFont f = painter.font(); f.setPointSizeF(8.5); f.setBold(true); painter.setFont(f);
    const QRectF box(mid.x() + 8, mid.y() - 9, 96, 18);
    painter.fillRect(box, QColor(0, 0, 0, 160));
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(box, Qt::AlignVCenter | Qt::AlignLeft, label);
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
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const Viewport vp = computeViewport();
    if (!vp.destRect.contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }

    // 좌클릭 누름 추적: 이동 없으면 '클릭=점 선택', 이동하면 '드래그=팬'(확대 시).
    mLeftDown = true;
    mPressMoved = false;
    mPressWidgetPos = event->position();
    if (mViewScale > 1.0) {            // 팬은 확대 상태에서만
        mPanning = true;
        mPanStartWidget = event->position();
        mPanStartOffset = mViewOffset;
        setCursor(Qt::ClosedHandCursor);
    }
    event->accept();
}

void SoundImageWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (mLeftDown &&
        (event->position() - mPressWidgetPos).manhattanLength() > 3) {
        mPressMoved = true;           // 임계 이상 이동 → 클릭 아님(드래그)
    }

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
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const bool wasClick = mLeftDown && !mPressMoved;
    const bool wasPan   = mPanning && mPressMoved;
    mLeftDown = false;
    mPanning = false;

    if (wasClick) {
        selectAt(event->position());        // 클릭 = 비트 점 선택(초록↔파랑 간격 표시)
    } else if (wasPan) {
        mUserViewLocked = true;             // 드래그 = 팬 → 자동 추적 중단
    }
    setCursor(mViewScale > 1.0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    event->accept();
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
