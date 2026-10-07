#include "dictionarydialogs.h"

#include "uilanguage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QEventLoop>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

QJsonObject namesBody(const QString &hy, const QString &en, const QString &ru)
{
    QJsonObject names;
    names.insert(QStringLiteral("hy"), hy);
    names.insert(QStringLiteral("en"), en);
    names.insert(QStringLiteral("ru"), ru);
    return names;
}

QString statusText(const QString &code)
{
    if (code == QLatin1String("vacant_ready"))
        return RoomEditorDialog::tr("Ready");
    if (code == QLatin1String("occupied"))
        return RoomEditorDialog::tr("Occupied");
    if (code == QLatin1String("vacant_dirty"))
        return RoomEditorDialog::tr("Dirty");
    if (code == QLatin1String("out_of_order"))
        return RoomEditorDialog::tr("Out of order");
    if (code == QLatin1String("house_use"))
        return RoomEditorDialog::tr("House use");
    if (code == QLatin1String("complimentary"))
        return RoomEditorDialog::tr("Complimentary");
    if (code == QLatin1String("out_of_inventory"))
        return RoomEditorDialog::tr("Out of inventory");
    return code;
}

const char *kStatusCodes[] = {
    "vacant_ready",
    "occupied",
    "vacant_dirty",
    "out_of_order",
    "house_use",
    "complimentary",
    "out_of_inventory",
};

} // namespace

NamedDictionaryDialog::NamedDictionaryDialog(ApiClient *api, Kind kind, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
    , m_kind(kind)
{
    setObjectName(kind == Kind::RoomType ? QStringLiteral("roomTypeDialog") : QStringLiteral("buildingDialog"));
    resize(440, 280);

    m_error = new QLabel;
    m_error->setObjectName(QStringLiteral("dictionaryError"));
    m_error->setWordWrap(true);
    m_code = new QLineEdit;
    m_code->setObjectName(QStringLiteral("dictionaryCode"));
    m_code->setMaxLength(32);
    m_hy = new QLineEdit;
    m_hy->setObjectName(QStringLiteral("dictionaryNameHy"));
    m_hy->setMaxLength(128);
    m_en = new QLineEdit;
    m_en->setObjectName(QStringLiteral("dictionaryNameEn"));
    m_en->setMaxLength(128);
    m_ru = new QLineEdit;
    m_ru->setObjectName(QStringLiteral("dictionaryNameRu"));
    m_ru->setMaxLength(128);
    m_save = new QPushButton;
    m_save->setObjectName(QStringLiteral("dictionarySave"));
    m_save->setDefault(true);
    m_cancel = new QPushButton;
    m_cancel->setObjectName(QStringLiteral("dictionaryCancel"));

    auto *form = new QFormLayout;
    auto addRow = [form](const char *name, QWidget *field) {
        auto *label = new QLabel;
        label->setObjectName(QString::fromLatin1(name));
        form->addRow(label, field);
    };
    addRow("dictionaryCodeLabel", m_code);
    addRow("dictionaryHyLabel", m_hy);
    addRow("dictionaryEnLabel", m_en);
    addRow("dictionaryRuLabel", m_ru);

    auto *actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(m_cancel);
    actions->addWidget(m_save);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_error);
    layout->addLayout(form);
    layout->addLayout(actions);

    connect(m_save, &QPushButton::clicked, this, &NamedDictionaryDialog::save);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    retranslateUi();
}

void NamedDictionaryDialog::setRecord(const QJsonObject &row)
{
    m_id = row.value(QStringLiteral("id")).toInteger();
    m_version = row.value(QStringLiteral("version")).toInt();
    m_code->setText(row.value(QStringLiteral("code")).toString());
    const QJsonObject names = row.value(QStringLiteral("names")).toObject();
    m_hy->setText(names.value(QStringLiteral("hy")).toString());
    m_en->setText(names.value(QStringLiteral("en")).toString());
    m_ru->setText(names.value(QStringLiteral("ru")).toString());
    m_error->clear();
    retranslateUi();
}

void NamedDictionaryDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void NamedDictionaryDialog::retranslateUi()
{
    auto setLabel = [this](const char *name, const QString &text) {
        if (auto *label = findChild<QLabel *>(QString::fromLatin1(name)))
            label->setText(text);
    };
    const bool creating = m_id <= 0;
    if (m_kind == Kind::RoomType)
        setWindowTitle(creating ? tr("New room type") : tr("Edit room type"));
    else
        setWindowTitle(creating ? tr("New building") : tr("Edit building"));
    setLabel("dictionaryCodeLabel", tr("Code"));
    setLabel("dictionaryHyLabel", tr("Armenian"));
    setLabel("dictionaryEnLabel", tr("English"));
    setLabel("dictionaryRuLabel", tr("Russian"));
    m_save->setText(tr("Save"));
    m_cancel->setText(tr("Cancel"));
}

ApiResponse NamedDictionaryDialog::waitFor(quint64 requestId)
{
    ApiResponse found;
    QEventLoop loop;
    const QMetaObject::Connection finished = connect(m_api, &ApiClient::responseFinished, &loop, [&](const ApiResponse &response) {
        if (response.id != requestId)
            return;
        found = response;
        loop.quit();
    });
    const QMetaObject::Connection rejected = connect(m_api, &ApiClient::sessionRejected, &loop, [&](const QString &) { loop.quit(); });
    loop.exec();
    disconnect(finished);
    disconnect(rejected);
    return found;
}

void NamedDictionaryDialog::save()
{
    const QString code = m_code->text().trimmed();
    const QString hy = m_hy->text().trimmed();
    const QString en = m_en->text().trimmed();
    const QString ru = m_ru->text().trimmed();
    if (code.isEmpty()) {
        m_error->setText(tr("Code is required."));
        return;
    }
    if (hy.isEmpty() || en.isEmpty() || ru.isEmpty()) {
        m_error->setText(tr("Armenian, English, and Russian names are required."));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("names"), namesBody(hy, en, ru));
    const QString path = m_kind == Kind::RoomType ? QStringLiteral("/api/v1/room-types") : QStringLiteral("/api/v1/buildings");
    m_save->setEnabled(false);
    ApiResponse response;
    if (m_id <= 0) {
        response = waitFor(m_api->request(HttpVerb::Post, path, QUrlQuery(), QJsonDocument(body).toJson(QJsonDocument::Compact), true));
    } else {
        body.insert(QStringLiteral("version"), m_version);
        response = waitFor(m_api->request(HttpVerb::Patch,
                                          path + QStringLiteral("/%1").arg(m_id),
                                          QUrlQuery(),
                                          QJsonDocument(body).toJson(QJsonDocument::Compact),
                                          true));
    }
    if (!response.ok) {
        m_save->setEnabled(true);
        m_error->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                               : response.error.userMessage);
        return;
    }
    accept();
}

RoomEditorDialog::RoomEditorDialog(ApiClient *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("roomDialog"));
    resize(480, 420);

    m_error = new QLabel;
    m_error->setObjectName(QStringLiteral("roomError"));
    m_error->setWordWrap(true);
    m_code = new QLineEdit;
    m_code->setObjectName(QStringLiteral("roomCode"));
    m_code->setMaxLength(32);
    m_type = new QComboBox;
    m_type->setObjectName(QStringLiteral("roomType"));
    m_building = new QComboBox;
    m_building->setObjectName(QStringLiteral("roomBuilding"));
    m_floor = new QLineEdit;
    m_floor->setObjectName(QStringLiteral("roomFloor"));
    m_phone = new QLineEdit;
    m_phone->setObjectName(QStringLiteral("roomPhone"));
    m_phone->setMaxLength(32);
    m_status = new QComboBox;
    m_status->setObjectName(QStringLiteral("roomStatus"));
    m_doNotDisturb = new QCheckBox;
    m_doNotDisturb->setObjectName(QStringLiteral("roomDoNotDisturb"));
    m_save = new QPushButton;
    m_save->setObjectName(QStringLiteral("roomSave"));
    m_save->setDefault(true);
    m_cancel = new QPushButton;
    m_cancel->setObjectName(QStringLiteral("roomCancel"));

    auto *form = new QFormLayout;
    auto addRow = [form](const char *name, QWidget *field) {
        auto *label = new QLabel;
        label->setObjectName(QString::fromLatin1(name));
        form->addRow(label, field);
    };
    addRow("roomCodeLabel", m_code);
    addRow("roomTypeLabel", m_type);
    addRow("roomBuildingLabel", m_building);
    addRow("roomFloorLabel", m_floor);
    addRow("roomPhoneLabel", m_phone);
    addRow("roomStatusLabel", m_status);
    addRow("roomDisturbLabel", m_doNotDisturb);

    auto *actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(m_cancel);
    actions->addWidget(m_save);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_error);
    layout->addLayout(form);
    layout->addLayout(actions);

    connect(m_save, &QPushButton::clicked, this, &RoomEditorDialog::save);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    retranslateUi();
}

