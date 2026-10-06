#include "apiclient.h"
#include "appconfig.h"
#include "connectiondialog.h"
#include "healthmonitor.h"
#include "uilanguage.h"
#include "loginwindow.h"
#include "mainwindow.h"
#include "urlutil.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRawFont>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QXmlStreamReader>

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
    void languageFollowsSystemUnlessIniOverrides();
    void translationsAreCompleteAndLoad();
    void languageSwitchRetranslatesWithoutRestart();
    void armenianTextIsNotBoxes();
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
    toSave.language = QStringLiteral("hy");
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
    QCOMPARE(again.config.language, QStringLiteral("hy"));
    QVERIFY(text.contains(QStringLiteral("language=hy")));

    QVERIFY(writeText(user, "language=de\nbase_url=http://10.0.0.5:9\n"));
    const ConfigLoad unknown = AppConfig::loadFrom(user, QString());
    QVERIFY(unknown.config.language.isEmpty());
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
    QVERIFY(loaded.config.language.isEmpty());
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
    HotelLocale::applyCode(QStringLiteral("en"));
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
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("commandsLabel"))->text(), QStringLiteral("yes"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("serverLabel"))->text(), QStringLiteral("http://127.0.0.1:8080"));
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("workspacePlaceholderLabel")));

    user.rolePresent = false;
    user.commandsAllowed = false;
    window.showSession(user, QStringLiteral("http://127.0.0.1:8080"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("roleLabel"))->text(), QStringLiteral("not assigned"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("commandsLabel"))->text(), QStringLiteral("no"));

    QSignalSpy logoutSpy(&window, &MainWindow::logoutRequested);
    auto *logout = window.findChild<QAction *>(QStringLiteral("logoutAction"));
    QVERIFY(logout);
    logout->trigger();
    QCOMPARE(logoutSpy.count(), 1);

    QVERIFY(window.findChild<QAction *>(QStringLiteral("connectionSettingsAction")));
}

