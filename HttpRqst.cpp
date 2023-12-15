#include "HttpRqst.h"

static QThreadStorage<QNetworkAccessManager> perThreadNam;

namespace HttpRqst {

void init() {

}

QByteArray GetUserAgentPC() {
    return "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
           " AppleWebKit/537.36 (KHTML, like Gecko)"
           " Chrome/119.0.0.0"
           " Safari/537.36"
           " Edg/119.0.0.0"_ba;
}

QNetworkAccessManager* getNamInst() {
    return &perThreadNam.localData();
}

QNetworkReply* postUrlEncoded(QNetworkRequest rqst, const QByteArray &data) {
    rqst.setRawHeader("content-type"_ba, "application/x-www-form-urlencoded;charset=UTF-8"_ba);
    return getNamInst()->post(rqst, data);
}

QNetworkReply* postJson(QNetworkRequest rqst, const QByteArray &data) {
    rqst.setRawHeader("content-type"_ba, "application/json;charset=UTF-8"_ba);
    return getNamInst()->post(rqst, data);
}

QNetworkReply* postJson(QNetworkRequest rqst, const QJsonObject &obj) {
    rqst.setRawHeader("content-type"_ba, "application/json;charset=UTF-8"_ba);
    auto data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return getNamInst()->post(rqst, data);
}



unique_ptr<RqstError> makeStatusCodeError(int code, QString reasonPhase) {
    using enum RqstError::Reason;
    QString desc = QString::number(code).append(' ').append(reasonPhase);
    return make_unique<RqstError>(BadHttpStatusCode, desc);
}

unique_ptr<RqstError> makeNetworkError(QString errorString) {
    using enum RqstError::Reason;
    return make_unique<RqstError>(NetworkError, errorString);
}

unique_ptr<RqstError> makeApiRespError(QString errorString) {
    using enum RqstError::Reason;
    return make_unique<RqstError>(BadApiResponse, errorString);
}

QString RqstError::getDescription() const {
    QString ret;
    switch (reason) {
    case NetworkError:
        ret = u"网络错误"_s;
        break;
    case BadHttpStatusCode:
        ret = u"HTTP请求错误"_s;
        break;
    case BadApiResponse:
        ret = u"API请求错误"_s;
        break;
    default:
        ret = u"请求错误"_s;
        break;
    }

    if (detail.isEmpty()) {
        return ret;
    } else {
        return ret + u"："_s + detail;
    }
}

unique_ptr<RqstError> checkReplyError(QNetworkReply *reply) {
    int code = getStatusCode(reply);
    if (code >= 300) {
        QString reasonPhase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        return makeStatusCodeError(code, reasonPhase);
    }

    if (reply->error() != QNetworkReply::NoError) {
        return makeNetworkError(reply->errorString());
    }

    return nullptr;
}





} // END namespace HttpRqst