void RoomEditorDialog::setRecord(const QJsonObject &row)
{
    m_id = row.value(QStringLiteral("id")).toInteger();
    m_version = row.value(QStringLiteral("version")).toInt();
    m_code->setText(row.value(QStringLiteral("code")).toString());
    const QJsonValue floor = row.value(QStringLiteral("floor"));
    m_floor->setText(floor.isNull() || floor.isUndefined() ? QString() : QString::number(floor.toInt()));
    const QJsonValue phone = row.value(QStringLiteral("phone"));
    m_phone->setText(phone.isNull() || phone.isUndefined() ? QString() : phone.toString());
    m_doNotDisturb->setChecked(row.value(QStringLiteral("do_not_disturb")).toBool());
    m_error->clear();
    loadLists();
    const qint64 typeId = row.value(QStringLiteral("room_type")).toObject().value(QStringLiteral("id")).toInteger();
    const int typeIndex = m_type->findData(typeId);
    if (typeIndex >= 0)
        m_type->setCurrentIndex(typeIndex);
    const QJsonValue building = row.value(QStringLiteral("building"));
    const qint64 buildingId = building.isObject() ? building.toObject().value(QStringLiteral("id")).toInteger() : 0;
    const int buildingIndex = m_building->findData(buildingId);
    if (buildingIndex >= 0)
        m_building->setCurrentIndex(buildingIndex);
    const QString status = row.value(QStringLiteral("status_code")).toString();
    if (!status.isEmpty()) {
        const int statusIndex = m_status->findData(status);
        if (statusIndex >= 0)
            m_status->setCurrentIndex(statusIndex);
        else
            m_status->addItem(statusText(status), status);
    }
    retranslateUi();
}

void RoomEditorDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        if (isVisible())
            loadLists();
    }
    QDialog::changeEvent(event);
}

void RoomEditorDialog::retranslateUi()
{
    auto setLabel = [this](const char *name, const QString &text) {
        if (auto *label = findChild<QLabel *>(QString::fromLatin1(name)))
            label->setText(text);
    };
    setWindowTitle(m_id <= 0 ? tr("New room") : tr("Edit room"));
    setLabel("roomCodeLabel", tr("Room"));
    setLabel("roomTypeLabel", tr("Type"));
    setLabel("roomBuildingLabel", tr("Building"));
    setLabel("roomFloorLabel", tr("Floor"));
    setLabel("roomPhoneLabel", tr("Phone"));
    setLabel("roomStatusLabel", tr("Status"));
    setLabel("roomDisturbLabel", tr("Do not disturb"));
    m_doNotDisturb->setText(tr("Do not disturb"));
    m_save->setText(tr("Save"));
    m_cancel->setText(tr("Cancel"));

    const QString status = m_status->currentData().toString();
    m_status->clear();
    for (const char *code : kStatusCodes)
        m_status->addItem(statusText(QLatin1String(code)), QLatin1String(code));
    int index = m_status->findData(status.isEmpty() ? QStringLiteral("vacant_ready") : status);
    if (index < 0 && !status.isEmpty()) {
        m_status->addItem(statusText(status), status);
        index = m_status->findData(status);
    }
    m_status->setCurrentIndex(index < 0 ? 0 : index);

    const qint64 buildingId = m_building->currentData().toLongLong();
    fillBuildings(m_buildings);
    const int buildingIndex = m_building->findData(buildingId);
    if (buildingIndex >= 0)
        m_building->setCurrentIndex(buildingIndex);
}

void RoomEditorDialog::fillTypes(const QJsonArray &items)
{
    const qint64 selected = m_type->currentData().toLongLong();
    m_type->clear();
    for (const QJsonValue &value : items) {
        const QJsonObject type = value.toObject();
        const QString name = type.value(QStringLiteral("name")).toString();
        const QString code = type.value(QStringLiteral("code")).toString();
        m_type->addItem(name.isEmpty() ? code : name, type.value(QStringLiteral("id")).toInteger());
    }
    const int index = m_type->findData(selected);
    if (index >= 0)
        m_type->setCurrentIndex(index);
}

