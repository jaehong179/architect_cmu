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
    static UploadResult uploadMeasurement(const QString &watchId,
                                          const QString &engineer,
                                          const QJsonObject &measurements);
};

#endif // MEASUREMENTUPLOADCLIENT_H
