#ifndef WEXCELYAVAILABILITY_H
#define WEXCELYAVAILABILITY_H

#include "basewidget.h"
#include "excely/pms/models.h"
#include "excely/pms/pmsconfig.h"

namespace Ui {
class WExcelyAvailability;
}

class WExcelyAvailability : public BaseWidget
{
    Q_OBJECT

public:
    explicit WExcelyAvailability(QWidget *parent = nullptr);
    ~WExcelyAvailability() override;
    void setupTab() override;

private slots:
    void on_btnRefreshCatalog_clicked();
    void on_btnUpload_clicked();
    void on_btnResyncYear_clicked();
    void on_rbRatePlan_toggled(bool checked);

private:
    Ui::WExcelyAvailability *ui;
    Excely::Pms::HotelCatalog m_catalog;

    bool ensureClient(Excely::Pms::Config *cfg, QString *errorText);
    bool buildMessage(Excely::Pms::AvailStatusMessage *msg, QString *errorText) const;
    bool sendMessages(const QVector<Excely::Pms::AvailStatusMessage> &messages);
    void fillCombosFromCatalog();
    void appendLog(const QString &line);
};

#endif // WEXCELYAVAILABILITY_H
