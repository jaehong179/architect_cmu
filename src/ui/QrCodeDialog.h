#ifndef QRCODEDIALOG_H
#define QRCODEDIALOG_H
// =============================================================================
//  QrCodeDialog — 측정 기록 웹 이력 QR 코드 표시(공용)
// -----------------------------------------------------------------------------
//  url(웹 뷰어 주소)을 QR 코드 이미지로 렌더해 화면 가운데 모달로 띄운다.
//  두 곳에서 재사용: ① 업로드 성공 직후 자동, ② 사이드바 'QR' 버튼(평상시 이력).
//  의존성: Qt Widgets/Gui + 번들된 qrcodegen 뿐.
// =============================================================================
#include <QDialog>
#include <QImage>
#include <QString>

class QrCodeDialog : public QDialog
{
    Q_OBJECT
public:
    //  url      : QR 이 인코딩할 웹 주소
    //  watchId  : 화면에 함께 표기할 Watch ID
    //  headline : 상단 제목(예: "Upload complete", "Scan to view history")
    explicit QrCodeDialog(const QString &url, const QString &watchId,
                          const QString &headline, QWidget *parent = nullptr);

private:
    //  url 을 QR 코드 흑백 QImage 로 렌더(quiet zone 포함). 실패 시 빈 QImage.
    static QImage renderQr(const QString &url, int targetPx);
};

#endif // QRCODEDIALOG_H
