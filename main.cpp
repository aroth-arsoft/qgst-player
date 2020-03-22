#include "MainWindow.h"

#include <QApplication>
#ifdef QGST_USE_QTGSTREAMER
#include <QGst/Init>
#else
#include <gst/gst.h>
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#ifdef QGST_USE_QTGSTREAMER
    QGst::init(&argc, &argv);
#else
    gst_init(&argc, &argv);
#endif

    MainWindow w;
    w.show();
    return a.exec();
}
