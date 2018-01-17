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
    connect(ui->nextSongButton, &QPushButton::clicked, this, &ClientWindow::nextSongPlease);

    connect(ui->upButton, &QPushButton::clicked, this, &ClientWindow::upButtonClicked);
    connect(ui->downButton, &QPushButton::clicked, this, &ClientWindow::downButtonClicked);

    socket = new QTcpSocket(this);
    songData = new QByteArray();

    // TYLKO Q AUDIO OUTPUT! <3
    this->createAudioOutput();

    audioBuffer = new QBuffer();
    audioBuffer->setBuffer(songData);
    audioBuffer->open(QIODevice::ReadWrite);

    songDataSize = 0;
    currPlPosition = 0;

}

void ClientWindow::upButtonClicked() {
    int itemRow = ui->playlistWidget->currentRow();
    if(itemRow > 0) {
        QByteArray *upSong = new QByteArray("SONG_UP_");
        upSong->append(QString::number(itemRow));
        upSong->append("\n");
        socketForMsg->write(upSong->data());
        ui->playlistWidget->selectRow(itemRow-1);
    }
}

void ClientWindow::downButtonClicked() {
    int itemRow = ui->playlistWidget->currentRow();
    QTableWidgetItem *nextItem = ui->playlistWidget->item(itemRow+1, 0);
    if(nextItem && !nextItem->text().isEmpty()) {
        QByteArray *downSong = new QByteArray("SONG_DO_");
        downSong->append(QString::number(itemRow));
        downSong->append("\n");
        socketForMsg->write(downSong->data());
        ui->playlistWidget->selectRow(itemRow+1);
    }
}

void ClientWindow::startPlaylistRequest() {
    QTableWidgetItem* item = ui->playlistWidget->item(0,0);
    if (item && !item->text().isEmpty()) {
        ui->messageBox->append("Requesting START PLAYLIST");
        socketForMsg->write(*playlistStartMsg);
    }
}

void ClientWindow::nextSongRequest() {
    QTableWidgetItem* item = ui->playlistWidget->item(0,0);
    if (item && !item->text().isEmpty() && playlistOn) {
        ui->messageBox->append("Requesting NEXT SONG");
        socketForMsg->write(*nextSongReq);
    }
}

void ClientWindow::nextSongPlease() {
    if (audioOut->state() == QAudio::ActiveState || audioOut->state() == QAudio::SuspendedState
            || audioOut->state() == QAudio::StoppedState) //?
        this->nextSongRequest();
}

void ClientWindow::stopPlaylistRequest() {
    QAudio::State s = audioOut->state();
    if (playlistOn) {
        socketForMsg->write(*playlistStopMsg);
    }
}

void ClientWindow::pushMeButtonClicked() {
    this->audioStart();
}

