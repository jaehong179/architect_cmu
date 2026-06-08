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
    // Recreate image to match new widget size.
    // Note: the SoundImageRenderer must be re-initialized via Reset() after a resize.
    delete image;
    image = nullptr;
    if (width() > 0 && height() > 0) {
        image = new QImage(size(), QImage::Format_ARGB32);
        image->fill(Qt::white);
    }
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
