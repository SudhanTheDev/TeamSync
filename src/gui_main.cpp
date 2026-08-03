#include "TeamSyncGui.h"

#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleFactory>
#include <QSettings>
#include <QString>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <algorithm>
#include <string>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName("TeamSync");
    application.setApplicationDisplayName("TeamSync");
    application.setOrganizationName("TeamSync");
    application.setStyle(QStyleFactory::create("Fusion"));
    application.setFont(QFont("Segoe UI", 10));

    std::filesystem::path root = ".";
    bool smokeTest = false;
    int smokeMilliseconds = 700;
    bool automationHostUi = false;
    bool automationJoinUi = false;
    bool automationConnectionPage = false;
    bool automationTutorialPage = false;
    bool automationChatPanel = false;
    bool automationNotificationsPanel = false;
    bool automationLogin = false;
    bool automationOpenRegistration = false;
    bool automationOpenRecovery = false;
    QString automationUsername;
    QString automationPassword;
    QString automationTheme;
    QString automationJoinAddress;
    quint16 automationJoinPort = 45454;
    QString automationJoinCode;
    QString screenshotPath;
    teamsync::LanLaunchOptions network;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--data-dir" && index + 1 < argc) root = argv[++index];
        else if (argument == "--host" && index + 1 < argc) {
            network.mode = teamsync::LanLaunchOptions::Mode::Host;
            bool valid = false;
            const auto port = QString::fromLocal8Bit(argv[++index]).toUShort(&valid);
            network.port = valid && port > 0 ? port : 45454;
        }
        else if (argument == "--host-code" && index + 1 < argc) {
            network.pairingCode = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--automation-host-ui") automationHostUi = true;
        else if (argument == "--automation-connection-page") automationConnectionPage = true;
        else if (argument == "--automation-tutorial-page") automationTutorialPage = true;
        else if (argument == "--automation-chat-panel") automationChatPanel = true;
        else if (argument == "--automation-notifications-panel") automationNotificationsPanel = true;
        else if (argument == "--automation-open-registration") automationOpenRegistration = true;
        else if (argument == "--automation-open-recovery") automationOpenRecovery = true;
        else if (argument == "--automation-login" && index + 2 < argc) {
            automationLogin = true;
            automationUsername = QString::fromLocal8Bit(argv[++index]);
            automationPassword = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--automation-theme" && index + 1 < argc) {
            automationTheme = QString::fromLocal8Bit(argv[++index]).toLower();
        }
        else if (argument == "--automation-join-ui" && index + 3 < argc) {
            automationJoinUi = true;
            automationJoinAddress = QString::fromLocal8Bit(argv[++index]);
            bool valid = false;
            const auto port = QString::fromLocal8Bit(argv[++index]).toUShort(&valid);
            automationJoinPort = valid && port > 0 ? port : 45454;
            automationJoinCode = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--join" && index + 3 < argc) {
            network.mode = teamsync::LanLaunchOptions::Mode::Client;
            network.address = QString::fromLocal8Bit(argv[++index]);
            bool valid = false;
            const auto port = QString::fromLocal8Bit(argv[++index]).toUShort(&valid);
            network.port = valid && port > 0 ? port : 45454;
            network.pairingCode = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--smoke-test") smokeTest = true;
        else if (argument == "--smoke-ms" && index + 1 < argc) {
            smokeTest = true;
            smokeMilliseconds = std::max(100, std::stoi(argv[++index]));
        }
        else if (argument == "--screenshot" && index + 1 < argc) {
            screenshotPath = QString::fromLocal8Bit(argv[++index]);
        }
    }

    if (network.mode == teamsync::LanLaunchOptions::Mode::Client) {
        const std::string identity = network.address.toStdString() + ':' + std::to_string(network.port);
        root /= "network_cache";
        root /= std::to_string(std::hash<std::string>{}(identity));
    }
    if (automationTheme == "light") QSettings("TeamSync", "TeamSync").setValue("appearance/darkTheme", false);
    else if (automationTheme == "dark") QSettings("TeamSync", "TeamSync").setValue("appearance/darkTheme", true);
    teamsync::TeamSyncGui window(root, network);
    window.show();
    if (automationHostUi) {
        QTimer::singleShot(150, &application, [&] {
            auto* host = window.findChild<QPushButton*>("hostConnectionButton");
            if (!host) return;
            QTimer::singleShot(80, &application, [&] {
                if (auto* port = window.findChild<QSpinBox*>("hostPortSpin")) port->setValue(45682);
                for (auto* dialog : window.findChildren<QDialog*>()) {
                    if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
                        if (auto* ok = box->button(QDialogButtonBox::Ok)) ok->click();
                    }
                }
            });
            host->click();
        });
    }
    if (automationJoinUi) {
        QTimer::singleShot(150, &application, [&] {
            auto* join = window.findChild<QPushButton*>("joinConnectionButton");
            if (!join) return;
            QTimer::singleShot(80, &application, [&] {
                if (auto* address = window.findChild<QLineEdit*>("joinHostAddress")) address->setText(automationJoinAddress);
                if (auto* port = window.findChild<QSpinBox*>("joinPortSpin")) port->setValue(automationJoinPort);
                if (auto* code = window.findChild<QLineEdit*>("joinPairingCode")) code->setText(automationJoinCode);
                for (auto* dialog : window.findChildren<QDialog*>()) {
                    if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
                        if (auto* ok = box->button(QDialogButtonBox::Ok)) ok->click();
                    }
                }
            });
            join->click();
        });
    }
    if (automationConnectionPage) {
        QTimer::singleShot(automationJoinUi ? 1900 : 700, &application, [&] {
            for (auto* navigation : window.findChildren<QPushButton*>()) {
                if (navigation->text() == "Connection") {
                    navigation->click();
                    break;
                }
            }
        });
    }
    if (automationLogin) {
        QTimer::singleShot(180, &application, [&] {
            auto* username = window.findChild<QLineEdit*>("loginUsername");
            auto* password = window.findChild<QLineEdit*>("loginPassword");
            auto* login = window.findChild<QPushButton*>("loginButton");
            if (username && password && login) {
                username->setText(automationUsername);
                password->setText(automationPassword);
                login->click();
            }
        });
    }
    if (automationOpenRegistration || automationOpenRecovery) {
        QTimer::singleShot(180, &application, [&] {
            const QString target = automationOpenRegistration ? "Create a new account" : "Forgot password?";
            for (auto* candidate : window.findChildren<QPushButton*>()) {
                if (candidate->text() == target) {
                    candidate->click();
                    break;
                }
            }
        });
    }
    if (automationTutorialPage || automationChatPanel || automationNotificationsPanel) {
        QTimer::singleShot(700, &application, [&] {
            if (automationTutorialPage) {
                for (auto* navigation : window.findChildren<QPushButton*>()) {
                    if (navigation->objectName() == "navButton" &&
                        navigation->text().contains("Tutorial")) {
                        navigation->click();
                        break;
                    }
                }
            }
            if (automationChatPanel) {
                if (auto* chat = window.findChild<QPushButton*>("floatingChatButton")) chat->click();
            }
            if (automationNotificationsPanel) {
                if (auto* notices = window.findChild<QPushButton*>("notificationButton")) notices->click();
            }
        });
    }
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot((automationChatPanel || automationNotificationsPanel) ? 1100 : 900, &application, [&] {
            QWidget* target = &window;
            if (automationOpenRegistration) {
                if (auto* dialog = window.findChild<QDialog*>("registrationDialog")) target = dialog;
            } else if (automationOpenRecovery) {
                if (auto* dialog = window.findChild<QDialog*>("recoveryDialog")) target = dialog;
            }
            if (automationChatPanel) {
                if (auto* dialog = window.findChild<QDialog*>("chatPanel")) target = dialog;
            } else if (automationNotificationsPanel) {
                if (auto* dialog = window.findChild<QDialog*>("notificationsPanel")) target = dialog;
            }
            target->grab().save(screenshotPath);
            application.quit();
        });
    } else if (smokeTest) {
        QTimer::singleShot(smokeMilliseconds, &application, &QApplication::quit);
    }
    return application.exec();
}
