#include "TopwarHelper.h"
#include "MainWindow.h"
#include "Config.h"
#include "log.h"

constexpr auto SessionSaveRelPath = "topwarSession.dat";

constexpr auto ReservedLoginTime = 5s;
constexpr auto KeepAliveTime = 10s; // close connection after idle
constexpr auto TaskMaxPendingTime = 120s;

constexpr int DailyTaskId = 0;


TopwarHelper::TopwarHelper() {
    doRqstGameVersion();

    logoutTimer.setSingleShot(true);
    logoutTimer.callOnTimeout(this, &TopwarHelper::logoutAndScheduleLogin);

    loginTimer.setSingleShot(true);
    loginTimer.callOnTimeout(this, [this]{ loginBySession(readSavedSession());});

    runTaskTimer.setSingleShot(true);
    runTaskTimer.callOnTimeout(this, &TopwarHelper::runTask);
}

void TopwarHelper::doRqstGameVersion() {
    auto rqstHandler = rqstGameVersion();
    httpRqstAborter.bindRqst(rqstHandler);
    rqstHandler.whenFinished(
        [this](QString result) {
            qDebug() << "version:" << result;
            gameVersion = result;
            if (!pendingLoginToken.isEmpty()) {
                loginByToken(pendingLoginToken);
                pendingLoginToken.clear();
            }
            if (pendingLoginSession != nullptr) {
                loginBySession(std::move(pendingLoginSession));
            }
        },
        [](auto &&err) {
            qDebug() << "failed to get game version." << err.getDescription();
        }
    );
}