void ClientWindow::closeEvent(QCloseEvent *event) {
    // stuff
    if (connectedToServer) {
        socketForMsg->write("^GOOD_BYEEE^\n");
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

    if (audioOut) {
        if (dataFromFile.size() != 0) {
            // plik wczytany do dataFromFile
            /*QBuffer *buffer = new QBuffer(qmp);
            buffer->setData(dataFromFile);
            buffer->open(QIODevice::ReadOnly);

            qmp->setMedia(QMediaContent(), buffer);
            qmp->play();*/

        }
    }
    else {
        ui->messageBox->append("No data found to be played. :c");
    }
}

void ClientWindow::playFromServer() {

    if (playlistOn) {

        //if (audioOut->state() != QAudio::ActiveState && audioOut->state() != QAudio::IdleState) {
            //if (songDataSize > minSongBytes)
                this->audioStart();
        //}
    }

}


void ClientWindow::audioStart() {

    if (audioOut && songDataSize > minSongBytes) {

        if (audioOut->state() == QAudio::StoppedState) {
            // if (songDataSize > minSongBytes) {
                ui->messageBox->append("I've got " + QString::number(songDataSize) + " bytes in a buffer to play :)");

                audioBuffer->close();
                //songData->clear();
                audioBuffer->setBuffer(songData);

                if (!audioBuffer->isOpen())
                    audioBuffer->open(QIODevice::ReadWrite);

                audioOut->start(audioBuffer);
            //}
        }

        else if (audioOut->state() == QAudio::SuspendedState)
            audioOut->resume();

    }
}

void ClientWindow::doConnect() {

    auto host = ui->destAddr->text();
    auto port = ui->portNumber->value();

    if (socket) {

        connect(socket, &QTcpSocket::connected, this, &ClientWindow::doConnectMsg);
        connect(socket, &QTcpSocket::readyRead, this, &ClientWindow::dataAvailable);
        connect(socket, (void (QTcpSocket::*) (QTcpSocket::SocketError))
                &QTcpSocket::error, this, &ClientWindow::someError);

        socket->connectToHost(host, port);

    }
}

void ClientWindow::doConnectMsg() {
    int secretPort = 54321;
    ui->messageBox->append("First connection succesful");

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
    ui->messageBox->append("Second conn succesful!");
    ui->destAddr->setEnabled(false);

    this->connectedToServer = true;
    ui->sendButton->setEnabled(true);
    ui->serverPlayButton->setEnabled(true);
}

void ClientWindow::dataAvailable() {

    auto dataRec = socket->readAll();

    // PLAYLIST
    if (songLoading) {
        songData->append(dataRec);
        songDataSize += dataRec.size();
        // if (songData->size() > minSongBytes)
        this->playFromServer();
    }


}

void ClientWindow::msgAvailable() {
    auto msgRec = socketForMsg->readAll();
    //ui->messageBox->append(msgRec);

    if (msgRec.contains("<playlist>")) {
        this->updatePlaylist(msgRec);
        //return; // ?
    }
    if (msgRec.contains(*playlistStartMsg)) {
        playlistOn = true;
        this->playFromServer();
        //return; // ?
    }
    if (msgRec.contains(*playlistStopMsg)) {
        this->audioStahp();
        playlistOn = false;
        //return; // ?
    }
    if (msgRec.contains(*plPos)) {

        /*int currR = ui->playlistWidget->currentRow();
        if (currR > 0)*/

        int posIdx = msgRec.indexOf("POS");
        QByteArray posData = msgRec.mid(posIdx + 3);
        posData.truncate(posData.indexOf("\n"));
        int plPosition = posData.toInt();
        ui->playlistWidget->selectRow(plPosition - 1);

        // clear previous
        for (int i = 0; i < 2; i ++)
            ui->playlistWidget->item(currPlPosition, i)->setBackgroundColor(Qt::white);
        // set new
        currPlPosition = plPosition-1;
        // paint pink <3
        for (int i = 0; i < 2; i ++)
            ui->playlistWidget->item(currPlPosition, i)->setBackgroundColor(Qt::magenta);

    }
    if (msgRec.contains(*songStartMsg)) {
        songData->clear();
        songDataSize = 0;
        songLoading = true;
        songLoaded = false;
        ui->messageBox->append("Song -> there was 'Song Start'' in the packet!");
    }
    else if (msgRec.contains(*songStopMsg)) {
        ui->messageBox->append("Song -> there was 'Song Stop'' in the packet!");

        songLoaded = true;
        songLoading = false;

        ui->messageBox->append("Server song size is : " + QString::number(songDataSize));
        //songData->append("\0");
        //this->playFromServer();

    }

}

void ClientWindow::updatePlaylist(QByteArray playlistData) {

    if (playlistData.contains("<playlist>") && playlistData.contains("<end_playlist>")) {

        int currR = ui->playlistWidget->currentRow();

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

        if (currR >= 0)
            ui->playlistWidget->selectRow(currR);
    }
    else {
        QMessageBox::information(this, "Error", "I got incomplete playlist data packet :|");
    }
}

void ClientWindow::handleStateAudioOutChanged(QAudio::State newState) {

    int err = audioOut->error();
    ui->messageBox->append("!!! AudioOut newState: " + QString::number(audioOut->state()) +
                           ", errors: " + QString::number(err));

    switch (newState) {
        case QAudio::IdleState:
            //audioOut->stop();
            //if (err == QAudio::NoError) {
            if (songLoaded)
                this->nextSongRequest();
            audioOut->stop();
            //}
            break;

        case QAudio::StoppedState:
            //audioOut->stop();
            // Stopped for other reasons
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
   // ??? audioOut->setNotifyInterval(200);

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
    ;
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
