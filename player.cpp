/*
    Copyright (C) 2010 Marco Ballesio <gibrovacco@gmail.com>
    Copyright (C) 2011 Collabora Ltd.
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
#include "player.h"
#include <QDir>
#include <QUrl>
#include <QDebug>

#ifdef QGST_USE_QTGSTREAMER
#include <QGlib/Connect>
#include <QGlib/Error>
#include <QGst/Pipeline>
#include <QGst/Parse>
#include <QGst/ElementFactory>
#include <QGst/Bus>
#include <QGst/Message>
#include <QGst/Query>
#include <QGst/ClockTime>
#include <QGst/Event>
#include <QGst/StreamVolume>
#else
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/audio/streamvolume.h>
#endif

#include <QStandardPaths>
#include <sstream>

QString getDemoDirectory()
{
    static QStringList locations = QStandardPaths::standardLocations(QStandardPaths::MoviesLocation);
    return locations.front();
}

void SourceSettings::setUri(const QString & url)
{
    QString filename = url;
    QUrl filenameAsUrl(filename);
    QString filenameScheme = filenameAsUrl.scheme();
    // handle filenames like "C:\me\too.mpv", which gets a scheme of "C"
    if (filenameScheme.length() == 1)
        filenameScheme.clear();
    if (filenameAsUrl.host().isEmpty() && (filenameScheme.isEmpty() || filenameScheme.compare("file") == 0) && !filename.isEmpty())
    {
#ifndef _WIN32
        // replace all backslash chars by slash so the cleanPath
        // can do its magic
        filename.replace(QChar('\\'), QChar('/'));
#endif
        // try media path
        bool ok = QFile::exists(filename);
        if (ok)
            filename = QDir::cleanPath(QDir().absoluteFilePath(filename));
        else
            filename = QDir::cleanPath(QDir(getDemoDirectory()).absoluteFilePath(filename));
        if(!filename.isEmpty() && filename.at(0) == '/')
            uri = "file://" + filename;
        else
            uri = "file:///" + filename;
        // make this URL completely legal without any windoof stuff
        uri.replace(QChar('\\'), QChar('/'));
        isLocalFile = true;
        isRTSP = false;
        isFTP = false;
        isUDP = false;
        noStreamingUriAvailable = false;
    }
    else
    {
        if (filenameScheme.compare("rtsp") == 0)
            isRTSP = true;
        else if (filenameScheme.compare("ftp") == 0)
            isFTP = true;
        else if (filenameScheme.compare("udp") == 0)
            isUDP = true;

        if(isRTSP || isFTP)
        {
            username = filenameAsUrl.userName();
            password = filenameAsUrl.password();
            uri = filenameAsUrl.toEncoded(QUrl::FullyEncoded | QUrl::RemoveUserInfo).constData();
        }
        else if(isUDP)
        {
            // drop any username/password
            uri = filenameAsUrl.toEncoded(QUrl::FullyEncoded | QUrl::RemoveUserInfo).constData();
        }
        else
            uri = url;
        noStreamingUriAvailable = uri.isEmpty();
    }
}

SourceSettings getSourceSettings(const QString & url, const QString & options)
{
    SourceSettings ret;
    ret.bufferTime = 0;
    ret.useAudio = false;
    ret.isLocalFile = false;
    ret.noStreamingUriAvailable = true;
    ret.pauseMode = false;

    QString uri = url;
    ret.options = options;



    return ret;
}


Player::Player(QWidget *parent)
    : QGst::Ui::VideoWidget(parent)
      , m_pipeline(nullptr)
      , m_pipelineConnectId(0)
{
    //this timer is used to tell the ui to change its position slider & label
    //every 100 ms, but only when the pipeline is playing
    connect(&m_positionTimer, &QTimer::timeout, this, &Player::positionChanged);
}

Player::~Player()
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER
        m_pipeline->setState(QGst::StateNull);
#else
        gst_element_set_state (GST_ELEMENT_CAST(m_pipeline), GST_STATE_NULL);
#endif
        stopPipelineWatch();
    }
}

void Player::setUri(const QString & uri)
{
    QString realUri = uri;

    //if uri is not a real uri, assume it is a file path
    int i = realUri.indexOf("://");
    if (i < 0) {
        realUri = QUrl::fromLocalFile(realUri).toEncoded();
    }

    QGst::PipelinePtr pipeline;
#ifdef QGST_USE_QTGSTREAMER
    pipeline = QGst::ElementFactory::make("playbin").dynamicCast<QGst::Pipeline>();
    if(pipeline)
         pipeline->setProperty("uri", realUri);
#else
    pipeline = GST_PIPELINE_CAST(gst_element_factory_make ("playbin", "playbin"));
    if(pipeline)
        g_object_set (pipeline, "uri", realUri.toLocal8Bit().constData(), NULL);
#endif
    setPipeline(pipeline);
}

QTime Player::position() const
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER
        //here we query the pipeline about its position
        //and we request that the result is returned in time format
        QGst::PositionQueryPtr query = QGst::PositionQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->position()).toTime();
#else
        gint64 position;
        if (!gst_element_query_position(GST_ELEMENT_CAST(m_pipeline), GST_FORMAT_TIME, &position))
            position = 0;
        return QGst::GstClockTime_to_QTime(position);
#endif
    } else {
        return QTime(0,0);
    }
}

void Player::setPosition(const QTime & pos)
{
#ifdef QGST_USE_QTGSTREAMER

    QGst::SeekEventPtr evt = QGst::SeekEvent::create(
        1.0, QGst::FormatTime, QGst::SeekFlagFlush,
        QGst::SeekTypeSet, QGst::ClockTime::fromTime(pos),
        QGst::SeekTypeNone, QGst::ClockTime::None
    );

    m_pipeline->sendEvent(evt);
#else
    GstEvent * event = gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, QGst::GstClockTime_from_QTime(pos), GST_SEEK_TYPE_NONE, 0);
    gst_element_send_event(GST_ELEMENT_CAST(m_pipeline), event);
#endif
}

int Player::volume() const
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER

        QGst::StreamVolumePtr svp =
            m_pipeline.dynamicCast<QGst::StreamVolume>();

        if (svp) {
            return svp->volume(QGst::StreamVolumeFormatCubic) * 10;
        }
#else
        GstStreamVolume * svp = GST_STREAM_VOLUME(m_pipeline);
        if(svp)
            return gst_stream_volume_get_volume(svp, GST_STREAM_VOLUME_FORMAT_CUBIC) * 10;
#endif
    }

    return 0;
}


void Player::setVolume(int volume)
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER
        QGst::StreamVolumePtr svp =
            m_pipeline.dynamicCast<QGst::StreamVolume>();

        if(svp) {
            svp->setVolume((double)volume / 10, QGst::StreamVolumeFormatCubic);
        }
#else
        GstStreamVolume * svp = GST_STREAM_VOLUME(m_pipeline);
        if(svp)
            gst_stream_volume_set_volume(svp, GST_STREAM_VOLUME_FORMAT_CUBIC, (double)volume / 10);
#endif
    }
}

QTime Player::length() const
{
    if (m_pipeline) {
        //here we query the pipeline about the content's duration
        //and we request that the result is returned in time format
#ifdef QGST_USE_QTGSTREAMER
        QGst::DurationQueryPtr query = QGst::DurationQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->duration()).toTime();
#else
        gint64 duration;
        if (!gst_element_query_duration(GST_ELEMENT_CAST(m_pipeline), GST_FORMAT_TIME, &duration))
            duration = 0;
        return QGst::GstClockTime_to_QTime(duration);
#endif
    } else {
        return QTime(0,0);
    }
}

QGst::State Player::state() const
{
#ifdef QGST_USE_QTGSTREAMER
    return m_pipeline ? m_pipeline->currentState() : QGst::StateNull;
#else
    return m_pipeline ? QGst::get_current_state(GST_ELEMENT_CAST(m_pipeline)) : QGst::StateNull;
#endif
}

void Player::play()
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER
        m_pipeline->setState(QGst::StatePlaying);
#else
        gst_element_set_state (GST_ELEMENT_CAST(m_pipeline), GST_STATE_PLAYING);
#endif
    }
}

void Player::pause()
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER
        m_pipeline->setState(QGst::StatePaused);
#else
        gst_element_set_state (GST_ELEMENT_CAST(m_pipeline), GST_STATE_PAUSED);
#endif
    }
}

void Player::stop()
{
    if (m_pipeline) {
#ifdef QGST_USE_QTGSTREAMER
        m_pipeline->setState(QGst::StateNull);
#else
        gst_element_set_state (GST_ELEMENT_CAST(m_pipeline), GST_STATE_NULL);
#endif

        //once the pipeline stops, the bus is flushed so we will
        //not receive any StateChangedMessage about this.
        //so, to inform the ui, we have to emit this signal manually.
        Q_EMIT stateChanged();
    }
}
#ifndef QGST_USE_QTGSTREAMER
gboolean Player::message_cb(GstBus * bus, GstMessage * message, gpointer user_data)
{
    Q_UNUSED(bus);
    ((Player*)user_data)->onBusMessage(message);
    return TRUE;
}
#endif

void Player::onBusMessage(const QGst::MessagePtr & message)
{
#ifdef QGST_USE_QTGSTREAMER
    switch (message->type()) {
    case QGst::MessageEos: //End of stream. We reached the end of the file.
        stop();
        break;
    case QGst::MessageError: //Some error occurred.
        qCritical() << message.staticCast<QGst::ErrorMessage>()->error();
        stop();
        break;
    case QGst::MessageStateChanged: //The element in message->source() has changed state
        if (message->source() == m_pipeline) {
            StateChangedMessage scm = message.staticCast<QGst::StateChangedMessage>();
            handlePipelineStateChange(scm->oldState(), scm->newState(), scm->pendingState());
        }
        break;
    default:
        break;
    }
#else
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        stop();
        break;
    case GST_MESSAGE_ERROR:
        {
            GError *err = NULL;
            gchar *dbg_info = NULL;

            gst_message_parse_error (message, &err, &dbg_info);
            qCritical() << "ERROR from element " << GST_OBJECT_NAME (message->src) << ": " << err->message;
            if(dbg_info)
                qCritical() << "Debugging info: " << dbg_info;
            g_error_free (err);
            g_free (dbg_info);
            break;
        }
        break;
    case GST_MESSAGE_STATE_CHANGED:
        //release the sink when it goes back to null state
        if (GST_MESSAGE_SRC (message) == GST_OBJECT (m_pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (message, &old_state, &new_state, &pending_state);
            handlePipelineStateChange(static_cast<QGst::State>(old_state), static_cast<QGst::State>(new_state), static_cast<QGst::State>(pending_state));
        }
    default:
        break;
    }
#endif
}

void Player::handlePipelineStateChange(const QGst::State & oldState, const QGst::State & newState, const QGst::State & pendingState)
{
    Q_UNUSED(pendingState);
    switch (newState) {
    case QGst::StatePlaying:
        //start the timer when the pipeline starts playing
        m_positionTimer.start(100);
        break;
    case QGst::StatePaused:
        //stop the timer when the pipeline pauses
        if(oldState == QGst::StatePlaying) {
            m_positionTimer.stop();
        }
        break;
    default:
        break;
    }
    Q_EMIT stateChanged();
}

void Player::setPipeline(const QGst::PipelinePtr & pipeline)
{
    if(m_pipeline == pipeline)
        return;

    if (m_pipeline)
    {
        stopPipelineWatch();
#ifdef QGST_USE_QTGSTREAMER
        QGst::BusPtr bus = m_pipeline->bus();
        // disconnect from the current bus and pipeline
        QGlib::disconnect(bus, "message", this, &Player::onBusMessage);
        bus->removeSignalWatch();
#else
        GstBus * bus = gst_pipeline_get_bus(m_pipeline);

        g_signal_handler_disconnect (G_OBJECT (bus), m_pipelineConnectId);
        gst_bus_remove_signal_watch(bus);

#endif
    }
    if(pipeline)
    {
        //let the video widget watch the pipeline for new video sinks
        watchPipeline(pipeline);
#ifdef QGST_USE_QTGSTREAMER
        //watch the bus for messages
        QGst::BusPtr bus = pipeline->bus();
        bus->addSignalWatch();
        QGlib::connect(bus, "message", this, &Player::onBusMessage);
#else
        GstBus * bus = gst_pipeline_get_bus(pipeline);
        gst_bus_add_signal_watch(bus);

        m_pipelineConnectId = g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), this);
#endif
    }
    m_pipeline = pipeline;
}

void Player::setSource(const SourceSettings & source)
{
    if(source.isRTSP)
    {
        std::stringstream ss;
        ss << "rtspsrc location=" << source.uri.toStdString();
        if(!source.username.isEmpty())
            ss << " user-id=" << source.username.toStdString();
        if(!source.password.isEmpty())
            ss << " user-pw=" << source.password.toStdString();
        if(source.bufferTime >= 0)
            ss << " latency=" << source.bufferTime;

        //ss << " onvif-mode=1";

        ss << " ! queue ! decodebin ! autovideosink";

        QGst::PipelinePtr pipeline;
#ifdef QGST_USE_QTGSTREAMER
        QGst::ElementPtr element = QGst::Parse::launch(ss.str().c_str());
        if(element)
            pipeline = element.dynamicCast<QGst::Pipeline>();
#else

        QGst::ElementPtr element = gst_parse_launch(ss.str().c_str(), NULL);
        if(element)
            pipeline = GST_PIPELINE_CAST(element);
#endif
        setPipeline(pipeline);
    }
    else
    {
        QGst::PipelinePtr pipeline;
#ifdef QGST_USE_QTGSTREAMER
        pipeline = QGst::ElementFactory::make("playbin").dynamicCast<QGst::Pipeline>();
        if(pipeline)
            pipeline->setProperty("uri", source.uri);
#else
        pipeline = GST_PIPELINE_CAST(gst_element_factory_make ("playbin", "playbin"));
        if(pipeline)
            g_object_set (pipeline, "uri", source.uri.toLocal8Bit().constData(), NULL);
#endif
        setPipeline(pipeline);
    }
}

#include "moc_player.cpp"
