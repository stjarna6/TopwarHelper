#include "log.h"
#include "MainWindow.h"

static QString buf;

LogTextStream::LogTextStream(QString *s)
    : QTextStream{s, QIODeviceBase::WriteOnly}
{
}

LogTextStream::~LogTextStream() {
    getMainWindow()->appendToLog(buf);
    buf.resize(0);
}

LogTextStream log() {
    if (buf.isNull()) {
        buf.reserve(256);
    }
    buf += QDateTime::currentDateTime().toString(u"[yyyy/MM/dd hh:mm:ss] "_s);
    return LogTextStream{&buf};
}
