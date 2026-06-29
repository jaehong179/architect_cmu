#include "MeasurementUploadClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QUrl>

QString MeasurementUploadClient::baseUrl()
{
    return QStringLiteral(
        "https://i5dhq7t6fb.execute-api.us-east-1.amazonaws.com/default/timegrapher_api");
}

static QString parseErrorMessage(const QByteArray &body, const QString &fallback)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return fallback;

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        QString message = obj.value(QStringLiteral("error")).toString();
        if (obj.contains(QStringLiteral("missing_fields"))) {
            const QJsonArray missing = obj.value(QStringLiteral("missing_fields")).toArray();
            QStringList fields;
            for (const QJsonValue &value : missing)
                fields.append(value.toString());
            if (!fields.isEmpty())
                message += QStringLiteral(" (missing: %1)").arg(fields.join(QStringLiteral(", ")));
        }
        return message;
    }

    if (obj.contains(QStringLiteral("message")))
        return obj.value(QStringLiteral("message")).toString();

    return fallback;
}

MeasurementUploadClient::UploadResult MeasurementUploadClient::uploadMeasurement(
    const QString &watchId, const QString &engineer, const QJsonObject &measurements)
{
    UploadResult result;

    QJsonObject payload;
    payload[QStringLiteral("watch_id")] = watchId.trimmed();
    payload[QStringLiteral("engineer")] = engineer.trimmed();
    if (!measurements.isEmpty())
        payload[QStringLiteral("measurements")] = measurements;

    QNetworkAccessManager nam;
    const QUrl url(baseUrl());
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=utf-8"));

    QEventLoop loop;
    QNetworkReply *reply = nam.post(request,
                                    QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        result.success = false;
        result.message = body.isEmpty()
                             ? reply->errorString()
                             : parseErrorMessage(body, reply->errorString());
    } else if (result.httpStatus >= 200 && result.httpStatus < 300) {
        result.success = true;
        result.message = parseErrorMessage(body, QStringLiteral("save complete"));
    } else {
        result.success = false;
        result.message = parseErrorMessage(body,
                                           QStringLiteral("HTTP %1").arg(result.httpStatus));
    }

    reply->deleteLater();
    return result;
}
