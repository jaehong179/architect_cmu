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

QString MeasurementUploadClient::webViewerBaseUrl()
{
    // [QR] 휴대폰으로 스캔하면 열리는 웹 이력 뷰어(프론트엔드).
    return QStringLiteral("https://timegrapher-history.vercel.app/");
}

QString MeasurementUploadClient::viewerUrl(const QString &watchId)
{
    // [QR] <프론트>?watch_id=<percent-encoded id>
    const QString encoded =
        QString::fromUtf8(QUrl::toPercentEncoding(watchId.trimmed()));
    return webViewerBaseUrl() + QStringLiteral("?watch_id=") + encoded;
}

static QString toUserFriendlyNetworkError(QNetworkReply::NetworkError error, const QString &errorString)
{
    switch (error) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::HostNotFoundError:
            return QStringLiteral("Unable to connect to the server. Please check your internet connection and try again later.");
        case QNetworkReply::TimeoutError:
            return QStringLiteral("Connection timed out. The network might be unstable. Please try again later.");
        case QNetworkReply::OperationCanceledError:
            return QStringLiteral("Upload request was canceled. Please try again later.");
        case QNetworkReply::SslHandshakeFailedError:
            return QStringLiteral("Secure connection (SSL/TLS) failed. Please check your system time setting and try again later.");
        case QNetworkReply::ContentAccessDenied:
        case QNetworkReply::ContentOperationNotPermittedError:
            return QStringLiteral("Access denied. You do not have permission to upload data to the server. Please try again later.");
        case QNetworkReply::ContentNotFoundError:
            return QStringLiteral("Upload server path not found. Please try again later.");
        default:
            return QStringLiteral("A network error occurred. Please try again later. (Details: %1)").arg(errorString);
    }
}

static QString toUserFriendlyHttpStatusError(int httpStatus)
{
    if (httpStatus >= 500) {
        return QStringLiteral("An internal server error occurred. Please try again later. (HTTP %1)").arg(httpStatus);
    }
    switch (httpStatus) {
        case 400:
        case 422:
            return QStringLiteral("Invalid request data. Please check your input information and try again. (HTTP %1)").arg(httpStatus);
        case 401:
        case 403:
            return QStringLiteral("Access denied. You do not have permission to upload. Please try again later. (HTTP %1)").arg(httpStatus);
        case 404:
            return QStringLiteral("Upload server path not found. Please try again later. (HTTP %1)").arg(httpStatus);
        default:
            return QStringLiteral("A server error occurred. Please try again later. (HTTP %1)").arg(httpStatus);
    }
}

static QString translateFieldName(const QString &field)
{
    if (field == QStringLiteral("watch_id")) return QStringLiteral("Watch ID");
    if (field == QStringLiteral("engineer")) return QStringLiteral("Engineer Name");
    if (field == QStringLiteral("measurements")) return QStringLiteral("Measurement Data");
    return field;
}

static QString parseErrorMessage(const QByteArray &body, const QString &fallback)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return fallback;

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        QString message = obj.value(QStringLiteral("error")).toString();
        if (message == QStringLiteral("Validation failed")) {
            message = QStringLiteral("Input validation failed.");
        }
        
        if (obj.contains(QStringLiteral("missing_fields"))) {
            const QJsonArray missing = obj.value(QStringLiteral("missing_fields")).toArray();
            QStringList fields;
            for (const QJsonValue &value : missing)
                fields.append(translateFieldName(value.toString()));
            if (!fields.isEmpty())
                message += QStringLiteral(" (Missing: %1)").arg(fields.join(QStringLiteral(", ")));
        }

        if (!message.contains(QStringLiteral("Please try again later"))) {
            if (message.endsWith(QStringLiteral("."))) {
                message += QStringLiteral(" Please try again later.");
            } else {
                message += QStringLiteral(". Please try again later.");
            }
        }
        return message;
    }

    if (obj.contains(QStringLiteral("message"))) {
        QString msg = obj.value(QStringLiteral("message")).toString();
        if (msg == QStringLiteral("Internal server error")) {
            return QStringLiteral("An internal server error occurred. Please try again later.");
        }
        if (!msg.contains(QStringLiteral("Please try again later"))) {
            if (msg.endsWith(QStringLiteral("."))) {
                msg += QStringLiteral(" Please try again later.");
            } else {
                msg += QStringLiteral(". Please try again later.");
            }
        }
        return msg;
    }

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
                             ? toUserFriendlyNetworkError(reply->error(), reply->errorString())
                             : parseErrorMessage(body, toUserFriendlyNetworkError(reply->error(), reply->errorString()));
    } else if (result.httpStatus >= 200 && result.httpStatus < 300) {
        result.success = true;
        result.message = parseErrorMessage(body, QStringLiteral("Upload complete."));
    } else {
        result.success = false;
        result.message = parseErrorMessage(body, toUserFriendlyHttpStatusError(result.httpStatus));
    }

    reply->deleteLater();
    return result;
}