void LoginSmoke::emptyLoginDoesNotCallServer()
{
    HotelLocale::applyCode(QStringLiteral("en"));
    ApiClient api;
    LoginWindow window(&api);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.findChild<QLineEdit *>(QStringLiteral("loginEdit"))->clear();
    window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->clear();
    QVERIFY(QMetaObject::invokeMethod(&window, "submit"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(),
             QStringLiteral("Enter your login and password."));
}

void LoginSmoke::unreachableServerMessage()
{
    HotelLocale::applyCode(QStringLiteral("en"));
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
    QTRY_VERIFY_WITH_TIMEOUT(error->text().contains(QStringLiteral("Server unavailable")), 12000);
    QVERIFY2(!error->text().contains(QStringLiteral("secret")), qPrintable(error->text()));
    QVERIFY(!api.hasToken());
}

void LoginSmoke::loginShowsDatabaseNotConfigured()
{
    HotelLocale::applyCode(QStringLiteral("en"));
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
    QVERIFY2(probeLabel->text().contains(QStringLiteral("not configured")), qPrintable(probeLabel->text()));

    ApiClient api;
    api.setBaseUrl(QStringLiteral("http://127.0.0.1:18080"));
    LoginWindow window(&api);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.findChild<QLineEdit *>(QStringLiteral("loginEdit"))->setText(QStringLiteral("nobody"));
    window.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(QStringLiteral("wrong-password"));
    QVERIFY(QMetaObject::invokeMethod(&window, "submit"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("errorLabel"));
    QTRY_VERIFY_WITH_TIMEOUT(error->text().contains(QStringLiteral("database_not_configured")), 10000);
    QVERIFY2(error->text().contains(QStringLiteral("not configured")), qPrintable(error->text()));
    QVERIFY(!error->text().contains(QStringLiteral("wrong-password")));
    QVERIFY(!api.hasToken());

    stopServer();
}

void LoginSmoke::languageFollowsSystemUnlessIniOverrides()
{
    QCOMPARE(HotelLocale::defaultCode(QLocale(QLocale::Armenian)), QStringLiteral("hy"));
    QCOMPARE(HotelLocale::defaultCode(QLocale(QLocale::English)), QStringLiteral("en"));
    QCOMPARE(HotelLocale::defaultCode(QLocale(QLocale::Russian)), QStringLiteral("ru"));
    QCOMPARE(HotelLocale::defaultCode(QLocale(QLocale::German)), QStringLiteral("ru"));
    QCOMPARE(HotelLocale::resolveCode(QString(), QLocale(QLocale::French)), QStringLiteral("ru"));
    QCOMPARE(HotelLocale::resolveCode(QStringLiteral("hy"), QLocale(QLocale::English)), QStringLiteral("hy"));
    QCOMPARE(HotelLocale::normalizeStored(QStringLiteral(" EN ")), QStringLiteral("en"));
    QVERIFY(HotelLocale::normalizeStored(QStringLiteral("de")).isEmpty());
}

void LoginSmoke::translationsAreCompleteAndLoad()
{
    const QDir dir(QDir(QStringLiteral(QT_TESTCASE_SOURCEDIR)).filePath(QStringLiteral("translations")));
    const QStringList catalogs = {QStringLiteral("hotel-desktop_ru.ts"), QStringLiteral("hotel-desktop_hy.ts")};
    for (const QString &name : catalogs) {
        QFile file(dir.filePath(name));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        QXmlStreamReader xml(&file);
        int messages = 0;
        QString source;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (xml.name() == QLatin1String("source"))
                source = xml.readElementText();
            if (xml.name() == QLatin1String("translation")) {
                const QString type = xml.attributes().value(QLatin1String("type")).toString();
                const QString text = xml.readElementText();
                if (type == QLatin1String("obsolete") || type == QLatin1String("vanished"))
                    continue;
                ++messages;
                QVERIFY2(type != QLatin1String("unfinished"), qPrintable(source));
                QVERIFY2(!text.trimmed().isEmpty(), qPrintable(name + QLatin1Char(' ') + source));
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(messages >= 90);
    }

    struct Expectation {
        const char *code;
        const char *button;
        const char *database;
    };
    const Expectation rows[] = {
        {"en", "Sign in", "The database is not configured"},
        {"ru", "Войти", "База не настроена"},
        {"hy", "Մուտք գործել", "Բազան կարգավորված չէ"},
    };
    for (const Expectation &row : rows) {
        QTranslator translator;
        const QString path = QStringLiteral(":/i18n/hotel-desktop_%1").arg(QString::fromLatin1(row.code));
        QVERIFY2(translator.load(path), qPrintable(path));
        QVERIFY(qApp->installTranslator(&translator));
        QCOMPARE(QCoreApplication::translate("LoginWindow", "Sign in", "button"), QString::fromUtf8(row.button));
        const QString db = QCoreApplication::translate(
            "ApiClient",
            "The database is not configured (database_not_configured). Sign-in is impossible until the server has a MariaDB connection.");
        QVERIFY2(db.contains(QString::fromUtf8(row.database)), qPrintable(db));
        QVERIFY(db.contains(QStringLiteral("database_not_configured")));
        qApp->removeTranslator(&translator);
    }

    const QString ruQt = QDir(QLibraryInfo::path(QLibraryInfo::TranslationsPath)).filePath(QStringLiteral("qtbase_ru.qm"));
    if (QFileInfo::exists(ruQt)) {
        HotelLocale::applyCode(QStringLiteral("ru"));
        QVERIFY(HotelLocale::qtBaseCatalogLoaded());
    }
    HotelLocale::applyCode(QStringLiteral("en"));
}

void LoginSmoke::languageSwitchRetranslatesWithoutRestart()
{
    ApiClient api;
    LoginWindow login(&api);
    MainWindow window;
    HotelLocale::applyCode(QStringLiteral("en"));
    QCOMPARE(login.findChild<QPushButton *>(QStringLiteral("loginButton"))->text(), QStringLiteral("Sign in"));
    QCOMPARE(login.windowTitle(), QStringLiteral("Sign in"));
    QCOMPARE(window.findChild<QMenu *>(QStringLiteral("languageMenu"))->title(), QStringLiteral("Language"));

    auto *button = login.findChild<QToolButton *>(QStringLiteral("languageButton"));
    QVERIFY(button);
    QVERIFY(!button->icon().isNull());
    QCOMPARE(button->menu()->actions().size(), 3);

    HotelLocale::applyCode(QStringLiteral("ru"));
    QCOMPARE(login.findChild<QPushButton *>(QStringLiteral("loginButton"))->text(), QStringLiteral("Войти"));
    QCOMPARE(login.windowTitle(), QStringLiteral("Вход"));
    QCOMPARE(window.findChild<QMenu *>(QStringLiteral("languageMenu"))->title(), QStringLiteral("Язык"));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("logoutAction"))->text(), QStringLiteral("Выход"));
    QVERIFY(button->icon().isNull() == false);

    HotelLocale::applyCode(QStringLiteral("hy"));
    QCOMPARE(login.findChild<QPushButton *>(QStringLiteral("loginButton"))->text(), QStringLiteral("Մուտք գործել"));
    QCOMPARE(window.findChild<QMenu *>(QStringLiteral("languageMenu"))->title(), QStringLiteral("Լեզու"));
    QCOMPARE(login.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(), QString());
    login.findChild<QLineEdit *>(QStringLiteral("loginEdit"))->clear();
    login.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->clear();
    QVERIFY(QMetaObject::invokeMethod(&login, "submit"));
    QCOMPARE(login.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(),
             QStringLiteral("Մուտքագրեք մուտքանունը և գաղտնաբառը։"));

    HotelLocale::applyCode(QStringLiteral("en"));
    QCOMPARE(login.findChild<QLabel *>(QStringLiteral("errorLabel"))->text(),
             QStringLiteral("Enter your login and password."));
}

void LoginSmoke::armenianTextIsNotBoxes()
{
    HotelLocale::installUiFont();
    const QChar letter(0x0570);
    bool supported = false;
    const QStringList families = QApplication::font().families();
    for (const QString &family : families) {
        const QRawFont raw = QRawFont::fromFont(QFont(family));
        if (raw.isValid() && raw.supportsCharacter(letter))
            supported = true;
    }
    QVERIFY2(supported, qPrintable(QStringLiteral("UI font list has no Armenian glyphs: %1").arg(families.join(QLatin1Char(',')))));

    const auto paint = [](const QFont &font) {
        QImage image(220, 48, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        QFont sized = font;
        sized.setPointSize(18);
        painter.setFont(sized);
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("Հայերեն"));
        return image;
    };

    QFont broken(QStringLiteral("DejaVu Sans"));
    broken.setStyleStrategy(QFont::NoFontMerging);
    const QRawFont brokenRaw = QRawFont::fromFont(broken);
    if (brokenRaw.isValid() && !brokenRaw.supportsCharacter(letter)) {
        QFont ui = QApplication::font();
        QVERIFY(paint(ui) != paint(broken));
    }
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
