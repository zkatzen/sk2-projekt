#ifndef CLIENTWINDOW_H
#define CLIENTWINDOW_H

#include <iostream>
#include <QMainWindow>

#include <QtWidgets>
#include <QtNetwork>
#include <QFileDialog>

#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioInput>

#include <QByteArray>

#include <QCloseEvent>
#include <thread>

#include <QMediaPlayer>

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
    void connSucceeded();
    void dataAvailable();
    void sendData();

    void selectWavFile();
    void loadWavFile();
    void audioStahp();
    void playFromServer();

    void createAudioOutput();
    void startLoadedAudio();
    void sendSongToServer();

    void pushMeButtonClicked();
    void startPlaylist();

    QAudioFormat getStdAudioFormat();

    void someError(QTcpSocket::SocketError);

    void updatePlaylist(QByteArray playlistData);

    QTcpSocket *socket = nullptr;

    QAudioOutput *audioOut = 0;
    QAudioInput *audioIn = 0;

    QMediaPlayer *qmp = 0;

    QFile sourceFile;
    QString loadedFileName;

    QByteArray dataFromFile;
    QByteArray *songData;

    bool songLoaded = false;
    bool connectedToServer = false;
    bool songLoading = false;


private:
    Ui::ClientWindow *ui;

private slots:
    void handleStateAudioOutChanged(QAudio::State newState);
    void handleStateAudioInChanged(QAudio::State newState);
    void positionChanged(qint64 progress);

};

#endif // CLIENTWINDOW_H
