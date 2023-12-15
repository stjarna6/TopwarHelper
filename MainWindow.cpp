#include <QtWidgets>

#include "MainWindow.h"
#include "./ui_mainwindow.h"
#include "WeixinLoginDialog.h"
#include "GameSessionRqst.h"
#include "TopwarIds.h"
#include "Config.h"

static MainWindow *mainwindow;

MainWindow* getMainWindow() {
    return mainwindow;
}

MainWindow::~MainWindow() {}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    mainwindow = this;
    ui->setupUi(this);
    Config::init();
    const QJsonObject &currentConfig = Config::get();

    {
        int currentVal = currentConfig[Config::KeyRunInterval].toInt();
        pair<seconds, QString> runIntervalOptions[] = {
            {60s*30, u"30分钟"_s},
            {60s*60, u"60分钟"_s},
            {60s*120, u"120分钟"_s},
        };
        for (const auto& [interval, text] : runIntervalOptions) {
            ui->runIntervalComboBox->addItem(text, interval.count());
            if (interval.count() == currentVal) {
                ui->runIntervalComboBox->setCurrentIndex(ui->runIntervalComboBox->count() - 1);
            }
        }
        connect(ui->runIntervalComboBox, &QComboBox::currentIndexChanged, this, [this](int idx) {
            int val = ui->runIntervalComboBox->currentData().toInt();
            val = std::max(val, 120);
            Config::set(Config::KeyRunInterval, val);
        });
    }

    {
        int currentVal = currentConfig[Config::KeyScienceDonatePrefer].toInt();
        for (const auto& [scienceId, text] : AllianceScience::getValues()) {
            if (scienceId == AllianceScience::快速作战) {
                continue;
            }
            ui->allianceScienceFirstPreferComboBox->addItem(scienceId == 0 ? u"联盟推荐"_s : text, scienceId);
            if (scienceId == currentVal) {
                ui->allianceScienceFirstPreferComboBox->setCurrentIndex(ui->allianceScienceFirstPreferComboBox->count() - 1);
            }
        }
        connect(ui->allianceScienceFirstPreferComboBox, &QComboBox::currentIndexChanged, this, [this](int idx) {
            int val = ui->allianceScienceFirstPreferComboBox->currentData().toInt();
            Config::set(Config::KeyScienceDonatePrefer, val);
        });
    }

    {
        ui->donateCoinConsumeComboBox->addItem(u"关闭"_s, false);
        ui->donateCoinConsumeComboBox->addItem(u"开启"_s, true);
        bool currentVal = currentConfig[Config::KeyDonateCoinConsume].toBool();
        ui->donateCoinConsumeComboBox->setCurrentIndex(currentVal ? 1 : 0);
        connect(ui->donateCoinConsumeComboBox, &QComboBox::currentIndexChanged, this, [this](int idx) {
            bool val = ui->donateCoinConsumeComboBox->currentData().toBool();
            Config::set(Config::KeyDonateCoinConsume, val);
        });
    }

    {
        int currentVal = currentConfig[Config::KeyWorldSiteDonatePrefer].toInt();
        for (const auto& [val, text] : WorldSite::getKinds()) {
            ui->worldSitePreferComboBox->addItem(val == 0 ? u"自动选择"_s : text, val);
            if (val == currentVal) {
                ui->worldSitePreferComboBox->setCurrentIndex(ui->worldSitePreferComboBox->count() - 1);
            }
        }
        connect(ui->worldSitePreferComboBox, &QComboBox::currentIndexChanged, this, [this](int idx) {
            int val = ui->worldSitePreferComboBox->currentData().toInt();
            Config::set(Config::KeyWorldSiteDonatePrefer, val);
        });
    }

    ui->changeServerButton->hide();
    connect(ui->changeServerButton, &QPushButton::clicked, this, &MainWindow::openChangeServerDialog);
    connect(ui->consumeCoinButton, &QPushButton::clicked, this, &MainWindow::openConsumeCoinDialog);
    ui->consumeCoinButton->hide();

    topwarHelper = make_unique<TopwarHelper>();
    unique_ptr<GameSessionInfo> session = topwarHelper->readSavedSession();

    if (session != nullptr) {
        topwarHelper->loginBySession(std::move(session));
    } else {
        auto dlg = new WeixinLoginDialog{
            u"wxa3a080af3ee8278d"_s,
            u"https://warh5.rivergame.net/webgame/platform/wxlogin_redirect.html"_s,
            generateTempId(),
            this
        };
        dlg->setAppName(u"口袋奇兵H5"_s);
        connect(dlg, &QDialog::finished, this, [this, dlg](int result) {
            dlg->deleteLater();
            if (result != QDialog::Accepted) {
                isForcedClose = true;
                QTimer::singleShot(0, this, &QMainWindow::close);
                return;
            }
            qDebug() << "wx login success." << dlg->getCode();
            topwarHelper->loginByToken(dlg->getCode());
        });
        dlg->open();
    }
}

