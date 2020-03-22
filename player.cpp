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

#include <QStandardPaths>

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
{
    //this timer is used to tell the ui to change its position slider & label
    //every 100 ms, but only when the pipeline is playing
    connect(&m_positionTimer, &QTimer::timeout, this, &Player::positionChanged);
}

Player::~Player()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StateNull);
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
    pipeline = QGst::ElementFactory::make("playbin").dynamicCast<QGst::Pipeline>();
    if(pipeline)
         pipeline->setProperty("uri", realUri);
    setPipeline(pipeline);
}

QTime Player::position() const
{
    if (m_pipeline) {
        //here we query the pipeline about its position
        //and we request that the result is returned in time format
        QGst::PositionQueryPtr query = QGst::PositionQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->position()).toTime();
    } else {
        return QTime(0,0);
    }
}

void Player::setPosition(const QTime & pos)
{
    QGst::SeekEventPtr evt = QGst::SeekEvent::create(
        1.0, QGst::FormatTime, QGst::SeekFlagFlush,
        QGst::SeekTypeSet, QGst::ClockTime::fromTime(pos),
        QGst::SeekTypeNone, QGst::ClockTime::None
    );

    m_pipeline->sendEvent(evt);
}

int Player::volume() const
{
    if (m_pipeline) {
        QGst::StreamVolumePtr svp =
            m_pipeline.dynamicCast<QGst::StreamVolume>();

        if (svp) {
            return svp->volume(QGst::StreamVolumeFormatCubic) * 10;
        }
    }

    return 0;
}


void Player::setVolume(int volume)
{
    if (m_pipeline) {
        QGst::StreamVolumePtr svp =
            m_pipeline.dynamicCast<QGst::StreamVolume>();

        if(svp) {
            svp->setVolume((double)volume / 10, QGst::StreamVolumeFormatCubic);
        }
    }
}

QTime Player::length() const
{
    if (m_pipeline) {
        //here we query the pipeline about the content's duration
        //and we request that the result is returned in time format
        QGst::DurationQueryPtr query = QGst::DurationQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->duration()).toTime();
    } else {
        return QTime(0,0);
    }
}

QGst::State Player::state() const
{
    return m_pipeline ? m_pipeline->currentState() : QGst::StateNull;
}

void Player::play()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StatePlaying);
    }
}

void Player::pause()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StatePaused);
    }
}

void Player::stop()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StateNull);

        //once the pipeline stops, the bus is flushed so we will
        //not receive any StateChangedMessage about this.
        //so, to inform the ui, we have to emit this signal manually.
        Q_EMIT stateChanged();
    }
}

void Player::onBusMessage(const QGst::MessagePtr & message)
{
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
            handlePipelineStateChange(message.staticCast<QGst::StateChangedMessage>());
        }
        break;
    default:
        break;
    }
}

void Player::handlePipelineStateChange(const QGst::StateChangedMessagePtr & scm)
{
    switch (scm->newState()) {
    case QGst::StatePlaying:
        //start the timer when the pipeline starts playing
        m_positionTimer.start(100);
        break;
    case QGst::StatePaused:
        //stop the timer when the pipeline pauses
        if(scm->oldState() == QGst::StatePlaying) {
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

        QGst::BusPtr bus = m_pipeline->bus();
        // disconnect from the current bus and pipeline
        QGlib::disconnect(bus, "message", this, &Player::onBusMessage);
        bus->removeSignalWatch();
    }
    if(pipeline)
    {
        //let the video widget watch the pipeline for new video sinks
        watchPipeline(pipeline);

        //watch the bus for messages
        QGst::BusPtr bus = pipeline->bus();
        bus->addSignalWatch();
        QGlib::connect(bus, "message", this, &Player::onBusMessage);
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
        QGst::ElementPtr element = QGst::Parse::launch(ss.str().c_str());
        QGst::PipelinePtr pipeline;
        if(element)
            pipeline = element.dynamicCast<QGst::Pipeline>();

        setPipeline(pipeline);
    }
    else
    {
        QGst::PipelinePtr pipeline;
        pipeline = QGst::ElementFactory::make("playbin").dynamicCast<QGst::Pipeline>();
        if(pipeline)
            pipeline->setProperty("uri", source.uri);
        setPipeline(pipeline);
    }
}

#include "moc_player.cpp"
