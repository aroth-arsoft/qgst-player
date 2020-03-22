/*
    Copyright (C) 2010 George Kiagiadakis <kiagiadakis.george@gmail.com>
    Copyright (C) 2011-2012 Collabora Ltd.
      @author George Kiagiadakis <george.kiagiadakis@collabora.co.uk>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "videowidget.h"
#ifdef QGST_USE_QTGSTREAMER
#include <QGst/VideoOverlay>
#include <QGst/Pipeline>
#include <QGst/Bus>
#include <QGst/Message>
#include <QGlib/Connect>
#include <QGlib/Signal>
#else
#include <gst/gstbus.h>
#endif
#include <QtCore/QDebug>
#include <QtCore/QMutex>
#include <QtCore/QThread>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QResizeEvent>

# include <QtWidgets/QApplication>
# include <QtWidgets/QHBoxLayout>

# include <QtOpenGL/QGLWidget>

namespace QGst {
namespace Ui {

class AbstractRenderer
{
public:
    static AbstractRenderer *create(const ElementPtr & sink, QWidget *videoWidget);

    virtual ~AbstractRenderer() {}
    virtual ElementPtr videoSink() const = 0;
};


class VideoOverlayRenderer : public QObject, public AbstractRenderer
{
public:
    VideoOverlayRenderer(QWidget *parent)
        : QObject(parent)
          , m_sink(nullptr)
    {
        m_windowId = widget()->winId(); //create a new X window (if we are on X11 with alien widgets)

        widget()->installEventFilter(this);
        widget()->setAttribute(Qt::WA_NoSystemBackground, true);
        widget()->setAttribute(Qt::WA_PaintOnScreen, true);
        widget()->update();
    }

    virtual ~VideoOverlayRenderer()
    {
        if (m_sink) {
#ifdef QGST_USE_QTGSTREAMER
            m_sink->setWindowHandle(0);
#else
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_sink), 0);
#endif
        }
        widget()->removeEventFilter(this);
        widget()->setAttribute(Qt::WA_NoSystemBackground, false);
        widget()->setAttribute(Qt::WA_PaintOnScreen, false);
        widget()->update();
    }

    void setVideoSink(const VideoOverlayPtr & sink)
    {
        QMutexLocker l(&m_sinkMutex);
        if (m_sink) {
#ifdef QGST_USE_QTGSTREAMER
            m_sink->setWindowHandle(0);
#else
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_sink), 0);
#endif
        }
        m_sink = sink;
        if (m_sink) {
#ifdef QGST_USE_QTGSTREAMER
            m_sink->setWindowHandle(m_windowId);
#else
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_sink), m_windowId);
#endif
        }
    }

    ElementPtr videoSink() const override
    {
        QMutexLocker l(&m_sinkMutex);
#ifdef QGST_USE_QTGSTREAMER
        return m_sink.dynamicCast<Element>();
#else
        return GST_ELEMENT_CAST(m_sink);
#endif
    }

protected:
    bool eventFilter(QObject *filteredObject, QEvent *event) override
    {
        if (filteredObject == parent() && event->type() == QEvent::Paint) {
            QMutexLocker l(&m_sinkMutex);

#ifdef QGST_USE_QTGSTREAMER
            State currentState = m_sink ? m_sink.dynamicCast<Element>()->currentState() : StateNull;
            if (currentState == StatePlaying || currentState == StatePaused) {
                m_sink->expose();
#else
            State currentState = m_sink ? get_current_state(GST_ELEMENT_CAST(m_sink)) : StateNull;
            if (currentState == StatePlaying || currentState == StatePaused) {
                gst_video_overlay_expose(GST_VIDEO_OVERLAY(m_sink));
#endif
            } else {
                QPainter p(widget());
                p.fillRect(widget()->rect(), Qt::black);
            }
            return true;
        } else {
            return QObject::eventFilter(filteredObject, event);
        }
    }

private:
    inline QWidget *widget() { return static_cast<QWidget*>(parent()); }
    WId m_windowId;
    mutable QMutex m_sinkMutex;
    VideoOverlayPtr m_sink;
};

class QtVideoSinkRenderer : public QObject, public AbstractRenderer
{
#ifndef QGST_USE_QTGSTREAMER
    static void update_cb(QWidget * widget, gpointer data)
    {
        Q_UNUSED(widget);
        ((QtVideoSinkRenderer*)data)->onUpdate();
    }
#endif
public:
    QtVideoSinkRenderer(const ElementPtr & sink, QWidget *parent)
        : QObject(parent), m_sink(sink)
    {
#ifdef QGST_USE_QTGSTREAMER
        QGlib::connect(sink, "update", this, &QtVideoSinkRenderer::onUpdate);
#else
        g_signal_connect (m_sink, "update", G_CALLBACK (update_cb), this);
#endif
        parent->installEventFilter(this);
        parent->setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    virtual ~QtVideoSinkRenderer()
    {
        widget()->removeEventFilter(this);
        widget()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

    ElementPtr videoSink() const override { return m_sink; }

protected:
    bool eventFilter(QObject *filteredObject, QEvent *event) override
    {
        if (filteredObject == parent() && event->type() == QEvent::Paint) {
            QPainter painter(widget());
            QRect targetArea = widget()->rect();
#ifdef QGST_USE_QTGSTREAMER
            QGlib::emit<void>(m_sink, "paint", (void*) &painter,
                              (qreal) targetArea.x(), (qreal) targetArea.y(),
                              (qreal) targetArea.width(), (qreal) targetArea.height());
#else
            GType itype = G_TYPE_FROM_INSTANCE(m_sink);
            guint signal_id = g_signal_lookup("paint", itype);
            GQuark detail = 0;

            //initialize arguments array
            const size_t size = 5;
            GValue *values = new GValue[size + 1];
            memset(values, 0, sizeof(GValue) * (size + 1));

            //set instance
            g_value_init(&values[0], itype);
            g_value_set_instance(&values[0], m_sink);

            g_value_init(&values[1], G_TYPE_POINTER);
            g_value_set_instance(&values[1], &painter);

            g_value_init(&values[2], G_TYPE_FLOAT);
            g_value_init(&values[3], G_TYPE_FLOAT);
            g_value_init(&values[4], G_TYPE_FLOAT);
            g_value_init(&values[5], G_TYPE_FLOAT);
            g_value_set_float(&values[2], targetArea.x());
            g_value_set_float(&values[3], targetArea.y());
            g_value_set_float(&values[4], targetArea.width());
            g_value_set_float(&values[5], targetArea.height());

            //initialize return value
            GValue returnValue = G_VALUE_INIT;

            //emit the signal
            g_signal_emitv(values, signal_id, detail, &returnValue);

            delete[] values;
#endif
            return true;
        } else {
            return QObject::eventFilter(filteredObject, event);
        }
    }

private:
    inline QWidget *widget() { return static_cast<QWidget*>(parent()); }
    void onUpdate() { widget()->update(); }

    ElementPtr m_sink;
};


#ifndef QTGSTREAMER_UI_NO_OPENGL

class QtGLVideoSinkRenderer : public AbstractRenderer
{
public:
    QtGLVideoSinkRenderer(const ElementPtr & sink, QWidget *parent)
    {
        m_layout = new QHBoxLayout(parent);
        m_glWidget = new QGLWidget(parent);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->addWidget(m_glWidget);
        parent->setLayout(m_layout);

        m_renderer = new QtVideoSinkRenderer(sink, m_glWidget);

        m_glWidget->makeCurrent();
#ifdef QGST_USE_QTGSTREAMER
        sink->setProperty("glcontext", (void*) QGLContext::currentContext());
#else
        g_object_set (sink, "glcontext", (void*) QGLContext::currentContext(), NULL);
#endif
        m_glWidget->doneCurrent();
    }

    ~QtGLVideoSinkRenderer() override
    {
        delete m_renderer;
        delete m_glWidget;
        delete m_layout;
    }

    ElementPtr videoSink() const override { return m_renderer->videoSink(); }

private:
    QtVideoSinkRenderer *m_renderer;
    QHBoxLayout *m_layout;
    QGLWidget *m_glWidget;
};

#endif // QTGSTREAMER_UI_NO_OPENGL


class QWidgetVideoSinkRenderer : public AbstractRenderer
{
public:
    QWidgetVideoSinkRenderer(const ElementPtr & sink, QWidget *parent)
        : m_sink(sink)
    {
        //GValue of G_TYPE_POINTER can only be set as void* in the bindings
#ifdef QGST_USE_QTGSTREAMER
        m_sink->setProperty<void*>("widget", parent);
#else
        g_object_set (m_sink, "widget", parent, NULL);
#endif
    }

    virtual ~QWidgetVideoSinkRenderer()
    {
#ifdef QGST_USE_QTGSTREAMER
        m_sink->setProperty<void*>("widget", NULL);
#else
        g_object_set (m_sink, "widget", NULL, NULL);
#endif
    }

    ElementPtr videoSink() const override { return m_sink; }

private:
    ElementPtr m_sink;
};

class PipelineWatch : public QObject, public AbstractRenderer
{
#ifndef QGST_USE_QTGSTREAMER
    static gboolean syncMessageCallback(GstBus* bus, GstMessage* message, gpointer data)
    {
        Q_UNUSED(bus);
        Q_ASSERT(GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT);
        PipelineWatch* obj = static_cast<PipelineWatch*>(data);
        obj->onBusSyncMessage(message);
        return TRUE;
    }
#endif
public:
    PipelineWatch(const PipelinePtr & pipeline, QWidget *parent)
        : QObject(parent), m_renderer(new VideoOverlayRenderer(parent)), m_pipeline(pipeline)
    {
#ifdef QGST_USE_QTGSTREAMER
        pipeline->bus()->enableSyncMessageEmission();
        QGlib::connect(pipeline->bus(), "sync-message",
                       this, &PipelineWatch::onBusSyncMessage);
#else
        GstBus * bus = gst_pipeline_get_bus(m_pipeline);
        gst_bus_enable_sync_message_emission(bus);

        gst_bus_set_sync_handler(bus, gst_bus_sync_signal_handler, this, nullptr);
        g_signal_connect(bus, "sync-message::element", G_CALLBACK(syncMessageCallback), this);
#endif
    }

    virtual ~PipelineWatch()
    {
#ifdef QGST_USE_QTGSTREAMER
        m_pipeline->bus()->disableSyncMessageEmission();
#else
        GstBus * bus = gst_pipeline_get_bus(m_pipeline);
        gst_bus_disable_sync_message_emission(bus);
#endif
        delete m_renderer;
    }

    ElementPtr videoSink() const override { return m_renderer->videoSink(); }

    void releaseSink() { m_renderer->setVideoSink(VideoOverlayPtr()); }

private:
    void onBusSyncMessage(const MessagePtr & msg)
    {
#ifdef QGST_USE_QTGSTREAMER
        switch (msg->type()) {
        case MessageElement:
            if (VideoOverlay::isPrepareWindowHandleMessage(msg)) {
                VideoOverlayPtr overlay = msg->source().dynamicCast<VideoOverlay>();
                m_renderer->setVideoSink(overlay);
            }
            break;
        case MessageStateChanged:
            //release the sink when it goes back to null state
            if (msg.staticCast<StateChangedMessage>()->newState() == StateNull &&
                msg->source() == m_renderer->videoSink())
            {
                releaseSink();
            }
        default:
            break;
        }
#else
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ELEMENT:
            if (gst_is_video_overlay_prepare_window_handle_message(msg)) {
                VideoOverlayPtr overlay = GST_VIDEO_OVERLAY(GST_MESSAGE_SRC (msg));
                m_renderer->setVideoSink(overlay);
            }
            break;
        case GST_MESSAGE_STATE_CHANGED:
            //release the sink when it goes back to null state
            if (GST_MESSAGE_SRC (msg) == GST_OBJECT (m_renderer->videoSink())) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
                if(new_state == GST_STATE_NULL)
                    releaseSink();
            }
        default:
            break;
        }
#endif
    }

private:
    VideoOverlayRenderer *m_renderer;
    PipelinePtr m_pipeline;
};

#ifdef QGST_USE_QTGSTREAMER
#define QGST_TYPE_NAME_FROM_INSTANCE(inst) QGlib::Type::fromInstance(inst).name()
#else
#define QGST_TYPE_NAME_FROM_INSTANCE(inst) g_type_name(G_TYPE_FROM_INSTANCE(inst))
#endif

AbstractRenderer *AbstractRenderer::create(const ElementPtr & sink, QWidget *videoWidget)
{
    VideoOverlayPtr overlay;
#ifdef QGST_USE_QTGSTREAMER
    overlay = sink.dynamicCast<VideoOverlay>();
#else
    overlay = GST_VIDEO_OVERLAY(sink);
#endif
    if (overlay) {
        VideoOverlayRenderer *r = new VideoOverlayRenderer(videoWidget);
        r->setVideoSink(overlay);
        return r;
    }

    G_TYPE_FROM_INSTANCE(sink);
    if (QGST_TYPE_NAME_FROM_INSTANCE(sink) == QLatin1String("GstQtVideoSink"
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
      "_qt5"
#endif
    )) {
        return new QtVideoSinkRenderer(sink, videoWidget);
    }

#ifndef QTGSTREAMER_UI_NO_OPENGL
    if (QGST_TYPE_NAME_FROM_INSTANCE(sink) == QLatin1String("GstQtGLVideoSink"
# if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
      "_qt5"
# endif
    )) {
        return new QtGLVideoSinkRenderer(sink, videoWidget);
    }
#endif

    if (QGST_TYPE_NAME_FROM_INSTANCE(sink) == QLatin1String("GstQWidgetVideoSink"
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
      "_qt5"
#endif
    )) {
        return new QWidgetVideoSinkRenderer(sink, videoWidget);
    }

    return NULL;
}


VideoWidget::VideoWidget(QWidget *parent, Qt::WindowFlags f)
    : QWidget(parent, f), d(NULL)
{
}

VideoWidget::~VideoWidget()
{
    delete d;
}

ElementPtr VideoWidget::videoSink() const
{
    return d ? d->videoSink() : ElementPtr();
}

void VideoWidget::setVideoSink(const ElementPtr & sink)
{
    if (!sink) {
        releaseVideoSink();
        return;
    }

    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());
    Q_ASSERT(d == NULL);

    d = AbstractRenderer::create(sink, this);

    if (!d) {
        qCritical() << "QGst::Ui::VideoWidget: Could not construct a renderer for the specified element";
    }
}

void VideoWidget::releaseVideoSink()
{
    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());

    if (d) {
        PipelineWatch *pw = dynamic_cast<PipelineWatch*>(d);
        if (pw) {
            pw->releaseSink();
        } else {
            delete d;
            d = NULL;
        }
    }
}

void VideoWidget::watchPipeline(const PipelinePtr & pipeline)
{
    if (!pipeline) {
        stopPipelineWatch();
        return;
    }

    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());
    Q_ASSERT(d == NULL);

    d = new PipelineWatch(pipeline, this);
}

void VideoWidget::stopPipelineWatch()
{
    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());

    if (dynamic_cast<PipelineWatch*>(d)) {
        delete d;
        d = NULL;
    }
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(event->rect(), Qt::black);
}

QPaintEngine * VideoWidget::paintEngine() const
{
    return nullptr;
}

} //namespace Ui
} //namespace QGst
