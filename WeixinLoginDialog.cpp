#include "WeixinLoginDialog.h"
#include "qrcodegen.h"
#include <QtWidgets>
#include <QDataStream>

using namespace HttpRqst;

static QNetworkRequest makeWxRqst(const QString &baseUrl, const QUrlQuery &params) {
    QUrl url{baseUrl};
    url.setQuery(params);
    QNetworkRequest rqst{url};
    rqst.setRawHeader("Referer"_ba, "https://open.weixin.qq.com/"_ba);
    rqst.setRawHeader("User-Agent"_ba, HttpRqst::GetUserAgentPC());
    rqst.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, true);
    return rqst;
}

WeixinLoginDialog::WeixinLoginDialog(const QString &appId,
                                     const QString &redirectUri,
                                     const QString &state,
                                     QWidget *parent)
    : QDialog(parent),
      appId{appId}, redirectUri{redirectUri}, state{state}
{
    auto mainLayout = new QVBoxLayout{this};
    qrCodeLabel = new QLabel{this};
    qrCodeLabel->setFixedSize(163, 163);
    qrCodeLabel->setAlignment(Qt::AlignCenter);
    tipLabel= new QLabel{u"使用微信扫一扫登录"_s, this};
    tipLabel->setAlignment(Qt::AlignCenter);
    tipLabel->setMinimumWidth(220);
    auto font = tipLabel->font();
    font.setPointSize(11);
    tipLabel->setFont(font);

    refreshButton = new QToolButton{qrCodeLabel};
    refreshButton->setText(u"点击刷新"_s);
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    refreshButton->setStyleSheet(u"background-color: white; border: 2px solid #cccccc;"_s);
    refreshButton->setIconSize(QSize{32, 32});
    QPixmap refreshBtnIconPixmap{128, 128};
    const char *refreshIconSvg = R"(<?xml version="1.0" standalone="no"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
<svg t="1701349212465" class="icon" viewBox="0 0 1024 1024" version="1.1" xmlns="http://www.w3.org/2000/svg" p-id="1454" xmlns:xlink="http://www.w3.org/1999/xlink" width="200" height="200">
<path d="M512 128v85.333333c164.693333 0 298.666667 133.973333 298.666667 298.666667s-133.973333 298.666667-298.666667 298.666667-298.666667-133.973333-298.666667-298.666667c0-79.765333 31.04-154.773333 87.466667-211.178667l-60.352-60.352A382.826667 382.826667 0 0 0 128.042667 512c0 212.074667 171.925333 384 384 384 212.053333 0 384-171.925333 384-384S724.053333 128 512 128" fill="#000000" p-id="1455"></path>
<path d="M375.786667 380.608l-83.989334 15.146667-22.677333-125.973334L143.146667 292.48 128 208.490667 337.962667 170.666667z" fill="#000000" p-id="1456"></path>
</svg>)";
    refreshBtnIconPixmap.loadFromData(refreshIconSvg, "svg");
    refreshButton->setIcon(refreshBtnIconPixmap);
    refreshButton->setHidden(true);
    connect(refreshButton, &QToolButton::clicked, this, [this]() {
        hideRefreshButton();
        rqstLoginUrl();
    });

    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    mainLayout->addWidget(qrCodeLabel, 0, Qt::AlignCenter);
    mainLayout->addWidget(tipLabel, 0, Qt::AlignCenter);
    setLayout(mainLayout);
    setWindowTitle(u"微信登录"_s);

    rqstLoginUrl();
}

void WeixinLoginDialog::setAppName(const QString &name) {
    appName = name;
}

QString WeixinLoginDialog::getCode() const {
    return code;
}

void WeixinLoginDialog::resetTipLabel() {
    if (appName.isEmpty()) {
        tipLabel->setText(u"使用微信扫一扫登录"_s);
    } else {
        tipLabel->setText(u"使用微信扫一扫登录<br>“%1”"_s.arg(appName));
    }
}

WeixinLoginDialog::~WeixinLoginDialog() = default;


void WeixinLoginDialog::showRefreshButton() {
    refreshButton->setHidden(false);
    refreshButton->move(qrCodeLabel->rect().center() - refreshButton->rect().center());
}

void WeixinLoginDialog::hideRefreshButton() {
    refreshButton->setHidden(true);
}

