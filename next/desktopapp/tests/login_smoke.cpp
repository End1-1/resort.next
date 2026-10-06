#include "apiclient.h"
#include "appconfig.h"
#include "connectiondialog.h"
#include "healthmonitor.h"
#include "loginwindow.h"
#include "mainwindow.h"
#include "urlutil.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

bool writeText(const QString &path, const QByteArray &body)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(body) == body.size();
}

} // namespace

class LoginSmoke : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void parsesAddresses();
    void configFileRoundTripSkipsPassword();
    void bundledDefaultsUsedOnlyWhenUserFileMissing();
    void shippedExampleLoads();
    void userPathIsNotBesideExe();
    void settingsDialogShowsPathAndWritesUserFile();
    void mainWindowShowsSessionAndLogout();
    void emptyLoginDoesNotCallServer();
    void unreachableServerMessage();
    void loginShowsDatabaseNotConfigured();
};

void LoginSmoke::initTestCase()
{
#ifndef Q_OS_WIN
    const QByteArray root = qgetenv("XDG_CONFIG_HOME");
    QVERIFY2(!root.isEmpty(), "XDG_CONFIG_HOME must be a temp directory");
    const QString path = AppConfig::userFilePath();
    QVERIFY2(path.startsWith(QString::fromUtf8(root)), qPrintable(path));
    QVERIFY(path.contains(QStringLiteral("Resort")));
    QVERIFY(path.endsWith(QStringLiteral("hotel-desktop.ini")));
#endif
}

void LoginSmoke::parsesAddresses()
{
    const UrlParse hostPort = parseServerBase(QStringLiteral("127.0.0.1:8080"));
    QVERIFY(hostPort.ok);
    QCOMPARE(hostPort.url, QStringLiteral("http://127.0.0.1:8080"));

    const UrlParse https = parseServerBase(QStringLiteral("https://example.com"));
    QVERIFY(https.ok);
    QCOMPARE(https.url, QStringLiteral("https://example.com"));

    const UrlParse slash = parseServerBase(QStringLiteral("http://127.0.0.1:8080/"));
    QVERIFY(slash.ok);
    QCOMPARE(slash.url, QStringLiteral("http://127.0.0.1:8080"));

    QVERIFY(!parseServerBase(QStringLiteral("http://127.0.0.1:8080/health")).ok);
    QVERIFY(!parseServerBase(QStringLiteral("http://user:pass@127.0.0.1:8080")).ok);
    QVERIFY(!parseServerBase(QString()).ok);

    const UrlParse emptySocket = parseWebSocketUrl(QString());
    QVERIFY(emptySocket.ok);
    QVERIFY(emptySocket.url.isEmpty());

    const UrlParse ws = parseWebSocketUrl(QStringLiteral("127.0.0.1:8081"));
    QVERIFY(ws.ok);
    QCOMPARE(ws.url, QStringLiteral("ws://127.0.0.1:8081/api/v1/ws"));

    const UrlParse wss = parseWebSocketUrl(QStringLiteral("wss://example.com/api/v1/ws"));
    QVERIFY(wss.ok);
    QCOMPARE(wss.url, QStringLiteral("wss://example.com/api/v1/ws"));
}

void LoginSmoke::configFileRoundTripSkipsPassword()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString user = dir.filePath(QStringLiteral("user/hotel-desktop.ini"));
    QVERIFY(writeText(user, "base_url=http://10.0.0.5:9\nlast_login=ivan\npassword=secret\n"));

    const ConfigLoad loaded = AppConfig::loadFrom(user, QString());
    QCOMPARE(loaded.source, ConfigLoad::Source::UserFile);
    QCOMPARE(loaded.config.baseUrl, QStringLiteral("http://10.0.0.5:9"));
    QCOMPARE(loaded.config.lastLogin, QStringLiteral("ivan"));

    DesktopConfig toSave = loaded.config;
    toSave.lastLogin = QStringLiteral("bob");
    toSave.webSocketUrl = QStringLiteral("ws://10.0.0.5:10/api/v1/ws");
    QString error;
    QVERIFY2(AppConfig::saveTo(user, toSave, &error), qPrintable(error));

    QFile file(user);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(!text.contains(QStringLiteral("secret")));
    QVERIFY(!text.contains(QStringLiteral("password"), Qt::CaseInsensitive));
    QVERIFY(text.contains(QStringLiteral("bob")));

    const ConfigLoad again = AppConfig::loadFrom(user, QString());
    QCOMPARE(again.config.lastLogin, QStringLiteral("bob"));
    QCOMPARE(again.config.webSocketUrl, QStringLiteral("ws://10.0.0.5:10/api/v1/ws"));
}

