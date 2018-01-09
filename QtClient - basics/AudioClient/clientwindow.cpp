#include "clientwindow.h"
#include "ui_clientwindow.h"

ClientWindow::ClientWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ClientWindow)
{
    ui->setupUi(this);

    connect(ui->makeConnection, &QPushButton::clicked, this,
            &ClientWindow::doConnect);

    connect(ui->fileSelect, &QPushButton::clicked, this, &ClientWindow::selectWavFile);
    connect(ui->fileSend, &QPushButton::clicked, this, &ClientWindow::loadWavFile);
    connect(ui->startButton, &QPushButton::clicked, this, &ClientWindow::startLoadedAudio);
    connect(ui->stopButton, &QPushButton::clicked, this, &ClientWindow::audioStahp);

    connect(ui->serverPlayButton, &QPushButton::clicked, this, &ClientWindow::playFromServer);
    connect(ui->sendButton, &QPushButton::clicked, this, &ClientWindow::sendSongToServer);

    connect(ui->pushMeButton, &QPushButton::clicked, this, &ClientWindow::pushMeButtonClicked);
    connect(ui->firePlaylistButton, &QPushButton::clicked, this, &ClientWindow::startPlaylistRequest);
    connect(ui->stopPlaylistButton, &QPushButton::clicked, this, &ClientWindow::stopPlaylistRequest);

    qmp = new QMediaPlayer(this);
    qmp->setAudioRole(QAudio::MusicRole);

    // TYLKO Q AUDIO OUTPUT! <3
    this->createAudioOutput();

}

void ClientWindow::startPlaylistRequest() {
    QTableWidgetItem* item = ui->playlistWidget->item(0,0);
    if (item && !item->text().isEmpty()) {
        // there's somethin' on a playlist
        socketForMsg->write(*playlistStartMsg);
    }
}

void ClientWindow::stopPlaylistRequest() {
    //QAudio::State s = audioOut->state();
    //if (s == QAudio::ActiveState || s == QAudio::SuspendedState || s == QAudio::IdleState) {
        socketForMsg->write(*playlistStopMsg);
    //}
}

void ClientWindow::pushMeButtonClicked() {
    if (qmp->state() == QMediaPlayer::PlayingState) {
        auto pos = qmp->position();
        ui->messageBox->append("qmp->songPosition() = " + QString::number(pos));
    }
}

void ClientWindow::closeEvent(QCloseEvent *event) {
    // stuff
    if (connectedToServer) {
        socketForMsg->write("^GOOD_BYEEE^");
        socketForMsg->disconnectFromHost();
        socket->disconnectFromHost();
    }
    QMessageBox::information(this, "(cries)", "Nevermind, I'll find someone like you (uuu)");
    QWidget::closeEvent(event);
}

void ClientWindow::sendSongToServer() {

    if (dataFromFile.size() == 0) {
        QMessageBox::information(this, "Error", "You didn't load a song.");
        return;
    }

    if (socket == nullptr || connectedToServer == false) {
        QMessageBox::information(this, "Error", "You should really connect to server first.");
        return;
    }
    else if (connectedToServer) {

        socket->write("fn:"); //znaczniki ktore wiadomosci dotycza czego, tak jak start+stop
        socket->write(loadedFileName.toUtf8()); //bez informacji o lokalizacji pliku (bez path)
        // wysylanie rozmiaru pliku, w odpowiednim formacie
        socket->write(QByteArray::number(sourceFile.size(), 10));
        socket->write(dataFromFile);
        ui->messageBox->append("\nSent " + loadedFileName + " file to server (or attempted to ;)), file size was " +
                               QString::number(sourceFile.size()));

    }
}

void ClientWindow::startLoadedAudio() {

    if (qmp) {
        if (dataFromFile.size() != 0) {
            // plik wczytany do dataFromFile
            QBuffer *buffer = new QBuffer(qmp);
            buffer->setData(dataFromFile);
            buffer->open(QIODevice::ReadOnly);

            qmp->setMedia(QMediaContent(), buffer);
            qmp->play();

        }
    }
    else {
        ui->messageBox->append("No data found to be played. :c");
    }
}

void ClientWindow::positionChanged(qint64 progress) {
    ;
}

void ClientWindow::playFromServer() {

    if (playlistOn && (songLoading || songLoaded)) {
        if (songData->size() > 44) { // no tyle, to ma naglowek, iks de

            if (audioOut->state() == QAudio::StoppedState) {
                QBuffer *buffer = new QBuffer();
                buffer->setBuffer(songData);
                buffer->open(QIODevice::ReadWrite);

                audioOut = new QAudioOutput(ClientWindow::getStdAudioFormat(), this);
                audioOut->start(buffer);
                ui->messageBox->append("AudioOut started...");

            }
            if (audioOut->state() == QAudio::SuspendedState)
                audioOut->resume();
        }
    }
}

void ClientWindow::doConnect() {

    auto host = ui->destAddr->text();
    auto port = ui->portNumber->value();

    if (socket == nullptr) {
        socket = new QTcpSocket(this);

        connect(socket, &QTcpSocket::connected, this, &ClientWindow::doConnectMsg);
        connect(socket, &QTcpSocket::readyRead, this, &ClientWindow::dataAvailable);
        connect(socket, (void (QTcpSocket::*) (QTcpSocket::SocketError))
                &QTcpSocket::error, this, &ClientWindow::someError);

        socket->connectToHost(host, port);

    }
}

