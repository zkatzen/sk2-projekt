#ifndef CLIENTWINDOW_H
#define CLIENTWINDOW_H

#include <QMainWindow>

#include <QtWidgets>
#include <QtNetwork>

#include <QAudioOutput>
#include <QAudioFormat>

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

    void someError(QTcpSocket::SocketError);

    QTcpSocket *socket = 0;

    QAudioOutput *audio = 0;
    QFile sourceFile;


private:
    Ui::ClientWindow *ui;

private slots:
    void handleStateChanged(QAudio::State newState);

};

#endif // CLIENTWINDOW_H
