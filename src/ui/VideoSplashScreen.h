#ifndef VIDEOSPLASHSCREEN_H
#define VIDEOSPLASHSCREEN_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEventLoop>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#endif

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

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_videoWidget = new QVideoWidget(this);
        layout->addWidget(m_videoWidget);

        m_player = new QMediaPlayer(this);
        m_player->setVideoOutput(m_videoWidget);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        m_audioOutput = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOutput);
        m_player->setSource(QUrl::fromLocalFile(filePath));
#else
        m_player->setMedia(QUrl::fromLocalFile(filePath));
#endif

        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &VideoSplashScreen::onMediaStatusChanged);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
            qWarning() << "Video splash playback error occurred:" << error << "-" << errorString;
            closeAndExit();
        });
#else
        connect(m_player, static_cast<void(QMediaPlayer::*)(QMediaPlayer::Error)>(&QMediaPlayer::error), this, [this](QMediaPlayer::Error error) {
            qWarning() << "Video splash playback error occurred:" << error;
            closeAndExit();
        });
#endif
    }

    void play()
    {
        m_player->play();
        setFocus();
    }

signals:
    void finished();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        closeAndExit();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        Q_UNUSED(event);
        closeAndExit();
    }

private slots:
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status)
    {
        if (status == QMediaPlayer::EndOfMedia) {
            closeAndExit();
        }
    }

private:
    void closeAndExit()
    {
        m_player->stop();
        close();
        emit finished();
    }

    QMediaPlayer *m_player = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioOutput *m_audioOutput = nullptr;
#endif
};

#endif // VIDEOSPLASHSCREEN_H
