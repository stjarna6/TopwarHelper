#pragma once

#include "GameConnection.h"
#include <queue>

template <class T>
using MinHeap = std::priority_queue<T, std::vector<T>, std::greater<T>>;

class TopwarHelper: public QObject
{
    Q_OBJECT

public:
    TopwarHelper();

    unique_ptr<GameSessionInfo> readSavedSession();
    void saveSession();

    void loginByToken(const QString &token);
    void loginBySession(unique_ptr<GameSessionInfo> session);
    void onWantedWarzoneChanged(int warzone);
    void doDailyTasks();
    void doDailyAllianceTasks();
    void consumeCoin(QByteArray batchBuildData, double coin);

    using SteadyTimepoint = std::chrono::time_point<std::chrono::steady_clock>;
    struct Task {
        SteadyTimepoint time;
        std::function<void()> callback;

        Task() = default;

        Task(SteadyTimepoint t, const std::function<void()> &cb)
            : time{t}, callback{cb} {}

        Task(SteadyTimepoint t, std::function<void()> &&cb)
            : time{t}, callback{std::move(cb)} {}

        friend auto operator <=> (const Task& l, const Task& r) {
            return l.time <=> r.time;
        }
    };

private:
    void doRqstGameVersion();
    void logoutAndScheduleLogin();
    void addTask(milliseconds t, std::function<void()> callback);
    // Note: do not call this function directly in the task callback of the same id
    void addScheduleTask(int id, milliseconds t, std::function<void()> callback);
    void taskAdded(milliseconds t);
    void runTask();

    void checkActivity();
    void checkDeepSeaTreasure(const QJsonObject &obj);

    HttpRqst::AbortHandler httpRqstAborter;
    QString gameVersion;
    QString pendingLoginToken;
    unique_ptr<GameSessionInfo> pendingLoginSession;

    MinHeap<Task> currLoginTaskQueue;
    std::map<int, Task> scheduleTaskMap;

    unique_ptr<GameConnection> conn;
    QTimer logoutTimer;
    QTimer loginTimer;
    QTimer runTaskTimer;
};
