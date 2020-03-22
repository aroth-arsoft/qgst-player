#ifndef OPENNETWORKDIALOG_H
#define OPENNETWORKDIALOG_H

#include <QDialog>
#include "player.h"

QT_BEGIN_NAMESPACE
namespace Ui { class OpenNetworkDialog; }
QT_END_NAMESPACE



class OpenNetworkDialog : public QDialog
{
    Q_OBJECT
public:
    OpenNetworkDialog(QWidget * parent);
    ~OpenNetworkDialog() override;

    const QString & url() const;
    void setUrl(const QString & url);

    const QString & options() const;
    void setOptions(const QString & opts);

    const SourceSettings & source() const;
    void setSource(const SourceSettings & source);

public:
    void accept() override;

private:
    Ui::OpenNetworkDialog * ui;
    SourceSettings m_source;
};

#endif // OPENNETWORKDIALOG_H
