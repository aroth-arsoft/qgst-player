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
#ifndef PLAYER_H
#define PLAYER_H

#include <QTimer>
#include <QTime>

#ifdef QGST_USE_QTGSTREAMER
#include <QGst/Pipeline>
#endif
#include "videowidget.h"

struct SourceSettings {
    SourceSettings()
        : uri()
          , options()
          , bufferTime(0)
          , useAudio(false)
          , pauseMode(false)
          , isLocalFile(false)
          , isRTSP(false)
          , isFTP(false)
          , isUDP(false)
          , noStreamingUriAvailable(false)
          , lowLatency(false)
    {
    }
    void setUri(const QString & s);
    void clear()
    {
        uri.clear();
        options.clear();
        username.clear();
        password.clear();
        bufferTime = 0;
        useAudio = false;
        pauseMode = false;
        isLocalFile = false;
        isRTSP = false;
        isFTP = false;
        isUDP = false;
        noStreamingUriAvailable = false;
        lowLatency = false;
    }
    QString uri;
    QString options;
    QString username;
    QString password;
    int bufferTime;
    bool useAudio;
    bool pauseMode;
    bool isLocalFile;
    bool isRTSP;
    bool isFTP;
    bool isUDP;
    bool noStreamingUriAvailable;
    bool lowLatency;
};

class Player : public QGst::Ui::VideoWidget
{
    Q_OBJECT
public:
    Player(QWidget *parent = 0);
    ~Player();

    void setUri(const QString & uri);

    QTime position() const;
    void setPosition(const QTime & pos);
    int volume() const;

    void setSource(const SourceSettings & source);

    QTime length() const;
    QGst::State state() const;

public Q_SLOTS:
    void play();
    void pause();
    void stop();
    void setVolume(int volume);

Q_SIGNALS:
    void positionChanged();
    void stateChanged();

private:
    void setPipeline(const QGst::PipelinePtr & pipeline);
#ifndef QGST_USE_QTGSTREAMER
    static gboolean message_cb(GstBus * bus, GstMessage * message, gpointer user_data);
#endif
    void onBusMessage(const QGst::MessagePtr & message);
    void handlePipelineStateChange(const QGst::State & oldState, const QGst::State & newState, const QGst::State & pendingState);

    QGst::PipelinePtr m_pipeline;
    QTimer m_positionTimer;
#ifndef QGST_USE_QTGSTREAMER
    gulong m_pipelineConnectId;
#endif
};

#endif
