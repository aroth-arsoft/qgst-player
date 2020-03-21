#include "MainWindow.h"
#include "./ui_MainWindow.h"

#include <QStyle>
#include <QFileDialog>
#include <QInputDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
      , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //this timer (re-)hides the controls after a few seconds when we are in fullscreen mode
    m_fullScreenTimer.setSingleShot(true);
    connect(&m_fullScreenTimer, &QTimer::timeout, this, &MainWindow::hideControls);

    connect(ui->player, &Player::positionChanged, this, &MainWindow::onPositionChanged);
    connect(ui->player, &Player::stateChanged, this, &MainWindow::onStateChanged);

    ui->volumeLabel->setPixmap(
        style()->standardIcon(QStyle::SP_MediaVolume).pixmap(QSize(32, 32),
                                                             QIcon::Normal, QIcon::On));

    m_currentUrl = QLatin1String("rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov");

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        setMouseTracking(false);
        ui->player->setMouseTracking(false);
        m_fullScreenTimer.stop();
        showControls(true);
        showNormal();
    } else {
        setMouseTracking(true);
        ui->player->setMouseTracking(true);
        hideControls();
        showFullScreen();
    }
}

void MainWindow::hideControls()
{
    showControls(false);
}

void MainWindow::showControls(bool show)
{
#if 0
    m_openButton->setVisible(show);
    m_playButton->setVisible(show);
    m_pauseButton->setVisible(show);
    m_stopButton->setVisible(show);
    m_fullScreenButton->setVisible(show);
    m_positionSlider->setVisible(show);
    m_volumeSlider->setVisible(show);
    m_volumeLabel->setVisible(show);
    m_positionLabel->setVisible(show);
#endif
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (isFullScreen()) {
        showControls(true);
        m_fullScreenTimer.start(3000); //re-hide controls after 3s
    }
}

/* Called when the user changes the slider's position */
void MainWindow::setPosition(int value)
{
    uint length = -ui->player->length().msecsTo(QTime(0,0));
    if (length != 0 && value > 0) {
        QTime pos(0,0);
        pos = pos.addMSecs(length * (value / 1000.0));
        ui->player->setPosition(pos);
    }
}

void MainWindow::onStateChanged()
{
    QGst::State newState = ui->player->state();
    ui->actionPlay->setEnabled(newState != QGst::StatePlaying);
    ui->actionPause->setEnabled(newState == QGst::StatePlaying);
    ui->actionStop->setEnabled(newState != QGst::StateNull);
    ui->positionSlider->setEnabled(newState != QGst::StateNull);
    ui->volumeSlider->setEnabled(newState != QGst::StateNull);
    ui->volumeLabel->setEnabled(newState != QGst::StateNull);
    ui->volumeSlider->setValue(ui->player->volume());

    //if we are in Null state, call onPositionChanged() to restore
    //the position of the slider and the text on the label
    if (newState == QGst::StateNull) {
        onPositionChanged();
    }
}

/* Called when the positionChanged() is received from the player */
void MainWindow::onPositionChanged()
{
    QTime length(0,0);
    QTime curpos(0,0);

    if (ui->player->state() != QGst::StateReady &&
        ui->player->state() != QGst::StateNull)
    {
        length = ui->player->length();
        curpos = ui->player->position();
    }

    ui->positionLabel->setText(curpos.toString("hh:mm:ss.zzz")
                             + "/" +
                             length.toString("hh:mm:ss.zzz"));

    if (length != QTime(0,0)) {
        ui->positionSlider->setValue(curpos.msecsTo(QTime(0,0)) * 1000 / length.msecsTo(QTime(0,0)));
    } else {
        ui->positionSlider->setValue(0);
    }

    if (curpos != QTime(0,0)) {
        ui->positionLabel->setEnabled(true);
        ui->positionSlider->setEnabled(true);
    }
}

void MainWindow::openUrl(const QString & s)
{
    m_currentUrl = s;

    ui->player->stop();
    ui->player->setUri(m_currentUrl);
    ui->player->play();
}


void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open a Movie"), m_currentUrl);

    if (!fileName.isEmpty()) {
        openUrl(fileName);
    }
}

void MainWindow::openNetwork()
{
    QString url = QInputDialog::getText(this, tr("Open a URL"), tr("URL"), QLineEdit::Normal, m_currentUrl);

    if (!url.isEmpty()) {
        openUrl(url);
    }
}

