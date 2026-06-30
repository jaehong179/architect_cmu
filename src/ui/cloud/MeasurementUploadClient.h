#ifndef MEASUREMENTUPLOADCLIENT_H
#define MEASUREMENTUPLOADCLIENT_H

#include <QJsonObject>
#include <QString>

class MeasurementUploadClient
{
public:
    struct UploadResult {
        bool success = false;
        int httpStatus = 0;
        QString message;
    };

    static QString baseUrl();

    // [QR] 사람이 보는 웹 이력 뷰어 주소(프론트엔드). baseUrl()(JSON API)과 별개.
    static QString webViewerBaseUrl();
    // [QR] 특정 Watch ID 의 이력 페이지 URL — QR 코드가 인코딩하는 값.
    static QString viewerUrl(const QString &watchId);

    static UploadResult uploadMeasurement(const QString &watchId,
                                          const QString &engineer,
                                          const QJsonObject &measurements);
};

#endif // MEASUREMENTUPLOADCLIENT_H
