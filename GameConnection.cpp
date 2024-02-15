#include <QtEndian>
#include <QJsonDocument>
#include <cmath>
#include "GameConnection.h"
#include "HttpRqst.h"
#include "log.h"

constexpr int BinaryDataPackFormatJson = 0;
constexpr int BinaryDataPackFormatProtobuf = 1;

static void convert(int seq, int &format, QByteArray &data) {
    int seed = seq | 0x01010101;
    format ^= ((seed >> 24) & 0xff);
    for (int i = 0; i < data.size(); i++) {
        int j = 3 - (i + 1) % 4;
        data[i] ^= (seed >> (j * 8)) & 0xff;
    }
}

QByteArray AppMessage::encoded() {
    QByteArray binData = QJsonDocument{data}.toJson(QJsonDocument::Compact);
    int fmtConverted = format;
    convert(seq, std::ref(fmtConverted), std::ref(binData));

    if (int dataSize = binData.size(); dataSize > 0) {
        binData.prepend(13, 0);
        qToBigEndian<int32_t>(rqstId, binData.data());
        qToBigEndian<int32_t>(seq, binData.data() + 4);
        qToBigEndian<int32_t>(dataSize+1, binData.data() + 8);
        binData[12] = static_cast<char>(fmtConverted);
    } else {
        binData.resize(12);
        qToBigEndian<int32_t>(rqstId, binData.data());
        qToBigEndian<int32_t>(seq, binData.data() + 4);
        qToBigEndian<int32_t>(0, binData.data() + 8);
    }
    return binData;
}

std::optional<AppMessage> AppMessage::decode(QBuffer &buffer, int &bytesRequired)
{
    bytesRequired = 12;
    if (buffer.bytesAvailable() < 12) {
        return {};
    }

    AppMessage appMsg;
    QByteArray data = buffer.peek(12);
    appMsg.rqstId = qFromBigEndian<int32_t>(data.data());
    appMsg.seq = qFromBigEndian<int32_t>(data.data() + 4);
    int len = qFromBigEndian<int32_t>(data.data() + 8);
    if (len == 0) {
        buffer.skip(12);
        return appMsg;
    }
    if (buffer.bytesAvailable() < 12 + len) {
        bytesRequired = 12 + len;
        return {};
    }
    buffer.skip(12);
    data = buffer.read(len);
    appMsg.format = (int)data[0] & 0xff;
    data.remove(0, 1);
    convert(appMsg.seq, std::ref(appMsg.format), std::ref(data));
    if (appMsg.format == BinaryDataPackFormatJson) {
        appMsg.data = QJsonDocument::fromJson(data).object();
    }
    return appMsg;
}



GameConnection::GameConnection() {}

GameConnection::GameConnection(const QString &gameVer, const GameSessionInfo &sessionInfo)
    : gameVersion{gameVer}, sessionInfo{sessionInfo}, webSock{u"https://warh5.rivergame.net"_s}
{
    connect(&webSock, &QWebSocket::connected, this, [this] {
        sendLogin();
        heartbeatTimer.start();
    });

    connect(&webSock, &QWebSocket::disconnected, this, [this] {
        if (isClosedByServer) {
            log() << userDesc() << u"服务器关闭了连接"_s;
        } else if (getWarzone() != 0) {
            log() << userDesc() << u"断开连接"_s;
        }

        if (changeServerSession == nullptr) {
            emit connectionClosed();
            return;
        }

        if (userInfo[u"isCross"_s].toInt() == 1) {
            reConnect(changeServerSession->serverUrl);
            changeServerSession.reset();
        }  else {
            reConnect(changeServerSession->serverUrl);
            this->sessionInfo = std::move(*changeServerSession);
            changeServerSession.reset();
        }
    });

    connect(&webSock, &QWebSocket::errorOccurred, this, [](auto err) {
        qDebug() << "error occurred" << err;
    });

    connect(&webSock, &QWebSocket::binaryMessageReceived,
            this, &GameConnection::processBinaryMessage);

    webSock.open(sessionInfo.serverUrl + u"?b=1"_s);



    registerCallback(TopwarRqstId::NO_QUEUE_HEART, [](const QJsonObject &resp) {
        qDebug() << QDateTime::currentDateTime() << "recv heartbeat resp" << resp;
    });
    registerCallback(TopwarRqstId::LOGIN, [this](auto &&resp){ recvLoginResponse(resp); });
    registerCallback(TopwarPushId::USER_DISCONNECT, [this](auto &&resp){
        isClosedByServer = true;
        webSock.close();
    });
    registerCallback(TopwarPushId::PUSH_RESOURCE, [this](auto &&resp){ recvUpdateResource(resp); });
    registerCallback(TopwarPushId::BUILDING_INFO_LIST, [this](auto &&resp){ recvUpdateBuildingInfo(resp); });


    heartbeatTimer.setSingleShot(false);
    heartbeatTimer.setInterval(heartbeatInterval);
    heartbeatTimer.callOnTimeout(this, &GameConnection::sendHeartbeat);
}

