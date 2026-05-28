#include "SoundImageWidget.h"
#include <QPainter>

SoundImageWidget::SoundImageWidget(QWidget *parent)
    : QWidget{parent}
{}

void  SoundImageWidget::CreateImage(void)
{
    image=new QImage(size(), QImage::Format_ARGB32);
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

void SoundImageWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);


    // 3. Draw the image to the widget
    QRect targetRect = rect();
    //painter.drawImage(0, 0, image);
    painter.drawImage(targetRect, *image);
}
