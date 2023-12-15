#include "GameSessionRqst.h"

using namespace HttpRqst;

static QNetworkRequest makeTwRqst(const QString &url) {
    QNetworkRequest rqst{QUrl{url}};
    rqst.setRawHeader("Referer"_ba, "https://warh5.rivergame.net/webgame/index.html"_ba);
    rqst.setRawHeader("User-Agent"_ba, HttpRqst::GetUserAgentPC());
    rqst.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, true);
    return rqst;
}

static QNetworkRequest makeTwRqst(const QString &baseUrl, const QUrlQuery &params) {
    QUrl url{baseUrl};
    url.setQuery(params);
    QNetworkRequest rqst{url};
    rqst.setRawHeader("Referer"_ba, "https://warh5.rivergame.net/webgame/index.html"_ba);
    rqst.setRawHeader("User-Agent"_ba, HttpRqst::GetUserAgentPC());
    rqst.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, true);
    return rqst;
}

QString generateTempId() {
    static QRandomGenerator random{(uint32_t)QDateTime::currentMSecsSinceEpoch()};
    static auto hex = u"0123456789abcdef";
    QString s = u"xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"_s;
    for (auto &ch : s) {
        if (ch == 'x') {
            int t = random.bounded(0, 16);
            ch = hex[t];
        } else if (ch == 'y') {
            int t = (random.bounded(0, 16) & 3) | 8;
            ch = hex[t];
        }
    }
    return s;
}

RqstHandler<QString> getSettingsJsUrl() {
    QString url = u"https://warh5.rivergame.net/webgame/index.html?t="_s
                  + QString::number(QDateTime::currentSecsSinceEpoch() / 120);
    auto r = getNamInst()->get(makeTwRqst(url));
    return {r, [](QNetworkReply *r) -> Expected<QString> {
        unique_ptr<RqstError> err = checkReplyError(r);
        if (err != nullptr) {
            return std::move(err);
        }
        QString content = QString::fromUtf8(r->readAll());
        auto m = QRegExp{uR"#("src/(settings\.[0-9a-z]+\.js)")#"_s}.match(content);
        if (!m.hasMatch()) {
            return make_unique<RqstError>();
        }
        return u"https://warh5.rivergame.net/webgame/src/"_s + m.captured(1);
    }};
}

RqstHandler<QString> rqstMainJsVersion(const QString &settingsJsurl) {
    auto r = getNamInst()->get(makeTwRqst(settingsJsurl));
    return {r, [](QNetworkReply *r) -> Expected<QString> {
        unique_ptr<RqstError> err = checkReplyError(r);
        if (err != nullptr) {
            return std::move(err);
        }
        QString content = QString::fromUtf8(r->readAll());
        auto m = QRegExp{uR"#(main"?\s*:\s*"([0-9a-z]+)")#"_s}.match(content);
        if (!m.hasMatch()) {
            return make_unique<RqstError>();
        }
        return m.captured(1);
    }};
}

RqstHandler<QString> rqstGameVersionFromJs(const QString &mainJsVersion) {
    QString url = u"https://warh5.rivergame.net/webgame/assets/main/index."_s
                  + mainJsVersion + u".js"_s;
    auto r = getNamInst()->get(makeTwRqst(url));
    return {r, [](QNetworkReply *r) -> Expected<QString> {
        unique_ptr<RqstError> err = checkReplyError(r);
        if (err != nullptr) {
            return std::move(err);
        }
        QString content = QString::fromUtf8(r->readAll());
        auto m = QRegExp{uR"#(app_version"?\s*:\s*"([^"]+)")#"_s}.match(content);
        if (!m.hasMatch()) {
            return make_unique<RqstError>();
        }
        return m.captured(1);
    }};
}

RqstHandler<QString> rqstGameVersion() {
    return getSettingsJsUrl()
        .thenRqst(rqstMainJsVersion)
        .thenRqst(rqstGameVersionFromJs)
    ;
}

RqstHandler<unique_ptr<GameSessionInfo>> rqstSessionInfo(const QString &gameVer, const QString &token) {
    QString baseUrl = u"https://serverlist-knight.rivergame.net/appServerListServlet"_s;
    QUrlQuery params{
        {u"__ts__"_s, QString::number(QDateTime::currentMSecsSinceEpoch())},
        {u"token"_s, token},
        {u"pf"_s, u"web_pc"_s},
        {u"platform"_s, u"webgame"_s},
        {u"channel"_s, u"webgame_webgameCn"_s},
        {u"appVersion"_s, gameVer},
        {u"tag"_s, u"1"_s},
        {u"rvflag"_s, u"0"_s},
        {u"lang"_s, u"zh_cn"_s},
        {u"systemCountryCode"_s, u"CN"_s},
        {u"systemLang"_s, u"zh_cn"_s},
        {u"code"_s, token}
    };
    auto r = getNamInst()->get(makeTwRqst(baseUrl, params));
    return {r, [](QNetworkReply *r) -> Expected<unique_ptr<GameSessionInfo>> {
        unique_ptr<RqstError> err = checkReplyError(r);
        if (err != nullptr) {
            return std::move(err);
        }
        const QJsonObject res = QJsonDocument::fromJson(r->readAll()).object();
        QString serverInfoToken = res[u"serverInfoToken"_s].toString();
        if (serverInfoToken.isEmpty()) {
            return make_unique<RqstError>();
        }
        auto session = make_unique<GameSessionInfo>();
        session->serverId = res[u"serverId"_s].toInt();
        session->serverUrl = res[u"url"_s].toString();
        session->serverInfoToken = serverInfoToken;
        session->tempId = generateTempId();
        return session;
    }};
}