void LoginSmoke::bundledDefaultsUsedOnlyWhenUserFileMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString user = dir.filePath(QStringLiteral("user/hotel-desktop.ini"));
    const QString bundled = dir.filePath(QStringLiteral("exe/hotel-desktop.ini"));
    QVERIFY(writeText(bundled,
                      "base_url=http://10.1.0.2:9\nwebsocket_url=ws://10.1.0.2:10/api/v1/ws\nlast_login=frombundled\n"));

    const ConfigLoad fromBundled = AppConfig::loadFrom(user, bundled);
    QCOMPARE(fromBundled.source, ConfigLoad::Source::BundledDefaults);
    QCOMPARE(fromBundled.config.baseUrl, QStringLiteral("http://10.1.0.2:9"));
    QCOMPARE(fromBundled.config.lastLogin, QStringLiteral("frombundled"));

    QVERIFY(writeText(user, "base_url=http://10.0.0.3:9\nlast_login=alice\n"));
    const ConfigLoad fromUser = AppConfig::loadFrom(user, bundled);
    QCOMPARE(fromUser.source, ConfigLoad::Source::UserFile);
    QCOMPARE(fromUser.config.baseUrl, QStringLiteral("http://10.0.0.3:9"));
    QCOMPARE(fromUser.config.lastLogin, QStringLiteral("alice"));
    QVERIFY(fromUser.config.webSocketUrl.isEmpty());

    QString error;
    DesktopConfig saved = fromUser.config;
    saved.lastLogin = QStringLiteral("alice");
    QVERIFY(AppConfig::saveTo(user, saved, &error));

    QFile bundledFile(bundled);
    QVERIFY(bundledFile.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(bundledFile.readAll()).contains(QStringLiteral("frombundled")));

    const ConfigLoad builtin = AppConfig::loadFrom(dir.filePath(QStringLiteral("missing-user.ini")),
                                                   dir.filePath(QStringLiteral("missing-bundled.ini")));
    QCOMPARE(builtin.source, ConfigLoad::Source::BuiltIn);
    QCOMPARE(builtin.config.baseUrl, QStringLiteral("http://127.0.0.1:8080"));
    QVERIFY(builtin.config.webSocketUrl.isEmpty());
}

void LoginSmoke::shippedExampleLoads()
{
    const QString example = QDir(QStringLiteral(QT_TESTCASE_SOURCEDIR)).filePath(QStringLiteral("hotel-desktop.ini.example"));
    QVERIFY(QFileInfo::exists(example));
    const ConfigLoad loaded = AppConfig::loadFrom(QStringLiteral("/no/such/hotel-desktop-user.ini"), example);
    QCOMPARE(loaded.source, ConfigLoad::Source::BundledDefaults);
    QVERIFY2(loaded.warning.isEmpty(), qPrintable(loaded.warning));
    QCOMPARE(loaded.config.baseUrl, QStringLiteral("http://127.0.0.1:8080"));
    QVERIFY(loaded.config.webSocketUrl.isEmpty());
    QVERIFY(loaded.config.lastLogin.isEmpty());
}

void LoginSmoke::userPathIsNotBesideExe()
{
    const QString path = AppConfig::userFilePath();
    QVERIFY(path.endsWith(QStringLiteral("hotel-desktop.ini")));
    QVERIFY(!path.startsWith(QCoreApplication::applicationDirPath()));
    const QString native = QDir::toNativeSeparators(path);
    QVERIFY(native.contains(QStringLiteral("Resort")));
    QVERIFY(native.contains(QStringLiteral("hotel-desktop")));
}

