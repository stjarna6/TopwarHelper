#pragma once

#include <QDebug>
#include <QTextStream>

class LogTextStream: public QTextStream {
public:
    LogTextStream(QString *s);
    ~LogTextStream() override;
};

extern LogTextStream log();