void GameConnection::reConnect(const QString &serverUrl) {
    callbackBySeq.clear();
    rqstCnt = 0;
    nextBytesRequired = 0;
    webSock.open(serverUrl + u"?b=1"_s);
}

const GameSessionInfo& GameConnection::getSessionInfo() const {
    return sessionInfo;
}

QWebSocket& GameConnection::getWebSocket() {
    return webSock;
}

SteadyTimepoint GameConnection::getLastRqstTimepoint() const {
    return lastRqstTimepoint;
}

milliseconds GameConnection::getLastServerTime() const {
    return lastServerTime;
}

const QJsonObject &GameConnection::getUserInfo() const {
    return userInfo;
}

int GameConnection::getWarzone() const {
    return userInfo[u"k"_s].toInt();
}

QString GameConnection::getUsername() const {
    return userInfo[u"username"_s].toString();
}

int64_t GameConnection::getAllianceId() const {
    return userInfo[u"allianceInfo"_s][u"aid"_s].toInteger();
}

QString GameConnection::userDesc() const {
    return u"[S%1/%2] "_s.arg(getWarzone()).arg(getUsername());
}

void GameConnection::sendRequest(int rqstId, const QJsonObject &rqstData,
                                 ResponseCallback callback) {
    if (!webSock.isValid()) {
        return;
    }
    rqstCnt++;
    int seq = rqstCnt;
    AppMessage msg;
    msg.rqstId = rqstId;
    msg.data = rqstData;
    msg.seq = seq;
    msg.format = BinaryDataPackFormatJson;
    if (callback) {
        callbackBySeq[seq] = std::move(callback);
    }
    webSock.sendBinaryMessage(msg.encoded());
    if (rqstId != TopwarRqstId::NO_QUEUE_HEART) {
        lastRqstTimepoint = SteadyClockNow();
    }
}

void GameConnection::registerCallback(int rqstId, ResponseCallback callback) {
    callbackByRqstId[rqstId] = std::move(callback);
}

void GameConnection::processBinaryMessage(const QByteArray &data) {
    recvBuffer.append(data);
    if (recvBuffer.size() < nextBytesRequired) {
        return;
    }

    QBuffer buf{&recvBuffer};
    buf.open(QIODevice::ReadOnly);
    optional<AppMessage> msg = AppMessage::decode(buf, std::ref(nextBytesRequired));
    if (!msg.has_value()) {
        return;
    }
    recvBuffer.remove(0, buf.pos());

    if (msg->format == BinaryDataPackFormatProtobuf) {
        qDebug() << "unhandled protobuf msg";
        return;
    }

    lastServerTime = milliseconds{msg->data[u"t"_s].toInteger()};
    auto respData = QJsonDocument::fromJson(msg->data[u"d"_s].toString().toUtf8()).object();
    if (respData.isEmpty()) {
        respData = msg->data;
    }
    if (msg->data[u"s"_s].toInt() == 3) {
        qDebug() << "wss recv error resp." << "seq:" << msg->seq
                 << "rqstId:" << rqstIdToString(msg->rqstId) << "content:" << respData;
        return; // currently error callback is not provided
    }
    if (msg->rqstId == -1) {
        qDebug() << "wss recv error msg." << "seq:" << msg->seq
                 << "rqstId:" << rqstIdToString(msg->rqstId) << "content:" << respData;
        return;
    }

    if (auto it = callbackBySeq.find(msg->seq); it != callbackBySeq.end()) {
        auto &callback = it->second;
        if (callback) {
            callback(respData);
        }
        callbackBySeq.erase(it);
    } else if (auto it = callbackByRqstId.find(msg->rqstId); it != callbackByRqstId.end()) {
        auto &callback = it->second;
        if (callback) {
            callback(respData);
        }
    } else {
        // qDebug() << "unhandled message." << "seq:" << msg->seq
        //          << "rqstId:" << rqstIdToString(msg->rqstId) << "content:" << respData;;
    }
}

