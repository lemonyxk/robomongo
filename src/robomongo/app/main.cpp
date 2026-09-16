#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <iostream>
#include <string>

#include <locale.h>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/Logger.h"       
#include "robomongo/gui/MainWindow.h"
#include "robomongo/gui/AppStyle.h"
#include "robomongo/gui/dialogs/EulaDialog.h"
#include "robomongo/ssh/ssh.h"
#include "robomongo/utils/RoboCrypt.h"       

int main(int argc, char *argv[])
{
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "Robo 3T " PROJECT_VERSION " | Qt " PROJECT_QT_VERSION " | mongosh " MONGOSH_VERSION " | MongoDB C Driver " ROBO_MONGOC_VERSION " | Node " NODE_VERSION "\n";
        return 0;
    }
    if (rbm_ssh_init()) 
        return 1;

    // Initialize Qt application
    QApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("ROBOMONGO_PROFILE_DIR")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, Robomongo::ConfigRoot);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, Robomongo::ConfigRoot);
    }

    // On Unix/Linux Qt is configured to use the system locale settings by default.
    // This can cause a conflict when using POSIX functions, for instance, when
    // converting between data types such as floats and strings, since the notation
    // may differ between locales. To get around this problem, call the POSIX
    // function setlocale(LC_NUMERIC, "C") right after initializing QApplication or
    // QCoreApplication to reset the locale that is used for number formatting to "C"-locale.
    // (https://doc.qt.io/qt-6/qcoreapplication.html#locale-settings)
    setlocale(LC_NUMERIC, "C");

     
    // Apply the shared theme before creating any top-level window.
    Robomongo::AppStyleUtils::initStyle();

    // EULA License Agreement
    auto const& settings { Robomongo::AppRegistry::instance().settingsManager() };
    if (!settings->acceptedEulaVersions().contains(PROJECT_VERSION)) {
        bool const showFormPage { settings->programExitedNormally() && !settings->disableHttpsFeatures() };
        Robomongo::EulaDialog eulaDialog(showFormPage);
        settings->setProgramExitedNormally(false);
        settings->save();
        int const result = eulaDialog.exec();
        settings->setProgramExitedNormally(true);
        settings->save();
        if (QDialog::Rejected == result) {
            rbm_ssh_cleanup();
            return 1;
        }
        // EULA accepted
        settings->addAcceptedEulaVersion(PROJECT_VERSION);
        settings->save();
    }  

    // To be set true at normal program exit
    settings->setProgramExitedNormally(false);
    settings->save();

    // Application main window
    Robomongo::MainWindow mainWindow;
    mainWindow.show();

    for(auto const& msgAndSeverity : Robomongo::RoboCrypt::roboCryptLogs())
        Robomongo::LOG_MSG(msgAndSeverity.first, msgAndSeverity.second);

    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(2000, &mainWindow, &QWidget::close);

    int rc = app.exec();
    rbm_ssh_cleanup();
    return rc;
}
