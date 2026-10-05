#include "wexcelyavailability.h"
#include "ui_wexcelyavailability.h"

#include "excely/pms/pmsconnectclient.h"
#include "excelyappconfig.h"
#include "message.h"

#include <QApplication>
#include <QDateTime>
#include <QSet>

WExcelyAvailability::WExcelyAvailability(QWidget *parent)
    : BaseWidget(parent)
    , ui(new Ui::WExcelyAvailability)
{
    ui->setupUi(this);
    ui->deStart->setDate(QDate::currentDate());
    ui->deEnd->setDate(QDate::currentDate().addDays(30));
    ui->spMinLOS->setValue(0);
    ui->cbInvBlock->setEnabled(false);
    on_rbRatePlan_toggled(true);

    const ExcelyAppConfig cfg = ExcelyAppConfig::load();
    ui->leHotelCode->setText(cfg.pms.hotelCode);
}

WExcelyAvailability::~WExcelyAvailability()
{
    delete ui;
}

void WExcelyAvailability::setupTab()
{
    setupTabTextAndIcon(tr("Excely availability"), QStringLiteral(":/images/configure.png"));
}

void WExcelyAvailability::appendLog(const QString &line)
{
    ui->teLog->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
}

bool WExcelyAvailability::ensureClient(Excely::Pms::Config *cfg, QString *errorText)
{
    const ExcelyAppConfig app = ExcelyAppConfig::load();
    if (!app.hasCredentials(errorText)) {
        return false;
    }
    ui->leHotelCode->setText(app.pms.hotelCode);
    if (cfg) {
        *cfg = app.pms;
    }
    return true;
}

void WExcelyAvailability::fillCombosFromCatalog()
{
    ui->cbRoomType->clear();
    ui->cbRatePlan->clear();
    ui->cbInvBlock->clear();

    for (const Excely::Pms::RoomTypeInfo &rt : m_catalog.roomTypes) {
        const QString label = rt.name.isEmpty()
                ? rt.roomTypeCode
                : QStringLiteral("%1 — %2").arg(rt.roomTypeCode, rt.name);
        ui->cbRoomType->addItem(label, rt.roomTypeCode);
    }

    QSet<QString> blocks;
    for (const Excely::Pms::RatePlanInfo &rp : m_catalog.ratePlans) {
        if (!rp.ratePlanCode.isEmpty()) {
            const QString label = rp.name.isEmpty()
                    ? rp.ratePlanCode
                    : QStringLiteral("%1 — %2").arg(rp.ratePlanCode, rp.name);
            ui->cbRatePlan->addItem(label, rp.ratePlanCode);
        }
        if (!rp.invBlockCode.isEmpty() && !blocks.contains(rp.invBlockCode)) {
            blocks.insert(rp.invBlockCode);
            ui->cbInvBlock->addItem(rp.invBlockCode, rp.invBlockCode);
        }
    }
}

void WExcelyAvailability::on_rbRatePlan_toggled(bool checked)
{
    ui->cbRatePlan->setEnabled(checked);
    ui->cbInvBlock->setEnabled(!checked);
}

void WExcelyAvailability::on_btnRefreshCatalog_clicked()
{
    QString err;
    Excely::Pms::Config cfg;
    if (!ensureClient(&cfg, &err)) {
        message_error(err);
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    Excely::Pms::PmsConnectClient client(cfg);
    Excely::Pms::HotelCatalog catalog;
    QVector<Excely::Channel::PmsError> errors;
    const bool ok = client.hotelAvail(&catalog, &errors);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QStringList lines;
        for (const Excely::Channel::PmsError &e : errors) {
            lines << QStringLiteral("%1: %2").arg(e.code).arg(e.text);
        }
        message_error(lines.isEmpty() ? tr("Failed to load Excely catalog") : lines.join(QLatin1Char('\n')));
        appendLog(tr("Catalog refresh failed"));
        return;
    }

    m_catalog = catalog;
    fillCombosFromCatalog();
    appendLog(tr("Catalog: %1 room types, %2 rate plans")
                  .arg(m_catalog.roomTypes.size())
                  .arg(m_catalog.ratePlans.size()));
}