void GameConnection::sendHeartbeat() {
    sendRequest(TopwarRqstId::NO_QUEUE_HEART, {});
}

void GameConnection::sendLogin() {
    int serverId = sessionInfo.serverId;
    if (userInfo[u"isCross"_s].toInt() == 1) {
        serverId = userInfo[u"sid"_s].toInt();
    }
    QJsonObject args{
        {u"token"_s, sessionInfo.serverInfoToken},
        {u"platformVer"_s, gameVersion},
        {u"appVersion"_s, gameVersion},
        {u"pbClientVer"_s, gameVersion},
        {u"serverId"_s, serverId},
        {u"serverInfoToken"_s, sessionInfo.serverInfoToken},
        {u"temp_id"_s, sessionInfo.tempId},
        {u"ua"_s, QString{HttpRqst::GetUserAgentPC()}},
        {u"country"_s, u"CN"_s},
        {u"lang"_s, u"zh_cn"_s},
        {u"nationalFlag"_s, 48},
        {u"ip"_s, u"0"_s},
        {u"pf"_s, u"web_pc"_s},
        {u"platform"_s, u"webgame"_s},
        {u"channel"_s, u"webgame_webgameCn"_s},
        {u"containerType"_s, u"web"_s},
        {u"gaid"_s, u""_s},
        {u"share_uid"_s, u""_s},
        {u"itemId"_s, u""_s},
        {u"rvflag"_s, 0},
        {u"launchPrams"_s, uR"({"query":{"channel":"webgame_webgameCn"}})"_s},
    };
    sendRequest(TopwarRqstId::LOGIN, args, [](auto &&resp) { qDebug() << "recv login resp" << resp; } );
}

void GameConnection::recvLoginResponse(const QJsonObject &resp) {
    userInfo = resp;
    if (userInfo[u"isCross"_s].toInt() == 1) {
        changeServerSession = make_unique<GameSessionInfo>(sessionInfo);
        changeServerSession->serverId = resp[u"sid"_s].toInt();
        changeServerSession->serverUrl = resp[u"wsurl"_s].toString();
        webSock.close();
        return;
    }

    log() << userDesc() << u"连接成功"_s;

    constexpr int AutoCollectBuildingId = 1801;
    for (const auto &obj : resp[u"buildings"_s].toArray()) {
        if (obj[u"buildingId"_s].toInt() == AutoCollectBuildingId) {
            autoCollectMachineId = obj[u"id"_s].toString();
            break;
        }
    }

    for (const auto &obj : resp[u"energy"_s].toArray()) {
        int energyType = obj[u"type"_s].toInt();
        int currentPoint = obj[u"point"_s].toInt();

        switch (energyType) {
        case EnergyType::AllianceDonateNum:
            allianceDonateNum = currentPoint;
            break;
        case EnergyType::AllianceDonateGoldNum:
            allianceDonateGoldNum = currentPoint;
            break;
        case EnergyType::AllianceWorldSiteDonateNum:
            allianceWorldSiteDonateNum = currentPoint;
            break;
        }
    }
    emit loginSucceeded();
}

void GameConnection::sendGetUserServerList(ResponseCallback callback) {
    QJsonObject data{
        {u"channel"_s, u"webgame"_s},
        {u"devPlatform"_s, u"webgame"_s},
        {u"lineAddress"_s, u""_s}
    };
    sendRequest(TopwarRqstId::GET_USER_SERVERLIST, data, std::move(callback));
}

void GameConnection::sendChangeServer(int serverId, int64_t uid, ResponseCallback callback) {
    QJsonObject data{
        {u"deviceType"_s, u"wxMiniProgram"_s},
        {u"isUnion"_s, 1},
        {u"serverId"_s, serverId},
        {u"serverInfoToken"_s, sessionInfo.serverInfoToken},
        {u"uid"_s, QString::number(uid)},
    };
    sendRequest(TopwarRqstId::CHANGE_SERVER, data, callback);
}

