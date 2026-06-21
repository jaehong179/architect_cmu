#ifndef SOUNDIMAGEWIDGET_H
#define SOUNDIMAGEWIDGET_H
// 폴딩 사운드 이미지 표시 위젯.
//  캔버스(QImage)는 '고정 크기'로 한 번 생성하고(위젯 크기와 독립), paintEvent 가 위젯
//  영역에 맞춰 스케일링해 그린다. 위젯 resize 가 캔버스를 재생성하지 않으므로
//  (1) 렌더러가 캐싱한 포인터가 무효화되지 않고(use-after-free 방지),
//  (2) 탭 전환 시 누적 이미지가 보존되어 warmup/anchor 재시작·앞부분 손실이 없다.
#include <QWidget>

class SoundImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SoundImageWidget(QWidget *parent = nullptr);
    ~SoundImageWidget() override;                    // 캔버스(image) 해제 — 위젯 파괴 시 누수 방지
    void     CreateImage(int width, int height);   // 고정 크기 캔버스 생성(위젯 크기와 무관). 1회 호출.
    void     DrawImage(void);                       // 재그리기 요청(update)
    QImage  *GetImage(void);                         // 렌더러가 그릴 대상 캔버스
    void     paintEvent(QPaintEvent *event) override;
private:
    QImage  *image = nullptr;
};

#endif // SOUNDIMAGEWIDGET_H