void MainWindow::appendToLog(const QString &s) {
    ui->logTextBrowser->append(s);
}

void MainWindow::openChangeServerDialog() {
    auto dlg = new QDialog{this};
    auto mainLayout = new QVBoxLayout{dlg};

    auto inputHLayout = new QHBoxLayout;
    auto inputLineEdit = new QLineEdit;
    inputLineEdit->setValidator(new QIntValidator{1, 9999});
    inputHLayout->addWidget(new QLabel{u"切换战区："_s}, 0, Qt::AlignLeft);
    inputHLayout->addWidget(inputLineEdit, 1, Qt::AlignLeft);
    mainLayout->addLayout(inputHLayout);

    auto dlgBtnLayout = new QHBoxLayout;
    auto acceptBtn = new QPushButton{u"确认"_s};
    auto cancelBtn = new QPushButton{u"取消"_s};
    dlgBtnLayout->addStretch(1);
    dlgBtnLayout->addWidget(acceptBtn);
    dlgBtnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(dlgBtnLayout);
    connect(acceptBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    dlg->setLayout(mainLayout);

    connect(dlg, &QDialog::finished, this, [this, dlg, inputLineEdit](int result) {
        dlg->deleteLater();
        if (result != QDialog::Accepted) {
            return;
        }
        int warzone = inputLineEdit->text().toInt();
        Config::set(Config::KeyWarzone, warzone);
        topwarHelper->onWantedWarzoneChanged(warzone);
    });
    dlg->open();
}

void MainWindow::openConsumeCoinDialog() {
    auto dlg = new QDialog{this};
    auto mainLayout = new QVBoxLayout{dlg};

    auto batchBuildDataTextEdit = new QTextEdit;
    batchBuildDataTextEdit->setWordWrapMode(QTextOption::NoWrap);
    batchBuildDataTextEdit->setReadOnly(false);
    mainLayout->addWidget(batchBuildDataTextEdit);

    auto coinHLayout = new QHBoxLayout;
    auto coinLineEdit = new QLineEdit;
    coinLineEdit->setValidator(new QDoubleValidator{});
    coinHLayout->addWidget(new QLabel{u"消耗金币："_s}, 0, Qt::AlignLeft);
    coinHLayout->addWidget(coinLineEdit, 1, Qt::AlignLeft);
    mainLayout->addLayout(coinHLayout);

    auto dlgBtnLayout = new QHBoxLayout;
    auto acceptBtn = new QPushButton{u"确认"_s};
    auto cancelBtn = new QPushButton{u"取消"_s};
    dlgBtnLayout->addStretch(1);
    dlgBtnLayout->addWidget(acceptBtn);
    dlgBtnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(dlgBtnLayout);
    connect(acceptBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    dlg->setLayout(mainLayout);

    connect(dlg, &QDialog::finished, this, [this, dlg, batchBuildDataTextEdit, coinLineEdit](int result) {
        dlg->deleteLater();
        if (result != QDialog::Accepted) {
            return;
        }
        QByteArray batchBuildData = batchBuildDataTextEdit->toPlainText().toUtf8();
        double coin = coinLineEdit->text().toDouble() * 1.0_hh;
        topwarHelper->consumeCoin(batchBuildData, coin);
    });
    dlg->open();
}


void MainWindow::showUserInfo(int warzone, const QString &username) {
    ui->warzoneLabel->setText(QString::number(warzone));
    ui->usernameLabel->setText(username);
    ui->changeServerButton->show();
}


void MainWindow::closeEvent(QCloseEvent *event) {
    if (isForcedClose) {
        event->accept();
        return;
    }

    auto ret = QMessageBox::question(this, u"" APP_NAME ""_s, u"确认关闭"_s);
    if (ret == QMessageBox::Yes) {
        event->accept();
    } else {
        event->ignore();
    }
}
