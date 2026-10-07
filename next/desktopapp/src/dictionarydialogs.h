#pragma once

#include "apiclient.h"

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Create or edit a room type or a building. Names are hy, en, and ru.
class NamedDictionaryDialog : public QDialog {
    Q_OBJECT

public:
    enum class Kind { RoomType, Building };

    explicit NamedDictionaryDialog(ApiClient *api, Kind kind, QWidget *parent = nullptr);
    void setRecord(const QJsonObject &row);

protected:
    void changeEvent(QEvent *event) override;

private:
    ApiResponse waitFor(quint64 requestId);
    void retranslateUi();
    void save();

    ApiClient *m_api = nullptr;
    Kind m_kind = Kind::RoomType;
    qint64 m_id = 0;
    int m_version = 0;

    QLabel *m_error = nullptr;
    QLineEdit *m_code = nullptr;
    QLineEdit *m_hy = nullptr;
    QLineEdit *m_en = nullptr;
    QLineEdit *m_ru = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_cancel = nullptr;
};

// Create or edit one room. The server checks the type, the building, the status, and version.
class RoomEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit RoomEditorDialog(ApiClient *api, QWidget *parent = nullptr);
    void setRecord(const QJsonObject &row);

protected:
    void changeEvent(QEvent *event) override;

private:
    ApiResponse waitFor(quint64 requestId);
    void fillTypes(const QJsonArray &items);
    void fillBuildings(const QJsonArray &items);
    void loadLists();
    void retranslateUi();
    void save();

    ApiClient *m_api = nullptr;
    qint64 m_id = 0;
    int m_version = 0;
    QJsonArray m_types;
    QJsonArray m_buildings;

    QLabel *m_error = nullptr;
    QLineEdit *m_code = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_building = nullptr;
    QLineEdit *m_floor = nullptr;
    QLineEdit *m_phone = nullptr;
    QComboBox *m_status = nullptr;
    QCheckBox *m_doNotDisturb = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_cancel = nullptr;
};
