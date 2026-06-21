#include "SoundImageWidget.h"
#include <QPainter>

SoundImageWidget::SoundImageWidget(QWidget *parent)
    : QWidget{parent}
{}

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
}

QImage * SoundImageWidget::GetImage(void)
{
    return image;
}

void  SoundImageWidget::DrawImage(void)
{
    update();
}

void SoundImageWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    if (!image || image->isNull()) {
        painter.fillRect(rect(), Qt::white);
        return;
    }
    // 고정 캔버스를 위젯 영역에 맞춰 스케일링해 그린다.
    painter.drawImage(rect(), *image);
}