unique_ptr<GameSessionInfo> TopwarHelper::readSavedSession() {
    QString filePath = QDir{QCoreApplication::applicationDirPath()}.filePath(SessionSaveRelPath);
    QFile sessionSaveFile{filePath};
    if (!sessionSaveFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    QDataStream in(&sessionSaveFile);
    in.setByteOrder(QDataStream::LittleEndian);
    auto session = make_unique<GameSessionInfo>();
    in >> *session;
    if (in.status() == QDataStream::ReadCorruptData) {
        return {};
    }
    return session;
}

void TopwarHelper::saveSession() {
    QString filePath = QDir{QCoreApplication::applicationDirPath()}.filePath(SessionSaveRelPath);
    QFile sessionSaveFile{filePath};
    if (!sessionSaveFile.open(QIODevice::WriteOnly)) {
        qDebug() << "failed to open session save file";
        return;
    }
    QDataStream out(&sessionSaveFile);
    out.setByteOrder(QDataStream::LittleEndian);
    GameSessionInfo session = conn->getSessionInfo();
    out << session;
}

void TopwarHelper::loginByToken(const QString &token) {
    if (gameVersion.isEmpty()) {
        pendingLoginToken = token;
        if (!httpRqstAborter.isValid()) {
            doRqstGameVersion();
        }
        return;
    }

    rqstSessionInfo(gameVersion, token).whenFinished(
        [this](unique_ptr<GameSessionInfo> session) {
            qDebug() << session->serverId
                     << "serverToken:" << session->serverInfoToken
                     << "temp_id:" << session->tempId;
            loginBySession(std::move(session));
        },
        [](auto &&err) {
            qDebug() << "failed to get game session." << err.getDescription();
        }
    );
}

void TopwarHelper::loginBySession(unique_ptr<GameSessionInfo> session) {
    // if login fails, we can still run later
    loginTimer.start(seconds{Config::get(Config::KeyRunInterval).toInt(120)});

    if (gameVersion.isEmpty()) {
        pendingLoginSession = std::move(session);
        if (!httpRqstAborter.isValid()) {
            doRqstGameVersion();
        }
        return;
    }

    conn = make_unique<GameConnection>(gameVersion, *session);
    connect(conn.get(), &GameConnection::connectionClosed, this, [this] {
        QTimer::singleShot(0, this, [this] {
            runTaskTimer.stop();
            logoutTimer.stop();
            conn.reset();
        });
    });
    connect(conn.get(), &GameConnection::loginSucceeded, this, [this] {
        getMainWindow()->showUserInfo(conn->getWarzone(), conn->getUsername());
        if (conn->getWarzone() == 0) {
            log() << u"登录失败，问题待解决"_s;
            return;
        }

        saveSession();

        int wantedWarzone = Config::get(Config::KeyWarzone).toInt();
        if (wantedWarzone != 0 && conn->getWarzone() != wantedWarzone) {
            conn->changeServer(wantedWarzone);
            return;
        }

        loginTimer.stop();
        currLoginTaskQueue = {};
        if (!scheduleTaskMap.contains(DailyTaskId)) {
            doDailyTasks();
        }
        if (!runTaskTimer.isActive()) {
            runTask();
        }
    });
}

void TopwarHelper::logoutAndScheduleLogin() {
    if (conn == nullptr) {
        return;
    }

    auto &ws = conn->getWebSocket();
    if (!ws.isValid()) {
        loginTimer.start(TaskMaxPendingTime);
        conn.reset();
        return;
    }

    if (SteadyClockNow() < conn->getLastRqstTimepoint() + KeepAliveTime) {
        QTimer::singleShot(0, this, &TopwarHelper::runTask);
        return;
    }

    gameVersion = QString{};
    ws.close();

    auto nextScheduleTime = SteadyClockMax;
    for (const auto& [id, task] : scheduleTaskMap) {
        nextScheduleTime = std::min(nextScheduleTime, task.time);
    }
    if (nextScheduleTime != SteadyClockMax) {
        auto t = (nextScheduleTime - SteadyClockNow()) - ReservedLoginTime;
        loginTimer.start(std::max(DurationCast::round<seconds>(t), 120s));
    }
}

void TopwarHelper::onWantedWarzoneChanged(int warzone) {
    currLoginTaskQueue = {};
    scheduleTaskMap.clear();
    if (conn == nullptr || !conn->getWebSocket().isValid()) {
        loginTimer.stop();
        loginBySession(readSavedSession());
    } else {
        loginTimer.stop();
        logoutTimer.stop();
        runTaskTimer.stop();
        conn->changeServer(warzone);
    }
}

void TopwarHelper::addTask(milliseconds t, std::function<void()> callback) {
    currLoginTaskQueue.emplace(SteadyClockNow() + t, std::move(callback));
    taskAdded(t);
}

void TopwarHelper::addScheduleTask(int id, milliseconds t, std::function<void()> callback) {
    scheduleTaskMap[id] = Task{SteadyClockNow() + t, std::move(callback)};
    taskAdded(t);
}

void TopwarHelper::taskAdded(milliseconds t) {
    if (conn != nullptr && conn->getWebSocket().isValid() && t < TaskMaxPendingTime) {
        if (logoutTimer.isActive()) {
            logoutTimer.stop();
            runTaskTimer.start(t);
        } else if (!runTaskTimer.isActive() || t < runTaskTimer.remainingTimeAsDuration()) {
            runTaskTimer.start(t);
        }
    } else {
        t = std::max(t - ReservedLoginTime, 0ms);
        if (!loginTimer.isActive() || t < loginTimer.remainingTimeAsDuration()) {
            loginTimer.start(t);
        }
    }
}

void TopwarHelper::runTask() {
    if (!conn->getWebSocket().isValid()) {
        logoutTimer.start(0);
    }

    auto now = SteadyClockNow();
    SteadyTimepoint nextTaskTime = SteadyClockMax;

    while (!currLoginTaskQueue.empty()) {
        auto &task = currLoginTaskQueue.top();
        if (task.time < now + 10ms) {
            task.callback();
            currLoginTaskQueue.pop();
        } else {
            nextTaskTime = task.time;
            break;
        }
    }

    for (auto it = scheduleTaskMap.begin(); it != scheduleTaskMap.end(); ) {
        auto &[id, task] = *it;
        if (task.time < now + 10ms) {
            task.callback();
            it = scheduleTaskMap.erase(it);
        } else {
            if (task.time < now + TaskMaxPendingTime) {
                nextTaskTime = std::min(nextTaskTime, task.time);
            }
            it++;
        }
    }

    if (nextTaskTime == SteadyClockMax) {
        logoutTimer.start(KeepAliveTime);
    } else {
        auto t = nextTaskTime - SteadyClockNow();
        runTaskTimer.start(std::max(DurationCast::round<milliseconds>(t), 0ms));
    }
}

void TopwarHelper::consumeCoin(QByteArray batchBuildData, double coin) {
    QJsonObject bathBuildDataObj = QJsonDocument::fromJson(batchBuildData).object();
    addScheduleTask(TopwarRqstId::BATCH_BUILD_ORDER, 0s, [this, bathBuildDataObj, coin]() {
        conn->consumeCoinByTrainArmy(bathBuildDataObj, coin);
    });
}

void TopwarHelper::doDailyTasks() {
    QTimer::singleShot(0, this, [this] {
        seconds interval{Config::get(Config::KeyRunInterval).toInt(120)};
        addScheduleTask(DailyTaskId, interval, [this]{ doDailyTasks(); });
    });

    checkActivity();

    addTask(1s, [this]{ conn->executeAutoCollectMachine(); });

    for (int i = conn->getUserInfo()[u"secretTreasure"_s].toInt(); i < 5; i++) {
        addTask(1s+100ms*i, [this]{ conn->obtainSecretTreasure(); });
    }

    if (conn->getUserInfo()[u"dayGoldVideoCount"_s].toInt() < 20) {
        addTask(2s, [this]{ conn->obtainVideoReward(); });
        addTask(32s, [this]{ conn->obtainVideoReward(); });
    }
    if (conn->getAllianceId() != 0) {
        doDailyAllianceTasks();
    }
}

void TopwarHelper::doDailyAllianceTasks() {
    struct WorldSite_t {
        int siteId;
        int kind;
        int level;
        int exp;
    };
    addTask(3s, [this] {
        conn->sendGetWorldSiteInfo([this](auto &&resp) {
            int preferred = Config::get(Config::KeyWorldSiteDonatePrefer).toInt();
            int64_t userAid = conn->getAllianceId();
            std::vector<WorldSite_t> candidates;
            candidates.reserve(8);
            for (const auto &obj : resp[u"worldWonders"_s].toArray()) {
                if (obj[u"activityAid"_s].toInteger() == userAid) {
                    WorldSite_t s;
                    s.siteId = obj[u"id"_s].toInt();
                    s.kind = WorldSite::siteToKind(s.siteId);
                    s.level = obj[u"level"_s].toInt();
                    s.exp = obj[u"exp"_s].toInt();
                    if (s.kind == preferred) {
                        s.kind = 0;
                    } else if (s.kind == WorldSite::种类::道具 && s.level >= 4) {
                        s.kind = std::numeric_limits<int>::max();
                    }
                    candidates.push_back(s);
                }
            }
            if (candidates.empty()) {
                return;
            }
            auto choice = std::ranges::min(candidates, [](const auto &a, const auto &b) -> bool {
                if (a.kind == b.kind) {
                    return (a.level == b.level ? a.exp > b.exp : a.level < b.level);
                }
                return a.kind < b.kind;
            });
            conn->donateWorldSite(choice.siteId);
        });
    });

    addTask(4s, [this] {
        conn->sendGetAllianceScienceInfo([this](auto &&resp) {
            int recommended = 0;
            int preferred = Config::get(Config::KeyScienceDonatePrefer).toInt();
            int candidate = 0;
            for (const auto &obj : resp[u"scs"_s].toArray()) {
                int id = obj[u"sid"_s].toInt();
                if (obj[u"recommend"_s].toBool()) {
                    recommended = id;
                }
                if (obj[u"lv"_s].toInt() >= AllianceScience::getMaxLevel(id)
                    || obj[u"endTime"_s].toInteger() != 0 || obj[u"status"_s].toInteger() != 0)
                {
                    if (recommended == id) {
                        recommended = 0;
                    }
                    if (preferred == id) {
                        preferred = 0;
                    }
                } else {
                    candidate = id;
                }
            }
            if (preferred == 0) {
                preferred = recommended;
            }
            if (preferred == 0) {
                preferred = candidate;
            }
            if (preferred != 0) {
                conn->donateAllianceScience(preferred);
            }
        });
    });

    if (QDate::currentDate().dayOfWeek() == 1 && Config::get(Config::KeyDonateCoinConsume).toBool()) {
        addTask(6s, [this]{ conn->donateAllianceScience(AllianceScience::快速作战); });
    }
}

void TopwarHelper::checkActivity() {
    conn->sendGetActivityData([this](auto &&resp){
        for (const auto &obj : resp[u"alist"_s].toArray()) {
            QString name = obj[u"showUiType"_s].toString();
            if (name == u"ActivityDeepSeaTreasure"_s) {
                checkDeepSeaTreasure(obj.toObject());
            }
        }
    });
}

void TopwarHelper::checkDeepSeaTreasure(const QJsonObject &obj) {
    int aid = obj[u"id"_s].toInt();
    std::vector<int64_t> slotEndTime;
    slotEndTime.reserve(3);
    for (const auto &slot : obj[u"extra"_s][u"slots"_s].toArray()) {
        slotEndTime.push_back(slot[u"et"_s].toInteger());
    }

    seconds nextTime = seconds::max();
    auto serverTime = conn->getLastServerTime();
    for (int i = 0; i < slotEndTime.size(); i++) {
        seconds endTime{slotEndTime[i]};
        if (endTime.count() == 0) {
            conn->startExploreSea(aid, i);
        } else if (endTime <= serverTime) {
            conn->obtainAwardExploreSea(aid, i);
        } else {
            nextTime = std::min(nextTime, endTime);
        }
    }

    if (nextTime != seconds::max()) {
        auto t = nextTime - serverTime;
        addScheduleTask(TopwarRqstId::AWARD_EXPLORE_SEA, t, [this]{ checkActivity(); });
    }
}
