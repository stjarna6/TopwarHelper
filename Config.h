#pragma once

#include <QJsonDocument>

namespace Config
{
constexpr int RunIntervalDefault = 60 * 60;

constexpr QLatin1StringView KeyWarzone{"warzone"};
constexpr QLatin1StringView KeyRunInterval{"RunInterval"};
constexpr QLatin1StringView KeyScienceDonatePrefer{"ScienceDonatePrefer"};
constexpr QLatin1StringView KeyDonateCoinConsume{"DonateCoinConsume"};
constexpr QLatin1StringView KeyWorldSiteDonatePrefer{"WorldSiteDonatePrefer"};

    void init();
    void save();

    const QJsonObject& get();
    const QJsonValue get(QLatin1StringView k);
    const QJsonValue get(QStringView k);
    void set(QLatin1StringView k, const QJsonValue &v);
    void set(QStringView k, const QJsonValue &v);
} // END namespace Config
