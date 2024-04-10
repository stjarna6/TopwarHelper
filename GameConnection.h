#pragma once

#include <QWebSocket>
#include <QJsonObject>
#include <QTimer>
#include <QBuffer>
#include "common.h"
#include "GameSessionRqst.h"
#include "TopwarIds.h"


class AppMessage {
public:
    int seq;
    int rqstId;
    int format;
    QJsonObject data;

    QByteArray encoded();
    static optional<AppMessage> decode(QBuffer &buffer, int &bytesRequired);
};


class GameConnection: public QObject
{
    Q_OBJECT

public:
    GameConnection();
    GameConnection(const QString &gameVer, const GameSessionInfo &sessionInfo);

    const GameSessionInfo& getSessionInfo() const;
    QWebSocket& getWebSocket();
    SteadyTimepoint getLastRqstTimepoint() const;
    milliseconds getLastServerTime() const;
    const QJsonObject& getUserInfo() const;
    int getWarzone() const;
    QString getUsername() const;
    int64_t getAllianceId() const;

    using ResponseCallback = std::function<void(const QJsonObject &resp)>;
    void sendRequest(int rqstId, const QJsonObject &rqstData, ResponseCallback callback={});
    void registerCallback(int rqstId, ResponseCallback callback);

    void sendGetUserServerList(ResponseCallback callback);
    void sendChangeServer(int serverId, int64_t uid, ResponseCallback callback);
    void sendGetAllianceScienceInfo(ResponseCallback callback);
    void sendAllianceDonateScience(int scienceId, int times);
    void sendGetWorldSiteInfo(ResponseCallback callback);
    void sendGetActivityData(ResponseCallback callback);
    void sendNotifyWxShare(ResponseCallback callback);

    void changeServer(int wantedServerId);
    void donateAllianceScience(int scienceId);
    void donateWorldSite(int siteId);
    void executeAutoCollectMachine();
    void obtainVideoReward();
    void obtainSecretTreasure();
    void consumeCoinByTrainArmy(QJsonObject bathBuildData, double coinToconsume);
    void obtainAwardExploreSea(int activityId, int slotIdx, bool restart = true);
    void startExploreSea(int activityId, int slotIdx);
    void obtainWxShareReward();

signals:
    void loginSucceeded();
    void connectionClosed();

private:
    QString userDesc() const;

    void sendLogin();
    void sendHeartbeat();
    void processBinaryMessage(const QByteArray &msg);
    void recvLoginResponse(const QJsonObject &resp);
    void reConnect(const QString &serverUrl);
    void changeServerReConnect();
    void recvUpdateResource(const QJsonObject &resp);
    void recvUpdateBuildingInfo(const QJsonObject &resp);
    void sendDeleteTrainingArmy();
    void sendBatchBuild();

private:
    QString gameVersion;
    GameSessionInfo sessionInfo;
    QWebSocket webSock;
    QByteArray recvBuffer;

    milliseconds heartbeatInterval{10'000ms};
    QTimer heartbeatTimer;
    bool connected{false};
    bool isClosedByServer{false};

    int rqstCnt{0};
    int nextBytesRequired{0};
    SteadyTimepoint lastRqstTimepoint;
    milliseconds lastServerTime;
    std::map<int, ResponseCallback> callbackBySeq;
    std::map<int, ResponseCallback> callbackByRqstId;

    QJsonObject userInfo;
    unique_ptr<GameSessionInfo> changeServerSession;

    QString autoCollectMachineId;
    int allianceDonateNum{0};
    int allianceDonateGoldNum{0};
    int allianceWorldSiteDonateNum{0};

    QJsonObject bathBuildData;
    double consumedCoin{0};
    double consumeTarget{0};
    std::map<QString, QString> armyBuildingMap;
};
