#include "OpenNetworkDialog.h"

#include "ui_OpenNetworkDialog.h"

OpenNetworkDialog::OpenNetworkDialog(QWidget * parent)
    : QDialog(parent)
    , ui(new Ui::OpenNetworkDialog)
{
    ui->setupUi(this);
}

OpenNetworkDialog::~OpenNetworkDialog()
{
    delete ui;
}


const QString & OpenNetworkDialog::url() const
{
    return m_source.uri;
}

void OpenNetworkDialog::setUrl(const QString & url)
{
    m_source.uri = url;
    ui->url->setText(m_source.uri);
}

const QString & OpenNetworkDialog::options() const
{
    return m_source.options;
}

void OpenNetworkDialog::setOptions(const QString & opts)
{
    m_source.options = opts;
    ui->options->setText(m_source.options);
}

const SourceSettings & OpenNetworkDialog::source() const
{
    return m_source;
}

void OpenNetworkDialog::setSource(const SourceSettings & source)
{
    m_source = source;
    ui->options->setText(m_source.options);
    ui->url->setText(m_source.uri);
    ui->lowLatency->setChecked(m_source.lowLatency);
    ui->bufferTime->setValue(m_source.bufferTime);
}

void OpenNetworkDialog::accept()
{
    m_source.setUri(ui->url->text());
    m_source.options = ui->options->text();
    m_source.lowLatency = ui->lowLatency->isChecked();
    m_source.bufferTime = ui->bufferTime->value();
    QDialog::accept();
}
