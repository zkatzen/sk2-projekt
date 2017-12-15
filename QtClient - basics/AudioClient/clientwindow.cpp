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
    connect(ui->startButton, &QPushButton::clicked, this, &ClientWindow::startLoadedAudio);
    connect(ui->stopButton, &QPushButton::clicked, this, &ClientWindow::audioStahp);

    connect(ui->serverPlayButton, &QPushButton::clicked, this, &ClientWindow::playFromServer);
    connect(ui->sendButton, &QPushButton::clicked, this, &ClientWindow::sendSongToServer);

}

void ClientWindow::sendSongToServer() {
    QAudioFormat format = this->getStdAudioFormat();
    audioIn = new QAudioInput(format, this);
    audioIn->setBufferSize(4096);
    audioIn->start(socket);
    connect(audioIn, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateAudioInChanged(QAudio::State)));
}

void ClientWindow::startLoadedAudio() {
    if (audioOut) {
        sourceFile.open(QIODevice::ReadOnly);
        audioOut->start(&sourceFile);
    }
}

void ClientWindow::playFromServer() {
    if (songLoaded) {
        this->createAudioOutput();
        QBuffer *buffer = new QBuffer(data);
        buffer->open(QIODevice::ReadOnly);
        audioOut->start(buffer);
    }
    else {
        return;
    }
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
    socket->connectToHost(host, port);
}

void ClientWindow::audioStahp() {
    audioOut->stop();
}

void ClientWindow::connSucceeded() {
    ui->destAddr->setEnabled(false);
    ui->messageInput->setEnabled(true);
    data = new QByteArray();
}

void ClientWindow::dataAvailable() {
    ui->messageBox->append("\nreading all:\n");

    auto dataRec = socket->readAll();
    if (dataRec.contains("start")) {
        int startPos = dataRec.indexOf("start");
        data->append(dataRec.mid(startPos));
        ui->messageBox->append("there was start in the packet!");
    }
    else if (dataRec.contains("stop")) {
        int stopPos = dataRec.indexOf("stop");
        dataRec.truncate(stopPos);
        data->append(dataRec);
        songLoaded = true;
        ui->messageBox->append("there was stop in the packet!");
    }
    else {
        data->append(dataRec);
    }
    ui->messageBox->append(QString::number(dataRec.size()));

}

void ClientWindow::handleStateAudioInChanged(QAudio::State newState) {
    switch (newState) {
        case QAudio::IdleState:
            // Finished playing (no more data)
            audioIn->stop();
            delete audioIn;
            break;

        case QAudio::StoppedState:
            // Stopped for other reasons
            if (audioIn->error() != QAudio::NoError) {
                // Error handling
            }
            break;

        default:
            // ... other cases as appropriate
            break;
    }
}

void ClientWindow::handleStateAudioOutChanged(QAudio::State newState) {
    switch (newState) {
        case QAudio::IdleState:
            // Finished playing (no more data)
            audioOut->stop();
            sourceFile.close();
            delete audioOut;
            break;

        case QAudio::StoppedState:
            // Stopped for other reasons
            if (audioOut->error() != QAudio::NoError) {
                // Error handling
            }
            break;

        default:
            // ... other cases as appropriate
            break;
    }
}

QAudioFormat ClientWindow::getStdAudioFormat() {
    QAudioFormat format;
    // Set up the format, eg.
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::UnSignedInt);

    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
    if (!info.isFormatSupported(format))
       QMessageBox::information(this, "Well...", "zUe formaty.");
        // wtedy przypał! nie zwracać formatu!

    return format;
}

void ClientWindow::createAudioOutput() {

   QAudioFormat format = this->getStdAudioFormat();
   audioOut = new QAudioOutput(format, this);
   connect(audioOut, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateAudioOutChanged(QAudio::State)));

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

    this->createAudioOutput();
    //audioOut->start(&sourceFile);
    QMessageBox::information(this, "Well...", "Loaded file size in bytes is " + QString::number(sourceFile.bytesAvailable()));

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
