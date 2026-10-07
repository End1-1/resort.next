#pragma once

#include "apiclient.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;

// Dictionaries for the signed-in property: room types, buildings, and rooms.
// Room status is a closed code, not a dictionary. Writes are disabled when
// commands_allowed is false. Lists reload on demand and when the UI language changes.
class DictionariesPage : public QWidget {
    Q_OBJECT

public:
    explicit DictionariesPage(ApiClient *api, QWidget *parent = nullptr);
    void reload();
    void setCommandsAllowed(bool allowed);

protected:
    void changeEvent(QEvent *event) override;

private:
    enum class Kind { RoomType, Building, Room };

    struct Pane {
        Kind kind = Kind::Room;
        QString path;
        QLabel *status = nullptr;
        QLabel *locked = nullptr;
        QPushButton *add = nullptr;
        QPushButton *edit = nullptr;
        QPushButton *remove = nullptr;
        QTableWidget *table = nullptr;
        quint64 requestId = 0;
        bool loaded = false;
    };

    void retranslateUi();
    void reloadPane(Pane *pane);
    void applyPane(Pane *pane, const ApiResponse &response);
    void updateButtons();
    void addRow(Pane *pane);
    void editRow(Pane *pane);
    void deleteRow(Pane *pane);
    QJsonObject selected(const Pane &pane) const;
    static QString statusText(const QString &code);

    ApiClient *m_api = nullptr;
    bool m_commands = false;
    QTabWidget *m_tabs = nullptr;
    Pane m_types;
    Pane m_buildings;
    Pane m_rooms;
};
