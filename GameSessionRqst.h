#pragma once

#include "HttpRqst.h"

struct GameSessionInfo {
    int serverId;
    QString serverUrl;
    QString serverInfoToken;
    QString tempId;
};

inline QDataStream& operator<< (QDataStream &out, const GameSessionInfo &session) {
    return out << session.serverId << session.serverUrl
               << session.serverInfoToken << session.tempId;
}

inline QDataStream& operator>> (QDataStream &in, GameSessionInfo &session) {
    return in >> session.serverId >> session.serverUrl
              >> session.serverInfoToken >> session.tempId;
}

QString generateTempId();

HttpRqst::RqstHandler<QString> rqstGameVersion();

HttpRqst::RqstHandler<unique_ptr<GameSessionInfo>> rqstSessionInfo(const QString &gameVer, const QString &token);
