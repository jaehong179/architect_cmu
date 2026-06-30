#include "QrCodeDialog.h"

#include "qrcodegen.hpp"

#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

QImage QrCodeDialog::renderQr(const QString &url, int targetPx)
{
    using qrcodegen::QrCode;

    // MEDIUM(~15% 오류정정) — 인쇄/화면 스캔 모두 무난.
    const QByteArray utf8 = url.toUtf8();
    const QrCode qr = QrCode::encodeText(utf8.constData(), QrCode::Ecc::MEDIUM);

    const int size = qr.getSize();          // 모듈 수(가로=세로)
    const int quiet = 4;                    // 권장 quiet zone(모듈)
    const int total = size + quiet * 2;     // 여백 포함 모듈 수

    // 정수배 스케일로 또렷하게(흐림 없는 픽셀 격자).
    int scale = targetPx / total;
    if (scale < 1) scale = 1;
    const int dim = total * scale;

    QImage img(dim, dim, QImage::Format_RGB32);
    img.fill(Qt::white);                    // 배경 + quiet zone = 흰색
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qr.getModule(x, y))
                continue;                   // 밝은 모듈은 흰 배경 유지
            const int px = (x + quiet) * scale;
            const int py = (y + quiet) * scale;
            for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx)
                    img.setPixel(px + dx, py + dy, qRgb(0, 0, 0));
        }
    }
    return img;
}

QrCodeDialog::QrCodeDialog(const QString &url, const QString &watchId,
                           const QString &headline, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Measurement Record QR"));
    setModal(true);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(14);

    auto *title = new QLabel(headline, this);
    title->setAlignment(Qt::AlignHCenter);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 15px; color: #f4f4f4;"));
    lay->addWidget(title);

    // QR 이미지 — 흰 카드 위에 얹어 어두운 테마에서도 스캔 잘 되게.
    auto *qrLabel = new QLabel(this);
    qrLabel->setAlignment(Qt::AlignCenter);
    const QImage qrImg = renderQr(url, 300);
    if (!qrImg.isNull()) {
        qrLabel->setPixmap(QPixmap::fromImage(qrImg));
    } else {
        qrLabel->setText(QStringLiteral("Failed to generate QR code"));
        qrLabel->setStyleSheet(QStringLiteral("color: #ff6b6b;"));
    }
    qrLabel->setStyleSheet(qrLabel->styleSheet() +
                           QStringLiteral(" background:#ffffff; border-radius:6px; padding:12px;"));
    lay->addWidget(qrLabel, 0, Qt::AlignHCenter);

    auto *watchLabel = new QLabel(QStringLiteral("Watch ID: %1").arg(watchId.trimmed()), this);
    watchLabel->setAlignment(Qt::AlignHCenter);
    watchLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: #ffffff;"));
    lay->addWidget(watchLabel);

    auto *hint = new QLabel(
        QStringLiteral("Scan with your phone camera to view the record on the web."), this);
    hint->setAlignment(Qt::AlignHCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #9e9e9e; font-size: 11px;"));
    lay->addWidget(hint);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignHCenter);

    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #181818; color: #f4f4f4; }"
        "QPushButton { background-color: #2f2f2f; color: #fff; border: 1px solid #4a4a4a; padding: 6px 18px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a3a3a; }"
        "QPushButton:pressed { background-color: #252525; }"
    ));
}