void LoginSmoke::settingsDialogShowsPathAndWritesUserFile()
{
    const QString bundled = AppConfig::bundledDefaultsPath();
    QVERIFY2(!QFileInfo::exists(bundled), qPrintable(bundled));

    DesktopConfig initial;
    ConnectionDialog dialog(initial);
    auto *pathLabel = dialog.findChild<QLabel *>(QStringLiteral("configPathLabel"));
    QVERIFY(pathLabel);
    QVERIFY(pathLabel->text().contains(QDir::toNativeSeparators(AppConfig::userFilePath())));

    auto *base = dialog.findChild<QLineEdit *>(QStringLiteral("baseUrlEdit"));
    QVERIFY(base);
    base->setText(QStringLiteral("10.2.0.8:4242"));
    dialog.accept();
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.settings().baseUrl, QStringLiteral("http://10.2.0.8:4242"));
    QVERIFY(QFileInfo::exists(AppConfig::userFilePath()));
    QCOMPARE(AppConfig::load().config.baseUrl, QStringLiteral("http://10.2.0.8:4242"));
    QVERIFY2(!QFileInfo::exists(bundled), qPrintable(bundled));

    QFile file(AppConfig::userFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(!text.contains(QStringLiteral("password"), Qt::CaseInsensitive));
}

void LoginSmoke::mainWindowShowsSessionAndLogout()
{
    UserSnapshot user;
    user.id = 4;
    user.login = QStringLiteral("ivan");
    user.name = QStringLiteral("Иван Иванов");
    user.rolePresent = true;
    user.roleId = 7;
    user.commandsAllowed = true;
    user.expiresAt = QStringLiteral("2026-10-06T12:00:00Z");

    MainWindow window;
    window.showSession(user, QStringLiteral("http://127.0.0.1:8080"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("userLabel"))->text(), QStringLiteral("Иван Иванов"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("loginLabel"))->text(), QStringLiteral("ivan"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("roleLabel"))->text(), QStringLiteral("7"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("commandsLabel"))->text(), QStringLiteral("да"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("serverLabel"))->text(), QStringLiteral("http://127.0.0.1:8080"));
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("workspacePlaceholderLabel")));

    user.rolePresent = false;
    user.commandsAllowed = false;
    window.showSession(user, QStringLiteral("http://127.0.0.1:8080"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("roleLabel"))->text(), QStringLiteral("не назначена"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("commandsLabel"))->text(), QStringLiteral("нет"));

    QSignalSpy logoutSpy(&window, &MainWindow::logoutRequested);
    auto *logout = window.findChild<QAction *>(QStringLiteral("logoutAction"));
    QVERIFY(logout);
    logout->trigger();
    QCOMPARE(logoutSpy.count(), 1);

    QVERIFY(window.findChild<QAction *>(QStringLiteral("connectionSettingsAction")));
}

