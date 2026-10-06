#pragma once

#include <QLocale>
#include <QString>

class QAction;
class QMenu;
class QWidget;
class QIcon;

// hy / en / ru for every client. Source strings in code are English.
// An empty ini value follows the system locale when that language is one of
// the three; otherwise the default is Russian.
namespace HotelLocale {

QString normalizeStored(const QString &stored);
QString defaultCode(const QLocale &system = QLocale::system());
QString resolveCode(const QString &stored, const QLocale &system = QLocale::system());

QString currentCode();
bool qtBaseCatalogLoaded();

// Puts Sylfaen and Noto Sans Armenian after the platform UI font so Armenian
// letters are drawn (Windows: Sylfaen; Linux: Noto Sans Armenian).
void installUiFont();

// Loads hotel-desktop_<code>.qm from the app resource and qtbase_<code>.qm
// from Qt's translation directory. Sends LanguageChange; widgets retranslate.
void applyCode(const QString &code);

struct LanguageActions {
    QMenu *menu = nullptr;
    QAction *armenian = nullptr;
    QAction *english = nullptr;
    QAction *russian = nullptr;
};

LanguageActions makeLanguageMenu(QWidget *parent);
void syncLanguageMenu(const LanguageActions &actions);
QIcon flagIcon(const QString &code);

} // namespace HotelLocale