void WeixinLoginDialog::rqstLoginUrl() {
    QUrlQuery params{
        {u"appid"_s, appId},
        {u"scope"_s, u"snsapi_login"_s},
        {u"redirect_uri"_s, redirectUri},
        {u"state"_s, state},
        {u"login_type"_s, u"jssdk"_s},
        {u"self_redirect"_s, u"true"_s},
        {u"styletype"_s, QString{}},
        {u"sizetype"_s, QString{}},
        {u"bgcolor"_s, QString{}},
        {u"rst"_s, QString{}},
        {u"style"_s, u"white"_s}
    };
    QNetworkRequest rqst = makeWxRqst(u"https://open.weixin.qq.com/connect/qrconnect"_s, params);
    QNetworkReply *r = getNamInst()->get(rqst);
    rqstAbortHandler.bindRqst(r, [this](QNetworkReply *r) {
        unique_ptr<RqstError> err = checkReplyError(r);
        if (err != nullptr) {
            showError();
            return;
        }
        QString content = QString::fromUtf8(r->readAll());
        auto m = QRegExp{uR"#(/connect/l/qrconnect\?uuid=([^"]+))#"_s}.match(content);
        if (!m.hasMatch()) {
            showError();
            return;
        }
        uuid = m.captured(1);
        setQrCode(u"https://open.weixin.qq.com/connect/confirm?uuid="_s + uuid);
        resetTipLabel();
        pollCnt = 0;
        pollStatus();
    });;
}

void WeixinLoginDialog::pollStatus() {
    if (pollCnt == 0) {
        firstPollTs = QDateTime::currentMSecsSinceEpoch();
    }
    pollCnt++;

    const QString baseUrl = u"https://lp.open.weixin.qq.com/connect/l/qrconnect"_s;
    QUrlQuery params;
    params.addQueryItem(u"uuid"_s, uuid);
    params.addQueryItem(u"_"_s, QString::number(firstPollTs + pollCnt));
    if (lastResult != 0) {
        params.addQueryItem(u"last"_s, QString::number(lastResult));
    };
    QNetworkRequest rqst = makeWxRqst(baseUrl, params);
    rqst.setTransferTimeout(60'000ms .count());
    QNetworkReply *r = getNamInst()->get(rqst);
    rqstAbortHandler.bindRqst(r, [this](QNetworkReply *r) {
        if (r->error() == QNetworkReply::OperationCanceledError) {
            // TransferTimeout
            QTimer::singleShot(5s, this, &WeixinLoginDialog::pollStatus);
            return;
        }
        unique_ptr<RqstError> err = checkReplyError(r);
        if (err != nullptr) {
            showError();
            return;
        }
        QString content = QString::fromUtf8(r->readAll());
        auto m = QRegExp{uR"#(window\.wx_errcode=([0-9]+);window\.wx_code='([^']*)';)#"_s}.match(content);
        if (!m.hasMatch()) {
            showError();
            return;
        }
        lastResult = 0;
        int ret = m.captured(1).toInt();
        switch (ret) {
        case 408:
            QTimer::singleShot(2s, this, &WeixinLoginDialog::pollStatus);
            break;
        case 404:
            lastResult = 404;
            showScanned();
            QTimer::singleShot(100ms, this, &WeixinLoginDialog::pollStatus);
            break;
        case 405:
            code = m.captured(2);
            accept();
            break;
        case 403:
            lastResult = 403;
            showCanceled();
            QTimer::singleShot(2s, this, &WeixinLoginDialog::pollStatus);
            break;
        case 402: case 500:
            showExpired();
            break;
        }
    });
}

void WeixinLoginDialog::showError() {
    tipLabel->setText(u"<h3>❌&nbsp;&nbsp;请求错误</h3>"_s);
    showRefreshButton();
}

void WeixinLoginDialog::showScanned() {
    tipLabel->setText(u"<h3>✅&nbsp;&nbsp;扫描成功</h3>"
                      "在微信中轻触允许即可登录"_s);
}

void WeixinLoginDialog::showCanceled() {
    tipLabel->setText(u"<h3>❌&nbsp;&nbsp;你已取消此次登录</h3>"
                      "你可再次扫描登录，或关闭窗口"_s);
}

void WeixinLoginDialog::showExpired() {
    // blur the QR code
    QPixmap pixmap = qrCodeLabel->pixmap();
    QPainter painter(&pixmap);
    painter.fillRect(QRect{QPoint{0, 0}, pixmap.size()}, QColor{255, 255, 255, 196});
    qrCodeLabel->setPixmap(pixmap);

    tipLabel->setText(u"二维码已失效"_s);
    showRefreshButton();
}

void WeixinLoginDialog::setQrCode(const QString &content) {
    using namespace qrcodegen;
    constexpr int W = 4;

    QrCode qr = QrCode::encodeText(content.toUtf8(), QrCode::Ecc::MEDIUM);
    int n = qr.getSize();

    QPixmap pixmap{n*W, n*W};
    QPainter painter{&pixmap};
    pixmap.fill(Qt::white);
    for (int row = 0; row < n; row++) {
        for (int col = 0; col < n; col++) {
            auto val = qr.getModule(col, row);
            if (val) {
                painter.fillRect(col*W, row*W, W, W, Qt::black);
            }
        }
    }

    qrCodeLabel->setFixedSize(pixmap.size());
    qrCodeLabel->setPixmap(pixmap);
}