void GameConnection::changeServer(int wantedServerId) {
    sendGetUserServerList([this, wantedServerId](auto &&resp) {
        int64_t uid = 0;
        for (const auto &obj : resp[u"serverList"_s].toArray()) {
            if (obj[u"serverId"_s].toInt() == wantedServerId) {
                uid = obj[u"uid"_s].toInteger();
                break;
            }
        }
        if (uid == 0) {
            log() << u"切换战区：用户在战区 S"_s << wantedServerId << u"无账号"_s;
            return;
        }

        changeServerSession = make_unique<GameSessionInfo>();
        changeServerSession->serverId = wantedServerId;
        for (const auto &obj : resp[u"showServerList"_s][u"serverList"_s].toArray()) {
            if (obj[u"id"_s].toInt() == wantedServerId) {
                changeServerSession->serverUrl = obj[u"url"_s].toString();
                break;
            }
        }
        changeServerSession->tempId = sessionInfo.tempId;
        sendChangeServer(wantedServerId, uid, [this](auto &&resp) {
            changeServerSession->serverInfoToken = resp[u"serverInfoToken"_s].toString();
            changeServerReConnect();
        });
    });
}

void GameConnection::changeServerReConnect() {
    heartbeatTimer.stop();
    webSock.close(); // reconnect in the slot of QWebSocket::disconnected
}

void GameConnection::recvUpdateResource(const QJsonObject &resp) {
    userInfo[u"resource"_s] = resp;
}

void GameConnection::recvUpdateBuildingInfo(const QJsonObject &resp) {
    for (const auto &&obj : resp[u"updateBuilds"_s].toArray()) {
        QString buildId = obj[u"id"_s].toString();
        for (const auto &&armyId : obj[u"productIds"_s].toArray()) {
            armyBuildingMap[armyId.toString()] = buildId;
        }
    }
    if (consumeTarget != 0.0) {
        QTimer::singleShot(20ms, this, [this]{ sendDeleteTrainingArmy(); });
    }
}

void GameConnection::sendGetAllianceScienceInfo(ResponseCallback callback) {
    sendRequest(TopwarRqstId::ALLIANCE_GET_SCIENCE, {}, std::move(callback));
}

void GameConnection::sendAllianceDonateScience(int scienceId, int times) {
    QJsonObject data{
        {u"scienceId"_s, scienceId},
        {u"type"_s, 1},
        {u"num"_s, times}
    };
    sendRequest(TopwarRqstId::ALLIANCE_DOANTE_SCIENCE, data, [this, scienceId, times](auto &&resp) {
        log() << userDesc() << u"捐献联盟科技「"_s
              << AllianceScience::toString(scienceId) << u"」"_s
              << times << u"次"_s;
    });
}

void GameConnection::donateAllianceScience(int scienceId) {
    if (scienceId == AllianceScience::快速作战) {
        for (int i = 0; i < allianceDonateGoldNum / 10 + 1; i++) {
            QTimer::singleShot(100ms * i, this, [this, scienceId]{
                sendAllianceDonateScience(scienceId, 10);
            });
        }
    } else {
        for (int i = 0; i < allianceDonateNum; i++) {
            QTimer::singleShot(100ms * i, this, [this, scienceId]{
                sendAllianceDonateScience(scienceId, 1);
            });
        }
    }
}

void GameConnection::sendGetWorldSiteInfo(ResponseCallback callback) {
    QJsonObject data{{u"worldId"_s, sessionInfo.serverId}};
    sendRequest(TopwarRqstId::GET_WORLD_SITE_DATA, data, std::move(callback));
}

void GameConnection::donateWorldSite(int siteId) {
    for (int i = 0; i < allianceWorldSiteDonateNum; i++) {
        QTimer::singleShot(100ms * i, this, [this, siteId] {
            QJsonObject data{
                {u"id"_s, siteId},
                {u"type"_s, 1}
            };
            sendRequest(TopwarRqstId::WORLDSITE_DONATE, data, [this, siteId](auto &&resp) {
                auto&& kindStr = WorldSite::kindToString(WorldSite::siteToKind(siteId));
                log() << userDesc() << u"捐献「遗迹-"_s << kindStr << u"」1次"_s;
            });
        });
    }
}

void GameConnection::executeAutoCollectMachine() {
    if (autoCollectMachineId.isEmpty()) {
        return;
    }
    sendRequest(TopwarRqstId::GET_ORDER, {{u"id"_s, autoCollectMachineId}}, [this](auto &&resp) {
        log() << userDesc() << u"收取金币收割机，获得 "_s
              << formatNumber(resp[u"reward"_s][u"resource"_s][u"coin"_s].toDouble())
              << u" 金币"_s;
    });
}