void LoginSmoke::emptyLoginDoesNotCallServer()
{
    ApiClient api;
    LoginWindow window(&api);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.findChild<QLineEdit *>(QStringLiteral("loginEdit"))->clear();
    window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->clear();
    QVERIFY(QMetaObject::invokeMethod(&window, "submit"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(),
             QStringLiteral("Введите логин и пароль."));
}

void LoginSmoke::unreachableServerMessage()
{
    ApiClient api;
    api.setBaseUrl(QStringLiteral("http://127.0.0.1:59999"));
    LoginWindow window(&api);
    QSignalSpy remembered(&window, &LoginWindow::rememberLogin);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.findChild<QLineEdit *>(QStringLiteral("loginEdit"))->setText(QStringLiteral("ivan"));
    window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(QStringLiteral("secret"));
    QVERIFY(QMetaObject::invokeMethod(&window, "submit"));
    QCOMPARE(remembered.count(), 1);
    QCOMPARE(remembered.at(0).at(0).toString(), QStringLiteral("ivan"));

    auto *error = window.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QTRY_VERIFY_WITH_TIMEOUT(error->text().contains(QStringLiteral("Сервер недоступен")), 12000);
    QVERIFY2(!error->text().contains(QStringLiteral("secret")), qPrintable(error->text()));
    QVERIFY(!api.hasToken());
}

void LoginSmoke::loginShowsDatabaseNotConfigured()
{
    const QString bin = qEnvironmentVariable("HOTEL_API_BIN");
    if (bin.isEmpty())
        QSKIP("HOTEL_API_BIN is not set");
    QVERIFY(QFileInfo::exists(bin));

    QTemporaryDir configDir;
    QVERIFY(configDir.isValid());
    const QString iniPath = configDir.filePath(QStringLiteral("hotel-api.ini"));
    QVERIFY(writeText(iniPath, "listen=127.0.0.1:18080\ndsn=\nws_listen=127.0.0.1:18081\n"));

    QProcess server;
    server.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("HOTEL_DSN"));
    env.remove(QStringLiteral("HOTEL_WS_LISTEN"));
    env.insert(QStringLiteral("HOTEL_CONFIG"), iniPath);
    env.insert(QStringLiteral("HOTEL_LISTEN"), QStringLiteral("127.0.0.1:18080"));
    server.setProcessEnvironment(env);
    server.start(bin, {});
    QVERIFY2(server.waitForStarted(5000), "hotel-api did not start");

    const auto stopServer = [&server]() {
        if (server.state() == QProcess::NotRunning)
            return;
        server.terminate();
        if (!server.waitForFinished(3000)) {
            server.kill();
            server.waitForFinished(2000);
        }
    };

    ApiClient probe;
    probe.setBaseUrl(QStringLiteral("http://127.0.0.1:18080"));
    bool up = false;
    QObject::connect(&probe, &ApiClient::healthFinished, &probe, [&up](const HealthStatus &status) {
        if (status.current && status.reachable && status.dbState == QLatin1String("skipped"))
            up = true;
    });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &probe, [&probe]() { probe.requestHealth(); });
    poll.start(200);
    probe.requestHealth();
    const bool ready = QTest::qWaitFor([&up]() { return up; }, 8000);
    poll.stop();
    if (!ready) {
        const QByteArray err = server.readAllStandardError();
        const QByteArray out = server.readAllStandardOutput();
        stopServer();
        QFAIL(qPrintable(QStringLiteral("hotel-api /health did not report skipped: %1 %2")
                             .arg(QString::fromUtf8(err), QString::fromUtf8(out))));
    }

    HealthMonitor monitor(&probe);
    monitor.setWebSocketUrl(QStringLiteral("ws://127.0.0.1:18081/api/v1/ws"));
    monitor.start();
    const bool hello = QTest::qWaitFor([&monitor]() {
        return monitor.socketText().contains(QStringLiteral("hello"));
    }, 5000);
    if (!hello) {
        const QByteArray err = server.readAllStandardError();
        const QString socketText = monitor.socketText();
        monitor.stop();
        stopServer();
        QFAIL(qPrintable(QStringLiteral("WebSocket hello missing (%1): %2")
                             .arg(socketText, QString::fromUtf8(err))));
    }
    monitor.stop();

    ConnectionDialog dialog(DesktopConfig{});
    dialog.findChild<QLineEdit *>(QStringLiteral("baseUrlEdit"))->setText(QStringLiteral("http://127.0.0.1:18080"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "checkConnection"));
    auto *probeLabel = dialog.findChild<QLabel *>(QStringLiteral("probeStatusLabel"));
    QTRY_VERIFY_WITH_TIMEOUT(probeLabel->text().contains(QStringLiteral("skipped")), 8000);
    QVERIFY2(probeLabel->text().contains(QStringLiteral("не настроена")), qPrintable(probeLabel->text()));

    ApiClient api;
    api.setBaseUrl(QStringLiteral("http://127.0.0.1:18080"));
    LoginWindow window(&api);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.findChild<QLineEdit *>(QStringLiteral("loginEdit"))->setText(QStringLiteral("nobody"));
    window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(QStringLiteral("wrong-password"));
    QVERIFY(QMetaObject::invokeMethod(&window, "submit"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QTRY_VERIFY_WITH_TIMEOUT(error->text().contains(QStringLiteral("База не настроена")), 10000);
    QVERIFY2(error->text().contains(QStringLiteral("database_not_configured"))
                 || error->text().contains(QStringLiteral("База не настроена")),
             qPrintable(error->text()));
    QVERIFY(!error->text().contains(QStringLiteral("wrong-password")));
    QVERIFY(!api.hasToken());

    stopServer();
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));

    QTemporaryDir configHome;
    if (configHome.isValid())
        qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    AppConfig::applyIdentity();
    QApplication app(argc, argv);
    LoginSmoke tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "login_smoke.moc"
