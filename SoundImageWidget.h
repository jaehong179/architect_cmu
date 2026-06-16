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
    void resizeEvent(QResizeEvent *event) override;
private:
    QImage *image=nullptr;

signals:
    void imageRecreated();   // resize 등으로 내부 QImage가 교체되면 통지(렌더러 재바인딩용)
};

#endif // SOUNDIMAGEWIDGET_H
