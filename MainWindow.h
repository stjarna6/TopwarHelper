#pragma once

#include <QMainWindow>
#include "TopwarHelper.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void appendToLog(const QString &s);
    void showUserInfo(int warzone, const QString &username);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void openChangeServerDialog();
    void openConsumeCoinDialog();

    Ui::MainWindow *ui;
    bool isForcedClose{false};
    unique_ptr<TopwarHelper> topwarHelper;
};

extern MainWindow* getMainWindow();
