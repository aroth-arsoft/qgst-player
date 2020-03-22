#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "player.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void setPosition(int pos);

    void toggleFullScreen();

    void openFile();
    void openNetwork();

public:
    void showControls(bool show);
    void hideControls();

protected:
    void mouseMoveEvent(QMouseEvent *event);

    void onStateChanged();
    void onPositionChanged();

private:
    void restartPlayer();

private:
    Ui::MainWindow *ui;
    QTimer m_fullScreenTimer;
    SourceSettings m_currentSource;
};
#endif // MAINWINDOW_H