void GameConnection::obtainVideoReward() {
    QJsonObject data{
        {u"type"_s, 8},
        {u"param1"_s, u"1"_s},
        {u"param2"_s, u""_s}
    };
    sendRequest(TopwarRqstId::VideoRewardGet, data, [this](auto &&resp) {
        int obtainedTimesToday = resp[u"dayGoldVideoCount"_s].toInt();
        userInfo[u"dayGoldVideoCount"_s] = obtainedTimesToday;
        log() << userDesc() << u"获取观影奖励 "_s
              << resp[u"resource"_s][u"resource"_s][u"gold"_s].toInt() << u" 钻石"_s
              << u"（今日已获取"_s << obtainedTimesToday << u"/20）"_s;
    });
}

void GameConnection::obtainSecretTreasure() {
    sendRequest(TopwarRqstId::ShareRewardBoxReceive, {}, [this](auto &&resp) {
        int gold = resp[u"reward"_s][u"resource"_s][u"gold"_s].toInt();
        double coin = resp[u"reward"_s][u"resource"_s][u"coin"_s].toDouble();
        int obtainedTimesToday = resp[u"secretTreasure"_s].toInt();
        userInfo[u"secretTreasure"_s] = obtainedTimesToday;
        log() << userDesc() << u"获取神秘奖励 "_s
              << (gold != 0 ? (QString::number(gold) + u" 钻石"_s) : (formatNumber(coin) + u" 金币"_s))
              << u"（今日已获取"_s << obtainedTimesToday << u"/5）"_s;
    });
}

void GameConnection::consumeCoinByTrainArmy(QJsonObject bathBuildData, double coinToconsume) {
    if (consumeTarget != 0) {
        log() << userDesc() << u"添加金币消耗任务失败：已在执行中"_s;
        return;
    }
    this->bathBuildData = bathBuildData;
    consumeTarget = userInfo[u"resource"_s][u"coin"_s].toDouble() - coinToconsume;
    sendBatchBuild();
}

void GameConnection::obtainAwardExploreSea(int activityId, int slotIdx, bool restart) {
    QJsonObject data{
        {u"aid"_s, activityId},
        {u"index"_s, slotIdx}
    };
    sendRequest(TopwarRqstId::AWARD_EXPLORE_SEA, data, [=, this](auto &&resp) {
        log() << userDesc() << u"深海寻宝：收取"_s << slotIdx + 1 << u"号位"_s;
        if (restart) {
            startExploreSea(activityId, slotIdx);
        }
    });
}

void GameConnection::startExploreSea(int activityId, int slotIdx) {
    QJsonObject data{
        {u"aid"_s, activityId},
        {u"index"_s, slotIdx}
    };
    sendRequest(TopwarRqstId::START_EXPLORE_SEA, data, [this, slotIdx](auto &&resp) {
        log() << userDesc() << u"深海寻宝："_s << slotIdx + 1 << u"号位开始探索"_s;
    });
}

void GameConnection::sendDeleteTrainingArmy() {
    QJsonArray armyList;
    for (const auto& [armyId, buildId] : armyBuildingMap) {
        QJsonObject t{
          {u"armyid"_s, armyId},
          {u"buildingid"_s, buildId},
          {u"groupid"_s, 1040},
        };
        armyList.append(std::move(t));
    }

    QJsonObject data{{u"cancel"_s, armyList}};
    sendRequest(TopwarRqstId::ARMY_CANCEL_PRODUCE_ALL, data, [this](auto &&resp) {
        armyBuildingMap.clear();
        QTimer::singleShot(20ms, this, [this]{ sendBatchBuild(); });
    });
}

void GameConnection::sendBatchBuild() {
    double coin = userInfo[u"resource"_s][u"coin"_s].toDouble();
    log() << userDesc() << u"当前金币："_s << formatNumber(coin, 4);
    if (coin > consumeTarget) {
        sendRequest(TopwarRqstId::BATCH_BUILD_ORDER, bathBuildData);
    } else {
        consumeTarget = 0;
        log() << userDesc() << u"训练完成"_s;
    }
}


void GameConnection::sendGetActivityData(ResponseCallback callback) {
    sendRequest(TopwarRqstId::GET_ACTIVITY_DATA, {}, std::move(callback));
}
