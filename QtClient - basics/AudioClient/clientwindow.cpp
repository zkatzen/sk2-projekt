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
    connect(ui->firePlaylistButton, &QPushButton::clicked, this, &ClientWindow::startPlaylist);

    qmp = new QMediaPlayer(this);
    qmp->setAudioRole(QAudio::MusicRole);

}

void ClientWindow::startPlaylist() {
    if (ui->playlistWidget->rowCount() > 0) {
        // there's somethin' on a playlist
        socket->write("^START_PLAYLIST^");
        qmp->play();

    }
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
        socket->write("^bye^");
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
        ui->messageBox->append("\nsent " + loadedFileName + " file to server (or attempted to ;))");

    }
}

void ClientWindow::startLoadedAudio() {

    if (qmp) {
        if (dataFromFile.size() != 0) {
            // plik wczytany do dataFromFile

            //connect(qmp, SIGNAL(&QMediaPlayer::positionChanged(qint64)),
            //        this, SLOT(&ClientWindow::positionChanged(qint64)));

            QBuffer *buffer = new QBuffer(qmp);
            buffer->setData(dataFromFile);
            buffer->open(QIODevice::ReadOnly);

            qmp->setMedia(QMediaContent(), buffer);
            qmp->play();

        }
    }
}

void ClientWindow::positionChanged(qint64 progress) {
    ;
}

void ClientWindow::playFromServer() {
    if (songLoaded) {

        QBuffer *buffer = new QBuffer(qmp);
        buffer->setData(*songData);
        buffer->open(QIODevice::ReadOnly);

        qmp->setMedia(QMediaContent(), buffer);
        qmp->play();

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

    socket->connectToHost(host, port);
}

void ClientWindow::audioStahp() {
    // audioOut->stop();
    qmp->stop();
}

void ClientWindow::connSucceeded() {
    ui->destAddr->setEnabled(false);
    songData = new QByteArray();

    this->connectedToServer = true;
    ui->sendButton->setEnabled(true);
    ui->serverPlayButton->setEnabled(true);
}

void ClientWindow::dataAvailable() {

    auto dataRec = socket->readAll();

    // SONG
    if (dataRec.contains("start")) {
        songLoading = true;
        int startPos = dataRec.indexOf("start");
        dataRec = dataRec.mid(startPos + sizeof("start"));
        ui->messageBox->append("song -> there was 'start'' in the packet!");
        ui->messageBox->append(dataRec);
    }
    else if (dataRec.contains("stop")) {
        int stopPos = dataRec.indexOf("stop");
        if (stopPos > 0) {
            dataRec.truncate(stopPos);
            songData->append(dataRec);
        }

        songLoaded = true;
        songLoading = false;
        ui->messageBox->append("song -> there was 'stop'' in the packet!");
        ui->messageBox->append("song size is : " + QString::number(songData->size()));

    }
    //przesuwanie piosenki do pozycji od serwera
    //SPRAWDZIC CZY TO DZIALA!
    if(dataRec.contains("^POS^")) {
        int posPos = dataRec.indexOf("^POS^");
        dataRec = dataRec.mid(posPos + sizeof("^POS^"));
        ui->messageBox->append(dataRec);

        QDataStream ds(dataRec);
        qint64 setMiliFromStart;// = dataRec::toLongLong();
        ds >> setMiliFromStart;
        qmp->setPosition(setMiliFromStart);

    }
    if (songLoading) {
        songData->append(dataRec);
    }

    // PLAYLIST
    if (dataRec.contains("<playlist>")) {
        this->updatePlaylist(dataRec);
    }

    //ui->messageBox->append(QString::number(dataRec.size()));

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

    this->createAudioOutput();
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
