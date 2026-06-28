#ifndef VIDEOSPLASHSCREEN_H
#define VIDEOSPLASHSCREEN_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEventLoop>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#include <QMediaDevices>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPainter>
#include <QImage>
#else
#include <QVideoWidget>
#endif

// Splash screen that plays a short intro video before the main window opens.
//
// On Qt 6 we deliberately render via QVideoSink + QPainter instead of
// QVideoWidget. QVideoWidget relies on a native video surface that, on
// Wayland (e.g. labwc on Raspberry Pi) with the FFmpeg backend, presents only
// the first frame and then never recomposites — the audio and decoder keep
// running while the picture stays frozen. Pulling each QVideoFrame from the
// sink and drawing it ourselves in paintEvent() uses the ordinary widget
// painting path, which works reliably on both Wayland and Windows.
//
// Playback ending (or user skip) emits finished() but keeps the last frame
// visible until dismiss() — typically after MainWindow::show().
class VideoSplashScreen : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSplashScreen(const QString &filePath, QWidget *parent = nullptr)
        : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        // Set fixed size of 1280x750 (matching original Splash.png aspect ratio/size)
        setFixedSize(1280, 750);
        setFocusPolicy(Qt::StrongFocus);

        m_player = new QMediaPlayer(this);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        // Paint the frames ourselves; the whole widget is repainted each frame.
        setAttribute(Qt::WA_OpaquePaintEvent);

        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) {
                    const QImage img = frame.toImage();
                    if (!img.isNull()) {
                        m_currentImage = img;
                        update();
                    }
                });

        m_audioOutput = new QAudioOutput(preferredVideoAudioOutput(), this);
        m_player->setAudioOutput(m_audioOutput);
        m_player->setSource(QUrl::fromLocalFile(filePath));
#else
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        m_videoWidget = new QVideoWidget(this);
        layout->addWidget(m_videoWidget);
        m_player->setVideoOutput(m_videoWidget);
        m_player->setMedia(QUrl::fromLocalFile(filePath));
#endif

        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &VideoSplashScreen::onMediaStatusChanged);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
            qWarning() << "Video splash playback error occurred:" << error << "-" << errorString;
            finishPlayback();
        });
#else
        connect(m_player, static_cast<void(QMediaPlayer::*)(QMediaPlayer::Error)>(&QMediaPlayer::error), this, [this](QMediaPlayer::Error error) {
            qWarning() << "Video splash playback error occurred:" << error;
            finishPlayback();
        });
#endif
    }

    void play()
    {
        m_player->play();
        setFocus();
    }

    void dismiss()
    {
        if (m_dismissed)
            return;
        m_dismissed = true;
        close();
    }

signals:
    void finished();

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (!m_currentImage.isNull()) {
            const QImage scaled = m_currentImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const int x = (width() - scaled.width()) / 2;
            const int y = (height() - scaled.height()) / 2;
            painter.drawImage(x, y, scaled);
        }
    }
#endif

    void mousePressEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        finishPlayback();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        Q_UNUSED(event);
        finishPlayback();
    }

private slots:
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status)
    {
        if (status == QMediaPlayer::EndOfMedia) {
            finishPlayback();
        }
    }

private:
    // Prefer HDMI output for video audio so that connecting a USB mic does not
    // silently redirect sound to the USB adapter's unplugged headphone jack.
    static QAudioDevice preferredVideoAudioOutput()
    {
        QAudioDevice hdmi, nonUsb;
        for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
            const QString desc = dev.description();
            if (desc.contains(QLatin1String("HDMI"), Qt::CaseInsensitive) && hdmi.isNull())
                hdmi = dev;
            if (!desc.contains(QLatin1String("USB"), Qt::CaseInsensitive) && nonUsb.isNull())
                nonUsb = dev;
        }
        if (!hdmi.isNull())  return hdmi;
        if (!nonUsb.isNull()) return nonUsb;
        return QMediaDevices::defaultAudioOutput();
    }

    void finishPlayback()
    {
        if (m_playbackFinished)
            return;
        m_playbackFinished = true;
        m_player->stop();
        emit finished();
    }

    QMediaPlayer *m_player = nullptr;
    bool m_playbackFinished = false;
    bool m_dismissed = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVideoSink *m_sink = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QImage m_currentImage;
#else
    QVideoWidget *m_videoWidget = nullptr;
#endif
};

#endif // VIDEOSPLASHSCREEN_H
