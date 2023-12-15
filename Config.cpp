#include <QtCore>
#include "Config.h"
#include "MainWindow.h"


static QJsonObject* data = nullptr;
static QTimer* saveTimer = nullptr;
constexpr auto ConfigSaveRelPath = "config.json";

static void doSave() {
    QString filePath = QDir{QCoreApplication::applicationDirPath()}.filePath(ConfigSaveRelPath);
    QFile configFile{filePath};
    if (!configFile.open(QIODevice::WriteOnly)) {
        qDebug() << "failed to open session save file";
        return;
    }
    configFile.write(QJsonDocument{*data}.toJson());
}

void Config::init() {
    data = new QJsonObject{};
    saveTimer = new QTimer{getMainWindow()};
    saveTimer->setSingleShot(true);
    saveTimer->callOnTimeout(getMainWindow(), doSave);

    QString filePath = QDir{QCoreApplication::applicationDirPath()}.filePath(ConfigSaveRelPath);
    QFile configFile{filePath};
    if (configFile.open(QIODevice::ReadOnly)) {
        *data = QJsonDocument::fromJson(configFile.readAll()).object();
    }

    if (!data->contains(KeyWarzone)) {
        data->insert(KeyWarzone, 0);
    }
    if (!data->contains(KeyRunInterval)) {
        data->insert(KeyRunInterval, 60*60);
    }
    if (!data->contains(KeyScienceDonatePrefer)) {
        data->insert(KeyScienceDonatePrefer, 0);
    }
    if (!data->contains(KeyDonateCoinConsume)) {
        data->insert(KeyDonateCoinConsume, false);
    }
    if (!data->contains(KeyWorldSiteDonatePrefer)) {
        data->insert(KeyWorldSiteDonatePrefer, 0);
    }
}

void Config::save() {
    if (!saveTimer->isActive()) {
        saveTimer->start(3s);
    }
}

const QJsonObject &Config::get() {
    return *data;
}

const QJsonValue Config::get(QLatin1StringView k) {
    return data->value(k);
}

const QJsonValue Config::get(QStringView k) {
    return data->value(k);
}

void Config::set(QLatin1StringView k, const QJsonValue &v) {
    data->insert(k, v);
    save();
}

void Config::set(QStringView k, const QJsonValue &v) {
    data->insert(k, v);
    save();
}