void ClientWindow::doConnectMsg() {
    int secretPort = 54321;
    ui->messageBox->append("First connection succesfull");
    if (socketForMsg == nullptr && socket != nullptr) {

        socketForMsg = new QTcpSocket(this);

        connect(socketForMsg, &QTcpSocket::connected, this, &ClientWindow::connSucceeded);
        connect(socketForMsg, &QTcpSocket::readyRead, this, &ClientWindow::msgAvailable);
        connect(socketForMsg, (void (QTcpSocket::*) (QTcpSocket::SocketError))
                &QTcpSocket::error, this, &ClientWindow::someError);

        socketForMsg->connectToHost(QHostAddress::LocalHost, secretPort);
    }

}

void ClientWindow::audioStahp() {

    if (audioOut->state() == QAudio::ActiveState)
        audioOut->suspend();
    if (audioOut->state() == QAudio::IdleState)
        audioOut->stop();

}

void ClientWindow::connSucceeded() {
    ui->messageBox->append("Second conn succefull!");
    ui->destAddr->setEnabled(false);
    songData = new QByteArray();

    this->connectedToServer = true;
    ui->sendButton->setEnabled(true);
    ui->serverPlayButton->setEnabled(true);
}

void ClientWindow::dataAvailable() {

    auto dataRec = socket->readAll();

    // PLAYLIST

    if (songLoading) {
        songData->append(dataRec);
        if (songData->size() > 44)
            this->playFromServer();
    }

}

void ClientWindow::msgAvailable() {
    auto msgRec = socketForMsg->readAll();
    if (msgRec.contains("<playlist>")) {
        this->updatePlaylist(msgRec);
        //return; // ?
    }
    if (msgRec.contains(*playlistStartMsg)) {
        playlistOn = true;
        //return; // ?
    }
    if (msgRec.contains(*playlistStopMsg)) {
        this->audioStahp();
        playlistOn = false;
        //return; // ?
    }
    if (msgRec.contains(*songStartMsg)) {
        // songData->clear();
        songLoading = true;
        ui->messageBox->append("Song -> there was 'Song Start'' in the packet!");
    }
    else if (msgRec.contains(*songStopMsg)) {
        songLoaded = true;
        songLoading = false;
        ui->messageBox->append("Song -> there was 'Song Stop'' in the packet!");
        ui->messageBox->append("Server song size is : " + QString::number(songData->size()));
    }

}

void ClientWindow::updatePlaylist(QByteArray playlistData) {
    if (playlistData.contains("<playlist>") && playlistData.contains("<end_playlist>")) {

        int plLen = playlistData.indexOf("<end_playlist>") - playlistData.indexOf("<playlist>") - sizeof("<playlist>");
        auto playlist = playlistData.mid(playlistData.indexOf("<playlist>") + sizeof("<playlist>") - 1, plLen);
        QStringList songs = QString::fromUtf8(playlist).split('<', QString::SkipEmptyParts);
        ui->playlistWidget->clear();
        int rowCounter = 0;
        for (QString s : songs) {
            QStringList slices = s.split(':');
            for (int i = 1; i < slices.length(); i++)
                ui->playlistWidget->setItem(rowCounter, i-1, new QTableWidgetItem(slices[i]));
            rowCounter++;
        }

    }
    else {
        QMessageBox::information(this, "Error", "I got incomplete playlist data packet :|");
    }
}

void ClientWindow::handleStateAudioOutChanged(QAudio::State newState) {

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
                                                    tr("Music (*.wav *.mp3)"));
    ui->fileNameInput->setText(fileName);
}

void ClientWindow::loadWavFile() {

    auto fileNameWithPath = ui->fileNameInput->text();
    sourceFile.setFileName(fileNameWithPath);
    if (!sourceFile.exists()) {
        QMessageBox::information(this, "Well...", "File doesnt exist, sorry :<");
        return;
    }
    else {
        QMessageBox::information(this, "Good news", "File open! <3");
    }


    loadedFileName = fileNameWithPath.mid(fileNameWithPath.lastIndexOf('/')+1);

    sourceFile.open(QIODevice::ReadOnly);
    dataFromFile = sourceFile.readAll();
    sourceFile.close();
    QMessageBox::information(this, "What happened:", "Loaded file size in bytes is " + QString::number(dataFromFile.size())
                             + ", file name is " + loadedFileName);

}

void ClientWindow::sendData() {
    /*
     * może się jeszcze przyda do czegoś
     *
    QString str = ui->messageInput->text();
    str += '\n';
    auto data = str.toUtf8();
    socket->write(data);
    ui->messageBox->append("<i>" + str + "</i>");
    ui->messageInput->clear();
    */
}

void ClientWindow::someError(QTcpSocket::SocketError) {
    QMessageBox::critical(this, "Error", socket->errorString());
    if (socket->error() == QTcpSocket::RemoteHostClosedError) {
        ui->destAddr->setEnabled(true);
        this->connectedToServer = false;
        ui->sendButton->setEnabled(false);
        ui->serverPlayButton->setEnabled(false);
        ui->messageBox->append("\n!!! server has disconnected! \n!!! consider re-connecting ;)\n");
    }
}

ClientWindow::~ClientWindow()
{
    delete ui;
}
