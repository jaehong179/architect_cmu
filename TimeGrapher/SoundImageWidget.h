#ifndef SOUNDIMAGEWIDGET_H
#define SOUNDIMAGEWIDGET_H

#include <QWidget>

class SoundImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SoundImageWidget(QWidget *parent = nullptr);
    void CreateImage(void);
    void DrawImage(void);
    QImage * GetImage(void);
    void paintEvent(QPaintEvent *event) override;
private:
    QImage *image=nullptr;

signals:
};

#endif // SOUNDIMAGEWIDGET_H
