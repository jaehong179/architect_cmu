#include "SoundImageWidget.h"
#include <QPainter>

SoundImageWidget::SoundImageWidget(QWidget *parent)
    : QWidget{parent}
{}

void  SoundImageWidget::CreateImage(void)
{
    delete image;
    image = nullptr;
    if (width() <= 0 || height() <= 0) return;
    image = new QImage(size(), QImage::Format_ARGB32);
    image->fill(Qt::white);
}

QImage * SoundImageWidget::GetImage(void)
{
    return image;
}

void  SoundImageWidget::DrawImage(void)
{
    update();
    return;
    image->fill(Qt::black); // Clear screen

    // 2. Manipulate raw pixels (Fast method)
    int width = image->width();
    int height = image->height();
    for (int y = 0; y < height; ++y) {
        // Get pointer to current line
        QRgb *line = reinterpret_cast<QRgb*>(image->scanLine(y));
        for (int x = 0; x < width; ++x) {
            // Example: Create a gradient effect
            line[x] = qRgba(x % 255, y % 255, (x+y) % 255, 255);
        }
    }

}

void SoundImageWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 크기가 실제로 바뀐 경우에만 재생성한다. (Qt는 레이아웃 중 같은 크기로 resizeEvent를
    //  여러 번 보내므로, 무조건 재생성하면 sound print가 매번 리셋되어 느려 보인다.)
    if (image && image->size() == size()) return;
    delete image;
    image = nullptr;
    if (width() > 0 && height() > 0) {
        image = new QImage(size(), QImage::Format_ARGB32);
        image->fill(Qt::white);
    }
    // QImage 포인터가 교체됐으므로, 옛 포인터를 캐싱한 렌더러를 재바인딩하도록 통지한다.
    emit imageRecreated();
}

void SoundImageWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    if (!image || image->isNull()) {
        painter.fillRect(rect(), Qt::white);
        return;
    }
    // 3. Draw the image to the widget
    QRect targetRect = rect();
    painter.drawImage(targetRect, *image);
}