void RoomEditorDialog::fillBuildings(const QJsonArray &items)
{
    const qint64 selected = m_building->currentData().toLongLong();
    m_building->clear();
    m_building->addItem(tr("None"), 0);
    for (const QJsonValue &value : items) {
        const QJsonObject building = value.toObject();
        const QString name = building.value(QStringLiteral("name")).toString();
        const QString code = building.value(QStringLiteral("code")).toString();
        m_building->addItem(name.isEmpty() ? code : name, building.value(QStringLiteral("id")).toInteger());
    }
    const int index = m_building->findData(selected);
    if (index >= 0)
        m_building->setCurrentIndex(index);
}

ApiResponse RoomEditorDialog::waitFor(quint64 requestId)
{
    ApiResponse found;
    QEventLoop loop;
    const QMetaObject::Connection finished = connect(m_api, &ApiClient::responseFinished, &loop, [&](const ApiResponse &response) {
        if (response.id != requestId)
            return;
        found = response;
        loop.quit();
    });
    const QMetaObject::Connection rejected = connect(m_api, &ApiClient::sessionRejected, &loop, [&](const QString &) { loop.quit(); });
    loop.exec();
    disconnect(finished);
    disconnect(rejected);
    return found;
}

void RoomEditorDialog::loadLists()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lang"), HotelLocale::currentCode());
    const ApiResponse types = waitFor(m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/room-types"), query, QByteArray(), true));
    if (types.ok && types.json.isObject()) {
        m_types = types.json.object().value(QStringLiteral("items")).toArray();
        fillTypes(m_types);
    } else if (!types.ok) {
        m_error->setText(types.error.userMessage.isEmpty() ? apiErrorMessage(types.error.httpStatus, types.error.code)
                                                           : types.error.userMessage);
    }
    const ApiResponse buildings = waitFor(m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/buildings"), query, QByteArray(), true));
    if (buildings.ok && buildings.json.isObject()) {
        m_buildings = buildings.json.object().value(QStringLiteral("items")).toArray();
        fillBuildings(m_buildings);
    } else if (!buildings.ok) {
        m_error->setText(buildings.error.userMessage.isEmpty() ? apiErrorMessage(buildings.error.httpStatus, buildings.error.code)
                                                               : buildings.error.userMessage);
    }
}

void RoomEditorDialog::save()
{
    const QString code = m_code->text().trimmed();
    if (code.isEmpty()) {
        m_error->setText(tr("Code is required."));
        return;
    }
    if (m_type->currentIndex() < 0 || m_type->currentData().toLongLong() <= 0) {
        m_error->setText(tr("Choose a room type."));
        return;
    }
    const QString floorText = m_floor->text().trimmed();
    int floor = 0;
    if (!floorText.isEmpty()) {
        bool ok = false;
        floor = floorText.toInt(&ok);
        if (!ok || floor < -32768 || floor > 32767) {
            m_error->setText(tr("Floor must be a whole number."));
            return;
        }
    }
    const QString phone = m_phone->text().trimmed();
    if (phone.size() > 32) {
        m_error->setText(tr("Phone is too long."));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("room_type_id"), m_type->currentData().toLongLong());
    const qint64 buildingId = m_building->currentData().toLongLong();
    body.insert(QStringLiteral("building_id"), buildingId > 0 ? QJsonValue(buildingId) : QJsonValue::Null);
    body.insert(QStringLiteral("floor"), floorText.isEmpty() ? QJsonValue::Null : QJsonValue(floor));
    body.insert(QStringLiteral("phone"), phone.isEmpty() ? QJsonValue::Null : QJsonValue(phone));
    body.insert(QStringLiteral("status_code"), m_status->currentData().toString());
    body.insert(QStringLiteral("do_not_disturb"), m_doNotDisturb->isChecked());

    m_save->setEnabled(false);
    ApiResponse response;
    if (m_id <= 0) {
        response = waitFor(m_api->request(HttpVerb::Post,
                                          QStringLiteral("/api/v1/rooms"),
                                          QUrlQuery(),
                                          QJsonDocument(body).toJson(QJsonDocument::Compact),
                                          true));
    } else {
        body.insert(QStringLiteral("version"), m_version);
        response = waitFor(m_api->request(HttpVerb::Patch,
                                          QStringLiteral("/api/v1/rooms/%1").arg(m_id),
                                          QUrlQuery(),
                                          QJsonDocument(body).toJson(QJsonDocument::Compact),
                                          true));
    }
    if (!response.ok) {
        m_save->setEnabled(true);
        m_error->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                               : response.error.userMessage);
        return;
    }
    accept();
}
