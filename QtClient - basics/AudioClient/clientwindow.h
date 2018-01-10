#ifndef CLIENTWINDOW_H
#define CLIENTWINDOW_H

#include <iostream>
#include <QMainWindow>

#include <QtWidgets>
#include <QtNetwork>
#include <QFileDialog>

#include <QAudioOutput>
#include <QAudioFormat>

#include <QByteArray>

#include <QCloseEvent>
#include <thread>

#include <QMediaPlayer>
#include <QAudioBuffer>

namespace Ui {
class ClientWindow;
}

class ClientWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ClientWindow(QWidget *parent = 0);
    void closeEvent(QCloseEvent *event);
    ~ClientWindow();

    void doConnect();
    void doConnectMsg();
    void connSucceeded();
    void msgAvailable();
    void dataAvailable();
    void sendData();

    void selectWavFile();
    void loadWavFile();
    void audioStahp();
    void audioStart();
    void playFromServer();

    void createAudioOutput();
    void startLoadedAudio();
    void sendSongToServer();

    void pushMeButtonClicked();
    void startPlaylistRequest();
    void stopPlaylistRequest();
    void nextSongRequest();

    QAudioFormat getStdAudioFormat();

    void someError(QTcpSocket::SocketError);

    void updatePlaylist(QByteArray playlistData);

    QTcpSocket *socket = nullptr;
    QTcpSocket *socketForMsg = nullptr;

    QAudioOutput *audioOut = nullptr;
    QBuffer *audioBuffer = nullptr;

    QFile sourceFile;
    QString loadedFileName;

    QByteArray dataFromFile;
    QByteArray *songData;

    bool songLoaded = false;
    bool connectedToServer = false;
    bool songLoading = false;

    bool playlistOn = false;

    const QByteArray *songStartMsg = new QByteArray("^START_SONG^\n");
    const QByteArray *songStopMsg = new QByteArray("^STOOP_SONG^\n");
    const QByteArray *playlistStartMsg = new QByteArray("^START_LIST^\n");
    const QByteArray *playlistStopMsg = new QByteArray("^STOOP_LIST^\n");
    const QByteArray *nextSongReq = new QByteArray("^NEXT_SOONG^\n");

    const QByteArray *plPos = new QByteArray("POS");
    const int minSongBytes = 44000;


private:
    Ui::ClientWindow *ui;

private slots:
    void handleStateAudioOutChanged(QAudio::State newState);
    void positionChanged(qint64 progress);

};

#endif // CLIENTWINDOW_H
