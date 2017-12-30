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

    connect(ui->fileSelect, &QPushButton::clicked, this, &ClientWindow::selectWavFile);
    connect(ui->fileSend, &QPushButton::clicked, this, &ClientWindow::loadWavFile);
    connect(ui->startButton, &QPushButton::clicked, this, &ClientWindow::startLoadedAudio);
    connect(ui->stopButton, &QPushButton::clicked, this, &ClientWindow::audioStahp);

    connect(ui->serverPlayButton, &QPushButton::clicked, this, &ClientWindow::playFromServer);
    connect(ui->sendButton, &QPushButton::clicked, this, &ClientWindow::sendSongToServer);

}

void ClientWindow::sendSongToServer() {

    /*QAudioFormat format = this->getStdAudioFormat();
    audioIn = new QAudioInput(format, this);
    audioIn->setBufferSize(4096);
    connect(audioIn, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateAudioInChanged(QAudio::State)));
    audioIn->start(socket);*/

    // wyslac po prostu plik binarnie? skoro serwer odesle go w takiej formie
    socket->write("fn:"); //znaczniki ktore wiadomosci dotycza czego, tak jak start+stop
    socket->write(sourceFile.fileName().toUtf8());
    // wysylanie rozmiaru pliku, w odpowiednim formacie
    //socket->write(QByteArray::number(sourceFile.size()),10);
    socket->write(dataFromFile);
    ui->messageBox->append("\nsent to server (.. at least tried)!");
}

void ClientWindow::startLoadedAudio() {

    if (audioOut) {
        if (dataFromFile.size() != 0) {
            // plik wczytany do dataFromFile
            QBuffer *buffer = new QBuffer(&dataFromFile);
            buffer->open(QIODevice::ReadOnly);
            audioOut->start(buffer);
        }
    }
}

void ClientWindow::playFromServer() {
    if (songLoaded) {
        this->createAudioOutput();
        QBuffer *buffer = new QBuffer(data);
        buffer->open(QIODevice::ReadOnly);
        audioOut->stop();
        audioOut->setBufferSize(buffer->size());
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
        dataRec = dataRec.mid(startPos + sizeof("start"));
        ui->messageBox->append("there was start in the packet!");
    }
    else if (dataRec.contains("stop")) {
        int stopPos = dataRec.indexOf("stop");
        if (stopPos > 0) {
            dataRec.truncate(stopPos);
        }
        songLoaded = true;
        ui->messageBox->append("there was stop in the packet!");
    }
    data->append(dataRec);
    ui->messageBox->append(QString::number(dataRec.size()));

}

void ClientWindow::handleStateAudioInChanged(QAudio::State newState) {
    switch (newState) {
        case QAudio::IdleState:
            audioIn->stop();
            ui->messageBox->append("finished sending to server!");
            delete audioIn; // ?
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
    QMessageBox::information(this, "Well...", "State changed! to " + QString::number(newState));

    switch (newState) {
        case QAudio::IdleState:
            audioOut->stop();
            delete audioOut; //?
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


void ClientWindow::selectWavFile() {
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open File"),
                                                    "/home",
                                                    tr("Music (*.wav)"));
    ui->fileNameInput -> setText(fileName);
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

    sourceFile.open(QIODevice::ReadOnly);
    dataFromFile = sourceFile.readAll();
    QMessageBox::information(this, "Well...", "Loaded file size in bytes is " + QString::number(dataFromFile.size()));

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
