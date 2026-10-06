#include "uilanguage.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QMenu>
#include <QTranslator>

namespace {

struct Catalog {
    QTranslator *app = nullptr;
    QTranslator *qt = nullptr;
    QString code = QStringLiteral("en");
    bool qtLoaded = false;
};

Catalog &catalog()
{
    static Catalog storage;
    if (!storage.app && qApp) {
        storage.app = new QTranslator(qApp);
        storage.qt = new QTranslator(qApp);
    }
    return storage;
}

} // namespace

void initFlagResources()
{
    Q_INIT_RESOURCE(flags);
}

namespace HotelLocale {

QString normalizeStored(const QString &stored)
{
    const QString code = stored.trimmed().toLower();
    if (code == QLatin1String("hy") || code == QLatin1String("en") || code == QLatin1String("ru"))
        return code;
    return {};
}

QString defaultCode(const QLocale &system)
{
    switch (system.language()) {
    case QLocale::Armenian:
        return QStringLiteral("hy");
    case QLocale::English:
        return QStringLiteral("en");
    case QLocale::Russian:
        return QStringLiteral("ru");
    default:
        break;
    }
    return QStringLiteral("ru");
}

QString resolveCode(const QString &stored, const QLocale &system)
{
    const QString code = normalizeStored(stored);
    if (!code.isEmpty())
        return code;
    return defaultCode(system);
}

QString currentCode()
{
    return catalog().code;
}

bool qtBaseCatalogLoaded()
{
    return catalog().qtLoaded;
}

void installUiFont()
{
    initFlagResources();

    QFont font = QGuiApplication::font();
    QStringList families;
    if (!font.family().isEmpty())
        families << font.family();
    const QStringList extra = {
        QStringLiteral("Noto Sans Armenian"),
        QStringLiteral("Sylfaen"),
        QStringLiteral("Segoe UI"),
    };
    for (const QString &name : extra) {
        if (!families.contains(name))
            families << name;
    }
    font.setFamilies(families);
    QGuiApplication::setFont(font);
}

void applyCode(const QString &code)
{
    installUiFont();
    const QString resolved = resolveCode(code);
    Catalog &active = catalog();
    if (!active.app)
        return;

    qApp->removeTranslator(active.app);
    qApp->removeTranslator(active.qt);
    active.code = resolved;
    active.qtLoaded = false;

    const bool loaded = active.app->load(QStringLiteral(":/i18n/hotel-desktop_%1").arg(resolved));
    if (loaded)
        qApp->installTranslator(active.app);
    else
        qWarning("hotel-desktop catalog failed to load: %s", qPrintable(resolved));

    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    active.qtLoaded = active.qt->load(QStringLiteral("qtbase_%1").arg(resolved), qtDir);
    if (active.qtLoaded)
        qApp->installTranslator(active.qt);

    QGuiApplication::setApplicationDisplayName(QCoreApplication::translate("MainWindow", "Hotel"));
    // installTranslator sends LanguageChange to qApp, which posts a copy to
    // each top-level widget. Flush those posted events now so open windows
    // retranslate before this function returns. Filtering on qApp misses them:
    // the receiver is the widget, not the application.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
}

LanguageActions makeLanguageMenu(QWidget *parent)
{
    initFlagResources();

    LanguageActions actions;
    actions.menu = new QMenu(parent);
    auto *group = new QActionGroup(actions.menu);
    group->setExclusive(true);

    const auto add = [&](const char *code, const QString &nativeName) {
        auto *action = actions.menu->addAction(flagIcon(QString::fromLatin1(code)), nativeName);
        action->setData(QString::fromLatin1(code));
        action->setCheckable(true);
        action->setObjectName(QStringLiteral("language-%1").arg(QString::fromLatin1(code)));
        group->addAction(action);
        return action;
    };
    actions.armenian = add("hy", QStringLiteral("Հայերեն"));
    actions.english = add("en", QStringLiteral("English"));
    actions.russian = add("ru", QStringLiteral("Русский"));
    return actions;
}

void syncLanguageMenu(const LanguageActions &actions)
{
    if (!actions.menu)
        return;
    const QString code = currentCode();
    actions.armenian->setChecked(code == QLatin1String("hy"));
    actions.english->setChecked(code == QLatin1String("en"));
    actions.russian->setChecked(code == QLatin1String("ru"));
}

QIcon flagIcon(const QString &code)
{
    initFlagResources();
    const QString normalized = normalizeStored(code);
    const QString file = normalized.isEmpty() ? QStringLiteral("ru") : normalized;
    return QIcon(QStringLiteral(":/flags/%1.png").arg(file));
}

} // namespace HotelLocale