bool WExcelyAvailability::buildMessage(Excely::Pms::AvailStatusMessage *msg, QString *errorText) const
{
    if (!msg) {
        return false;
    }
    msg->invTypeCode = ui->cbRoomType->currentData().toString();
    if (msg->invTypeCode.isEmpty()) {
        if (errorText) {
            *errorText = tr("Select a room type (refresh catalog first)");
        }
        return false;
    }

    if (ui->rbRatePlan->isChecked()) {
        msg->ratePlanCode = ui->cbRatePlan->currentData().toString();
        msg->invBlockCode.clear();
        if (msg->ratePlanCode.isEmpty()) {
            if (errorText) {
                *errorText = tr("Select a rate plan");
            }
            return false;
        }
    } else {
        msg->invBlockCode = ui->cbInvBlock->currentData().toString();
        msg->ratePlanCode.clear();
        if (msg->invBlockCode.isEmpty()) {
            if (errorText) {
                *errorText = tr("Select an inventory block");
            }
            return false;
        }
    }

    msg->start = ui->deStart->date();
    msg->end = ui->deEnd->date();
    if (!msg->start.isValid() || !msg->end.isValid() || msg->end < msg->start) {
        if (errorText) {
            *errorText = tr("Invalid date range");
        }
        return false;
    }

    msg->sendBookingLimit = true;
    msg->bookingLimit = ui->spBookingLimit->value();

    switch (ui->cbStatus->currentIndex()) {
    case 0:
        msg->restrictionStatus = QStringLiteral("Open");
        break;
    case 1:
        msg->restrictionStatus = QStringLiteral("Close");
        break;
    default:
        msg->restrictionStatus.clear();
        break;
    }

    msg->minLOS = ui->spMinLOS->value() > 0 ? ui->spMinLOS->value() : -1;
    return true;
}

bool WExcelyAvailability::sendMessages(const QVector<Excely::Pms::AvailStatusMessage> &messages)
{
    QString err;
    Excely::Pms::Config cfg;
    if (!ensureClient(&cfg, &err)) {
        message_error(err);
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    Excely::Pms::PmsConnectClient client(cfg);
    QVector<Excely::Channel::PmsError> errors;
    const bool ok = client.hotelAvailNotif(messages, &errors);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QStringList lines;
        for (const Excely::Channel::PmsError &e : errors) {
            lines << QStringLiteral("%1: %2").arg(e.code).arg(e.text);
        }
        message_error(lines.isEmpty() ? tr("HotelAvailNotif failed") : lines.join(QLatin1Char('\n')));
        appendLog(tr("Upload failed"));
        return false;
    }

    for (const Excely::Channel::PmsError &e : errors) {
        appendLog(QStringLiteral("WARN %1: %2").arg(e.code).arg(e.text));
    }
    return true;
}

void WExcelyAvailability::on_btnUpload_clicked()
{
    Excely::Pms::AvailStatusMessage msg;
    QString err;
    if (!buildMessage(&msg, &err)) {
        message_error(err);
        return;
    }
    if (sendMessages({msg})) {
        appendLog(tr("Uploaded %1 / %2–%3 limit=%4")
                      .arg(msg.invTypeCode,
                           msg.start.toString(Qt::ISODate),
                           msg.end.toString(Qt::ISODate))
                      .arg(msg.bookingLimit));
        message_info(tr("Availability uploaded"));
    }
}

void WExcelyAvailability::on_btnResyncYear_clicked()
{
    Excely::Pms::AvailStatusMessage msg;
    QString err;
    if (!buildMessage(&msg, &err)) {
        message_error(err);
        return;
    }
    msg.end = msg.start.addDays(365);
    ui->deEnd->setDate(msg.end);
    if (sendMessages({msg})) {
        appendLog(tr("Resync year: %1 / %2–%3 limit=%4")
                      .arg(msg.invTypeCode,
                           msg.start.toString(Qt::ISODate),
                           msg.end.toString(Qt::ISODate))
                      .arg(msg.bookingLimit));
        message_info(tr("Year resync uploaded"));
    }
}
