#pragma once

#include <QDialog>
#include "HttpRqst.h"

class QLabel;
class QTimer;
class QToolButton;
class QNetworkReply;

class WeixinLoginDialog : public QDialog
{
    Q_OBJECT

public:
    WeixinLoginDialog(
        const QString &appId,
        const QString &redirectUri,
        const QString &state,
        QWidget *parent = nullptr
    );
    ~WeixinLoginDialog() override;
    void setAppName(const QString &name);
    void setSrcUrl(const QString &url);
    QString getCode() const;

private:
    void resetTipLabel();
    void showRefreshButton();
    void hideRefreshButton();

    void rqstLoginUrl();
    void setQrCode(const QString &content);
    void pollStatus();
    void showError();
    void showScanned();
    void showCanceled();
    void showExpired();

    QString appId;
    QString redirectUri;
    QString state;
    QString appName;

    QString uuid;
    int lastResult{0};
    int pollCnt{0};
    int64_t firstPollTs;
    QString code;
    HttpRqst::AbortHandler rqstAbortHandler;

    QLabel *qrCodeLabel;
    QLabel *tipLabel;
    QToolButton *refreshButton;    
};
