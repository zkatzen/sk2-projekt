#include "clientwindow.h"
#include "ui_clientwindow.h"

ClientWindow::ClientWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ClientWindow)
{
    ui->setupUi(this);

    connect(ui->makeConnection, &QPushButton::clicked, this,
            &ClientWindow::doConnect);
    connect(ui->messageInput, &QLineEdit::returnPressed, this, &ClientWindow::sendData);
    connect(ui->fileSend, &QPushButton::clicked, this, &ClientWindow::loadWavFile);
    connect(ui->stopButton, &QPushButton::clicked, this, &ClientWindow::audioStahp);
}

void ClientWindow::doConnect() {

    auto host = ui->destAddr->text();
    auto port = ui->portNumber->value();

    socket = new QTcpSocket(this);

    connect(socket, &QTcpSocket::connected, this, &ClientWindow::connSucceeded);
    connect(socket, &QTcpSocket::readyRead, this, &ClientWindow::dataAvailable);
    connect(socket, (void (QTcpSocket::*) (QTcpSocket::SocketError))
            &QTcpSocket::error, this, &ClientWindow::someError);

    //socket->connectToHost(host, port);
    socket->connectToHost(QHostAddress::LocalHost, port);
}

void ClientWindow::audioStahp() {
    audio->stop();
}

void ClientWindow::connSucceeded() {
    ui->destAddr->setEnabled(false);
    ui->messageInput->setEnabled(true);
}

void ClientWindow::dataAvailable() {
    auto data = socket->readAll();
    /*if (data.size() < 16)
        ui->messageBox->append("Received start or stop: " + QString::number(data.size()) + " bytes.");
    else
        ui->messageBox->append("Received lots of bytes packet.");*/
    ui->messageBox->append(QString::number(data.size()));

}

void ClientWindow::handleStateChanged(QAudio::State newState) {
    switch (newState) {
        case QAudio::IdleState:
            // Finished playing (no more data)
            audio->stop();
            sourceFile.close();
            delete audio;
            break;

        case QAudio::StoppedState:
            // Stopped for other reasons
            if (audio->error() != QAudio::NoError) {
                // Error handling
            }
            break;

        default:
            // ... other cases as appropriate
            break;
    }
}

void ClientWindow::loadWavFile() {

    auto fileName = ui->fileNameInput->text();

    sourceFile.setFileName(fileName);
    if (!sourceFile.exists()) {
        QMessageBox::information(this, "Well...", "File doesnt exist, sorry :<");
        return;
    }
    else {
        QMessageBox::information(this, "Well...", "File open! <3");
    }
    sourceFile.open(QIODevice::ReadOnly);

    QAudioFormat format;
    // Set up the format, eg.
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::UnSignedInt);

    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
    if (!info.isFormatSupported(format)) {
       QMessageBox::information(this, "Well...", "zUe formaty.");
       return;
    }

   QMessageBox::information(this, "Well...", QString::number(sourceFile.bytesAvailable()));

   audio = new QAudioOutput(format, this);
   connect(audio, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));
   audio->start(&sourceFile);

   QMessageBox::information(this, "Well...", QString::number(sourceFile.bytesAvailable()));

   //audio->start();

   QByteArray buffer(32768, 0);
   int chunks = audio->bytesFree() / audio->periodSize();
   while (chunks) {
      const qint64 len = sourceFile.read(buffer.data(), audio->periodSize());
      if (len) {
          socket->write(buffer.data());
          ui->messageBox->append("<i>" + QString::fromUtf16((ushort *) buffer.data()) + "</i>");
      }
      if (len != audio->periodSize())
          break;
      --chunks;
   }

}

void ClientWindow::sendData() {
    QString str = ui->messageInput->text();
    str += '\n';
    auto data = str.toUtf8();
    socket->write(data);
    ui->messageBox->append("<i>" + str + "</i>");
    ui->messageInput->clear();
}

void ClientWindow::someError(QTcpSocket::SocketError) {
    QMessageBox::critical(this, "Error", socket->errorString());
}

ClientWindow::~ClientWindow()
{
    delete ui;
}
