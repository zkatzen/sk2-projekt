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

#include <QAtomicInt>

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
    void nextSongPlease();

    void upButtonClicked();
    void downButtonClicked();
    void deleteButtonClicked();

    QAudioFormat getStdAudioFormat();

    void sockError(QTcpSocket::SocketError);
    void msgSockError(QTcpSocket::SocketError);

    void updatePlaylist(QByteArray playlistData);
    void serverConnMode();

    QTcpSocket *socket = nullptr;
    QTcpSocket *socketForMsg = nullptr;

    QAudioOutput *audioOut = nullptr;
    QBuffer *audioBuffer = nullptr;

    QFile sourceFile;
    QString loadedFileName;

    QByteArray dataFromFile;
    QByteArray *songData;

    int songDataSize;

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
    const int minSongBytes = 44100 * 2;

    int currPlPosition;


private:
    Ui::ClientWindow *ui;

private slots:
    void handleStateAudioOutChanged(QAudio::State newState);

};

#endif // CLIENTWINDOW_H
