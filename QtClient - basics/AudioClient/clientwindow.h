#ifndef CLIENTWINDOW_H
#define CLIENTWINDOW_H

#include <iostream>
#include <QMainWindow>

#include <QtWidgets>
#include <QtNetwork>

#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioInput>

#include <QByteArray>

namespace Ui {
class ClientWindow;
}

class ClientWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ClientWindow(QWidget *parent = 0);
    ~ClientWindow();

    void doConnect();
    void connSucceeded();
    void dataAvailable();
    void sendData();

    void loadWavFile();
    void audioStahp();
    void playFromServer();

    void createAudioOutput();
    void startLoadedAudio();
    void sendSongToServer();

    QAudioFormat getStdAudioFormat();

    void someError(QTcpSocket::SocketError);
    void delay(int millisecondsToWait);

    QTcpSocket *socket = 0;

    QAudioOutput *audioOut = 0;
    QAudioInput *audioIn = 0;

    QFile sourceFile;
    QByteArray dataFromFile;
    QByteArray *data;

    bool songLoaded = false;


private:
    Ui::ClientWindow *ui;

private slots:
    void handleStateAudioOutChanged(QAudio::State newState);
    void handleStateAudioInChanged(QAudio::State newState);

};

#endif // CLIENTWINDOW_H
