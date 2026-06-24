#include "SoundImageWidget.h"
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <QWheelEvent>

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
    image->fill(Qt::white);
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

void SoundImageWidget::DrawImage(void)
{
    followLiveColumn();
    update();
}

SoundImageWidget::Viewport SoundImageWidget::computeViewport() const
{
    Viewport vp;
    if (!image || image->isNull()) return vp;

    const double iw = image->width();
    const double ih = image->height();
    const double ww = width();
    const double wh = height();
    if (iw <= 0.0 || ih <= 0.0 || ww <= 0.0 || wh <= 0.0) return vp;

    const double fitScale = qMin(ww / iw, wh / ih);
    vp.displayScale = fitScale * mViewScale;

    const QSizeF drawnSize(iw * vp.displayScale, ih * vp.displayScale);
    const QPointF topLeft((ww - drawnSize.width()) * 0.5, (wh - drawnSize.height()) * 0.5);
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
    painter.fillRect(rect(), Qt::white);

    if (!image || image->isNull()) return;

    const Viewport vp = computeViewport();
    if (vp.destRect.isEmpty() || vp.sourceRect.isEmpty()) return;

    painter.drawImage(vp.destRect, *image, vp.sourceRect);
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
