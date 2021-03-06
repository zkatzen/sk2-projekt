#include <cstdlib>
#include <cstdio>

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <poll.h> 

#include <thread>
#include <unordered_set>
#include <signal.h>
#include <vector>
#include <map>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stddef.h>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>


#include "Stopwatch.hpp"

struct client {
	
	client(int sock1, int sock2)
		: msgSock(sock1), songSock(sock2) {}
		
	bool operator==(client const& c) {
		return (msgSock == c.msgSock) && 
			   (songSock == c.songSock);
	}
	
	int msgSock;
	int songSock;
	
};

bool operator==(client const& c1, client const& c2) {
	return (c1.msgSock == c2.msgSock) && 
		   (c1.songSock == c2.songSock);
}

namespace std {
template<>
struct hash<client> {
	size_t operator()(const client& c) const
    {
        size_t result = 0;
        boost::hash_combine(result, c.msgSock);
        boost::hash_combine(result, c.songSock);
        return result;
    }
};
}

const struct timespec interval = { 0, 100000000 };

// prowizoryczne znaczniki start i koniec przesylania
char start_msg[] = "^START_SONG^\n";
char stop_msg[] = "^STOOP_SONG^\n";

char songNameStart[] = "fn:";
char songNameEnd[] = ".wav";
char songDataStart[] = "RIFF";

char byeMsg[] = "^GOOD_BYEEE^\n";
char playlistFire[] = "^START_LIST^\n";
char playlistStop[] = "^STOOP_LIST^\n";
char nextSong[] = "^NEXT_SOONG^";

char songUp[] = "SONG_UP_";
char songDown[] = "SONG_DO_";
char songDelete[] = "SONG_DEL_";
char playlistPos[] = "POS%d\n";

// zmienna 'czy nadajemy z playlisty, czy nie?'
std::atomic<bool> playlistOn (false);
std::mutex cv_m;
std::condition_variable cv;

std::atomic<bool> nextSongRequest (false);

std::atomic<int>  currentPlaying (0);

// server socket
int servFd;
int servFdMsg;

// client sockets
std::mutex clients_mutex;
std::unordered_set<client> clients;

// client threads
std::vector<std::thread> clientThreads;

// files names
std::mutex fileNames_mutex;
std::vector<std::string> fileNames;
std::atomic<int> currentFile; // idx for file that we are broadcasting (or clients are listening to)

// clients and servers file names
std::map<std::string, std::string> fileNamesDict;

// song name + client user name
std::map<std::string, std::string> playList;

// handles SIGINT
void ctrl_c(int);

// sends data to clientFds excluding fd
void sendToAllBut(int fd, char * buffer, int count);

// converts cstring to port
uint16_t readPort(char * txt);

// sets SO_REUSEADDR
void setReuseAddr(int sock);

// myslalam czy sie nie przyda, ale na razie nie ma w main'ie
void setKeepAlive(int sock);

// threads stuff
void receiveDataFromClient(int sock, int msgSock);
void messagesChannel(int messageSock, int sock);
void sendNewDataToClient(int sock);

void sendPlaylistInfo(int sock, std::string playlist);
void checkClientFd(int sock);
std::string getPlayListString();

void updatePlaylistInfo();
void broadcastSong(int socket, std::string filename);

int getFileSize(std::ifstream &file); 
char* getFileData(std::ifstream &file);
double getSongDuration(int songsize);

int countDigits(int number);
void sendSongToClient(); 
char* getFileData(std::ifstream &file);
// char* getFileData(FILE **f);

void broadcastToAll(std::string filename);
void playlistStartNotify();
void playlistStopNotify();

void goodbyeSocket(int sock, int messageSock);
int handleServerMsgs(char* msg, int sock, int mesageSock);
void loadSong(int sock, std::string& songInfo, char *currentBuffer, int currentBufSize);

int songsPollFd, msgsPollFd;
epoll_event event;
const int MAX_EVENTS = 500;
struct epoll_event ev, events[MAX_EVENTS];


int main(int argc, char **argv){
	// get and validate port number
	if(argc != 3) error(1, 0, "Need 2 args: port + port");
	auto port = readPort(argv[1]);
	auto portMsg = readPort(argv[2]);
	// create socket
	servFd = socket(AF_INET, SOCK_STREAM, 0);
    servFdMsg = socket(AF_INET, SOCK_STREAM, 0);

	if(servFd == -1 or servFdMsg == -1) error(1, errno, "socket failed");
	
	// graceful ctrl+c exit
	signal(SIGINT, ctrl_c);
	// prevent dead sockets from throwing pipe errors on write
	signal(SIGPIPE, SIG_IGN);
	
	setReuseAddr(servFd);
	setReuseAddr(servFdMsg);
        
	// bind to any address and port provided in arguments
	sockaddr_in serverAddr{.sin_family=AF_INET, .sin_port=htons((short)port), .sin_addr={INADDR_ANY}};
	int res = bind(servFd, (sockaddr*) &serverAddr, sizeof(serverAddr));
	if(res) error(1, errno, "[1] bind failed");
	
	// enter listening mode
	res = listen(servFd, 10); // chyba wieksza ta kolejka
	if(res) 
		error(1, errno, "[1] listen failed");
	
	//again for port for special messages
	sockaddr_in serverAddrMsg{.sin_family=AF_INET, .sin_port=htons((short)portMsg), .sin_addr={INADDR_ANY}};
	res = bind(servFdMsg, (sockaddr*) &serverAddrMsg, sizeof(serverAddrMsg));
	if(res) 
		error(1, errno, "[2] bind failed");
	res = listen(servFdMsg, 10);
	if(res) 
		error(1, errno, "[2] listen failed");
        
    // EPOLL STUFF
    songsPollFd = epoll_create1(0);
    if (songsPollFd < 0) 
		error(1, errno, "Couldn't create SongsEPOLL FD!");
	
	msgsPollFd = epoll_create1(0);
    if (msgsPollFd < 0) 
		error(1, errno, "Couldn't create MsgsEPOLL FD!");
		
	event.events = EPOLLOUT; // available for WRITE
	// /EPOLL STUFF
    
	currentFile = -1;
	std::thread br(sendSongToClient);
    br.detach();
    
	while(true) {

		// prepare placeholders for client address
		sockaddr_in clientAddr{0};
		socklen_t clientAddrSize = sizeof(clientAddr);
		
		printf("\nAwaiting new connections...\n");
		
		// accept new connection
		auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
		if (clientFd == -1) 
			error(1, errno, "[1] accept failed");
		printf("! [1] New connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
		fcntl(clientFd, F_SETFL, O_NONBLOCK, 1);

		event.events = EPOLLOUT; 
		event.data.fd = clientFd;
		if (epoll_ctl(songsPollFd, EPOLL_CTL_ADD, clientFd, &event) == -1) {
			perror("epoll_ctl: client fd sock");
			exit(EXIT_FAILURE);
		}

		auto clientFdMsg = accept(servFdMsg, (sockaddr*) &clientAddr, &clientAddrSize);
		if (clientFdMsg == -1) 
			error(1, errno, "[2] accept failed");
		printf("! [2] New connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFdMsg);
		fcntl(clientFdMsg, F_SETFL, O_NONBLOCK, 1);

		event.data.fd = clientFdMsg;
		if (epoll_ctl(msgsPollFd, EPOLL_CTL_ADD, clientFdMsg, &event) == -1) {
			perror("epoll_ctl: client msg fd sock");
			exit(EXIT_FAILURE);
		}

		std::thread t(receiveDataFromClient, clientFd, clientFdMsg);
		t.detach();

	}

	return 0;
	
}

int handleServerMsgs(char* msg, int sock, int messageSock) {
	char *newLinePtr = strstr(msg, "\n");
	// check what's that!
	if (strstr(msg, byeMsg) != nullptr) {
		goodbyeSocket(sock, messageSock);
		return -1; // so that we know that we should end thread
	}
	else if (strstr(msg, playlistFire) != nullptr) {
		if (!playlistOn && fileNames.size() > 0) {                 
			{
				std::lock_guard<std::mutex> lk(cv_m);
				playlistOn = true;
			}
			cv.notify_all();
			playlistStartNotify();  
		}
	}
	else if (strstr(msg, playlistStop) != nullptr) {
		if (playlistOn) { 
			playlistStopNotify();
			playlistOn = false;
		}
	}
	else if (strstr(msg, nextSong) != nullptr) {
		nextSongRequest = true;
	}
	
	else if( strstr(msg, songUp) != nullptr) {
		char* up = strstr(msg, songUp);
		char songPos[4];
		memcpy(songPos, up + sizeof(songUp)-1, (newLinePtr-msg)-(up-msg+sizeof(songUp)-1));
		songPos[(newLinePtr-msg)-(up-msg+sizeof(songUp)-1)] = '\0';
		int posUp = atoi(songPos);
		{
			std::lock_guard<std::mutex> lk(fileNames_mutex);
			auto temp = fileNames[posUp-1];
			fileNames[posUp-1] = fileNames[posUp];
			fileNames[posUp] = temp;
		}
		updatePlaylistInfo();
	   
	}
	else if( strstr(msg, songDown) != nullptr) {
		char* down = strstr(msg, songDown);
		char songPos[4];
		memcpy(songPos, down + sizeof(songDown)-1, (newLinePtr-msg)-(down-msg+sizeof(songDown)-1));
		songPos[(newLinePtr-msg)-(down-msg+sizeof(songDown)-1)] = '\0';
		int posDown = atoi(songPos);
		{
			std::lock_guard<std::mutex> lk(fileNames_mutex);
			auto temp = fileNames[posDown+1];
			fileNames[posDown+1] = fileNames[posDown];
			fileNames[posDown] = temp;
		}
		updatePlaylistInfo(); 
	}
	else if (strstr(msg, songDelete) != nullptr) {
		char* del = strstr(msg, songDelete);
 		char songPos[4];
		memcpy(songPos, del + sizeof(songDelete)-1, (newLinePtr-msg)-(del-msg+sizeof(songDelete)-1));
		songPos[(newLinePtr-msg)-(del-msg+sizeof(songDelete)-1)] = '\0';
		int posDel = atoi(songPos);
		if(posDel < currentFile) {--currentFile;}
		{
			std::lock_guard<std::mutex> lk(fileNames_mutex);
			auto fileN = fileNames[posDel];             
			fileNames.erase(fileNames.begin()+posDel);
		
			if (remove(fileN.c_str()) != 0) {
				printf("troubles with removing %s\n", fileN.c_str());
			}
			else {
				printf("removing file %s\n", fileN.c_str());
			}
		}
		updatePlaylistInfo();
	}
	else {
		printf("Warning, couldn't recognise message!\n");
	}
	return 0;
}


void receiveDataFromClient(int sock, int msgSock) {
	
	struct client newClient = client(msgSock, sock);
	{
		std::lock_guard<std::mutex> lk(clients_mutex);
		clients.insert(newClient);
	}
	
	std::thread tt(messagesChannel, msgSock, sock);	
	tt.detach();

	std::string fileName;
	std::string clientsFileName;	

	char buffer[1024];
	std::string previousBuffer = std::string("");
	
	int bytesRead;
		
	std::string songInfoStr = std::string("");
		
	std::string buf;
	buf.reserve(1024);
	
	pollfd singlePollFd[1]{};
	singlePollFd[0].fd = sock;
	singlePollFd[0].events = POLLIN;
	
	while (1) {

		poll(singlePollFd, 1, -1);
		if (singlePollFd[0].revents == POLLIN) {

			bytesRead = read(sock, buffer, sizeof(buffer));

			if (bytesRead > 0) {
				
				for (unsigned int i = bytesRead; i < sizeof(buffer); i++)
					buffer[i] = '\0';

				char *checkNewLine = strstr(buffer, "\n");
				
				if (checkNewLine == nullptr) {
					buf += std::string(buffer, bytesRead);
				}
				else {
					buf += std::string(buffer, checkNewLine-buffer);
					printf("End of buffering with result <%s>\n", buf.c_str());
					
					if (checkNewLine+1 < buffer + bytesRead) {
						loadSong(sock, buf, checkNewLine+1, buffer + bytesRead - (checkNewLine+1));
					}
					else {
						loadSong(sock, buf, NULL, 0);
					}

					buf.clear();
					if (checkNewLine-buffer+1 == sizeof(buffer) || checkNewLine + 1 == '\0') {
						// end of message
					}
					else {
						// there's something
						checkNewLine += 1;
						char *anotherLine = strstr(checkNewLine, "\n");
						while (anotherLine != nullptr) {
							std::string message = std::string(checkNewLine, anotherLine-checkNewLine);
							
							// process message ? nie wiem jaka szansa że to się wydarzy
							printf("message %s\n", message.c_str());
							checkNewLine = anotherLine + 1;
							anotherLine = strstr(checkNewLine, "\n");
						}
						// no \n's left
						if (checkNewLine < buffer+bytesRead) {
							buf += std::string(checkNewLine, bytesRead - (checkNewLine-buffer));
							
						}
					}																		
				} 		
			}
			
			else if (bytesRead == 0) {
				// theres nothin'...
			}

			else if (bytesRead < 0) {
				if (errno == EBADF) {
					printf("! [songSock] got error - %d fd : %s\n! [songSock] probably sent 'good-bye'...\n", sock, strerror(errno));
					goodbyeSocket(sock, msgSock);
				}
				else {
					printf("! [songSock] got error - %d fd : %s\n", sock, strerror(errno));
				}
				return;
			}

		}
		else if (singlePollFd[0].revents == POLLNVAL) {
			printf("Song Sock %d has been closed.\n", sock);
			return;
		}
		else {
			printf("Something else happened O.o %d \n", singlePollFd[0].revents);
			return;
		}
	}
	
	printf("\n%d sock's thread has escaped while loop :)\n", sock);	
}

void messagesChannel(int messageSock, int sock) {
	
    int msgBufSize = 32; 
    char message[msgBufSize];
    char *msgPtr;

    bool tempBuffering = false;
    char *previous;
    char *checkNewLine;
    
    std::string temp = std::string("");
    
    int bytesRead;
    sendNewDataToClient(messageSock);
    
    pollfd singlePollFd[1]{};
	singlePollFd[0].fd = messageSock;
	singlePollFd[0].events = POLLIN;
    
    while (1) {
		
		poll(singlePollFd, 1, -1);
		if (singlePollFd[0].revents == POLLIN) {
		
			bytesRead = read(messageSock, message, msgBufSize);
			msgPtr = message;

			if (bytesRead < 0) {
				// cant read from sock = disconnection?
				if (errno == EBADF) {
					printf("! [messageSock] got error - %d fd : %s\n! [messageSock] probably sent 'good-bye'...\n", messageSock, strerror(errno));
					goodbyeSocket(sock, messageSock);
				}
				else {
					printf("! [messageSock] got error - %d fd : %s\n", messageSock, strerror(errno));
				}	
				return;
			}
			
			else if (bytesRead > 0) {
				
				// clear rest of the buffer
				for (int i = bytesRead; i < msgBufSize; i++)
					message[i] = '\0';
				
				if (tempBuffering) {
					temp += std::string(message);
					msgPtr = (char *) temp.c_str();

				}
				
				checkNewLine = strstr(msgPtr, "\n");

				if (checkNewLine == nullptr) {
					// no \n found in all bytes read
					tempBuffering = true;
					temp += std::string(message, sizeof(message));
					msgPtr = (char *) temp.c_str();
				}

				else { // there are some \n's

					while (checkNewLine != nullptr) { // following \n's are found
						
						// check what's that!					
						int result = handleServerMsgs(msgPtr, sock, messageSock);
						if (result == -1)
							return;
										
						previous = checkNewLine;
						checkNewLine = strstr(previous+1, "\n");
							
					}
					// nie ma nastepnych \n
					if (previous+1 < msgPtr + bytesRead) {
						// ale zostaly dane
						temp += std::string(previous+1, (msgPtr+bytesRead - previous+1));
						tempBuffering = true;
					}
					else {
						// wszystko ok?
						checkNewLine = nullptr;
						tempBuffering = false;
						temp = std::string("");
					}
				}
				
			}
		}
		else if (singlePollFd[0].revents == POLLNVAL) {
			printf("Message Sock %d has been closed.\n", messageSock);
			return;
		}
		else {
			/*
			 * Posprawdzać wszystkie opcje co tu się dzieje
			 * Np. 25 = pollin+pollerr+pollhup
			 * 	   17 = pollin+pollhup
			 */ 
			printf("Something else happened O.o %d \n", singlePollFd[0].revents);
			return;
		}
	}
}

void goodbyeSocket(int sock, int messageSock) {

	/*
	 * PROBLEM:
	 * Zamykamy tutaj socket, potem wątek do odbierania piosenek
	 * próbuje wykonać read na zamkniętym sockecie i zwraca -1,
	 * mimo że POLL uznał przed chwilą że było zdarzenie. ???
	 */
	close(sock);
	close(messageSock);

	for (auto it = clients.begin(); it != clients.end(); (*it).songSock == sock ? it = clients.erase(it) : ++it)
    ;
	
	if (clients.empty()) {
		std::cout << "EMPTY...";
		
		playlistOn = false;
		currentFile = -1;
		std::cout << "done! \n";
	}
	printf("\nSocket %d has sent goodbye...\n", messageSock);
}

void loadSong(int sock, std::string& songInfo, char *currentBuffer, int currentBufSize) {
	char buffer[4096];
	
	std::size_t fNStart = songInfo.find(songNameStart) + sizeof(songNameStart) - 1;
	std::size_t fSizeStart = songInfo.find(songNameEnd) + sizeof(songNameEnd) - 1;
	
	if (fNStart == std::string::npos || fSizeStart == std::string::npos) {
		printf("ERROR! loadSong -> Got incorrect songInfo.\n");
		return;
	}
	
	std::string clientsFileName = songInfo.substr(fNStart, fSizeStart - fNStart);
	std::string songSizeStr = songInfo.substr(fSizeStart);
	int songSize = atoi(songSizeStr.c_str()); printf("SONG SIZE %d\n", songSize);
	int bytesWritten = 0;
	
	char fN[] = "songXXXXXX.wav";
	int	fileFd = mkostemps(fN, 4, O_APPEND);
	
	if (currentBufSize > 0) {
		// we got song info + some song data from the beginning of the file in the buffer,
		// so we write it to file
		bytesWritten += write(fileFd, currentBuffer, currentBufSize);
		printf("%d\n", bytesWritten);
	}
	
	int bytesReceived;
	
		
	pollfd singlePollFd[1]{};
	singlePollFd[0].fd = sock;
	singlePollFd[0].events = POLLIN;
	
	while (bytesWritten < songSize) {
		poll(singlePollFd, 1, -1);
		if (singlePollFd[0].revents == POLLIN) {
			bytesReceived = read(sock, buffer, sizeof(buffer));
			write(fileFd, buffer, bytesReceived);
			bytesWritten += bytesReceived;
		}
		else {
			printf("Something else happened O.o %d \n", singlePollFd[0].revents);
		}
	} // songSize + 1 - klient nadał "\n"
	printf("GOT IT!\n");

	std::string fileName = std::string(fN);
	{
		std::lock_guard<std::mutex> lk(fileNames_mutex);
		fileNames.push_back(fileName);
	}
	printf("Loaded song, closing file '%s'...\n", fN);
	
	if (close(fileFd) == 0) {
		printf("-> Closed!\n");
		fileFd = -1;
	} 
	else {
		printf("! Error, file couldn't be closed properly!\n");
	}
	
	fileNamesDict[fileName] = clientsFileName;
	playList[fileName] = "socket '" + std::to_string(sock) + "'";

	updatePlaylistInfo();
	printf("Whole song received. <3\n");
	return;					

};

void playlistStartNotify() {
	std::lock_guard<std::mutex> lk(clients_mutex);
	int nfds = epoll_wait(msgsPollFd, events, MAX_EVENTS, -1);
	for (int n = 0; n < nfds; n++) {
		if (events[n].data.fd != 0) {
			write(events[n].data.fd, playlistFire, sizeof(playlistFire));
		}
	}
	printf("Start playlist -> all clients notified.\n");
}

void playlistStopNotify() {
	std::lock_guard<std::mutex> lk(clients_mutex);
	int nfds = epoll_wait(msgsPollFd, events, MAX_EVENTS, -1);
	for (int n = 0; n < nfds; n++) {
		if (events[n].data.fd != 0) {
			write(events[n].data.fd, playlistStop, sizeof(playlistStop));
		}
	}
	printf("Stop playlist -> all clients notified.\n");
}

void sendNewDataToClient(int sock) {
	std::lock_guard<std::mutex> lk(clients_mutex);
	int nfds = epoll_wait(msgsPollFd, events, MAX_EVENTS, -1);
	for (int n = 0; n < nfds; n++) {
		if (events[n].data.fd == sock) {
			sendPlaylistInfo(sock, getPlayListString());
			if (playlistOn)
				write(sock, playlistFire, sizeof(playlistFire));
		}
	}
}

double getSongDuration(int songsize) {
	return (double) songsize/(44100.0 * 2.0 * (16.0/8.0));
}

void updatePlaylistInfo() {
	std::string playlist = getPlayListString();
	if (playlist.length() == 0) {
		return;
	}
	std::lock_guard<std::mutex> lk(clients_mutex);
	for (client c : clients) {
		sendPlaylistInfo(c.msgSock, playlist);
	}
	printf("Playlist -> all clients updated\n");
}

std::string getPlayListString() {
	
	std::string result = "";
	
	if (playList.size() == 0) {
		printf("! Was to get playlist info, but playlist is empty\n");
		return result;
	}
	
	std::map<std::string, std::string>::iterator it;
	int counter = 1;
	{
		std::lock_guard<std::mutex> lk(fileNames_mutex);
		for (unsigned int i = 0; i < fileNames.size(); i++) {
			result += "<" + std::to_string(counter++) + ":";
			result += fileNamesDict[fileNames[i]] + ":" + playList[fileNames[i]];
		}
	}
	return result;
	
}

void sendPlaylistInfo(int sock, std::string plString) {

	std::string start = "<playlist>";
	std::string end = "<end_playlist>";

	std::string dataStr = "<playlist>" + plString + "<end_playlist>";
	
	int plSize = dataStr.length();

	char data[plSize + 1];
	dataStr.copy(data, plSize);
	data[plSize] = '\0';
	
	//std::lock_guard<std::mutex> lk(clients_mutex);
	int nfds = epoll_wait(msgsPollFd, events, MAX_EVENTS, -1);
	for (int n = 0; n < nfds; n++) {
		if (events[n].data.fd == sock) {
			int writeRes = write(sock, data, sizeof(data));
			if (writeRes == -1) {
				checkClientFd(sock);
				return;
			}
		}
	}
}

int getFileSize(std::ifstream &file) {
	int result = -1;
	if (file.is_open()) {
		// get rozmiar pliku
		file.seekg (0, file.end);
		result = file.tellg();
		file.seekg (0, file.beg);
	}
	return result;
}

char* getFileData(std::ifstream &file) {
	int fileSize = getFileSize(file);
	if (fileSize <= 0)
		printf("(getFileData) -> Could not determine size.\n");
	else
		; // printf("(getFileData) -> File size was %d\n", fileSize);

	char *buffer = new char[fileSize]{};
	file.read(buffer, fileSize);
	if (file) {
		// printf("(getFileData) -> Read %d bytes.\n", fileSize);
		file.close();
	}
	else
		printf("(getFileData) -> Troubles reading file.\n");
		
	return buffer;
}

void sendSongToClient() {
	
    printf("Hello, I'll be the broadcasting thread.\n");
    
    int chunkSize = 17640;
    int headerSize = 44; 
    int i, n, nfds, chunksCount;
    
    char *buffer = NULL;
    char *fileDataStart = NULL;
    
    char header[headerSize];
    char dataChunk[chunkSize];
    
    char plPosBuf[sizeof(playlistPos) + 1]; // ewentualnie dwu cyfrowa liczba
    
    bool songSet = false;
    
    int fileSize;
    int bytesSent = 0;
    
    while (1) {
		std::unique_lock<std::mutex> lk(cv_m);
        cv.wait(lk, []{return playlistOn == true;});
        if (currentFile == -1) {
			printf("no current file");
			if (fileNames.size() > 0) {
				nextSongRequest = true;
                // currentFile = 0;
            }
            else {
                printf("Error, tried to start playlist but there is no file to play.\n");
                return;
            }
        }
			
        if (nextSongRequest) {
            
            currentFile++;
            {
				std::lock_guard<std::mutex> lk(fileNames_mutex);
				currentFile = currentFile % fileNames.size();
			}
				
            std::ifstream myFile (fileNames[currentFile], std::ios::in | std::ios::binary);
            fileSize = getFileSize(myFile);
            
            delete[] fileDataStart;
            fileDataStart = getFileData(myFile);	
            buffer = fileDataStart;
            
            printf("Broadcasting song %s!\n", fileNames[currentFile].c_str());
            chunksCount = (fileSize-headerSize) / chunkSize + 1;
            i = 0;
            bytesSent = 0;
            memcpy(header, buffer, headerSize);
            buffer += headerSize;

            printf("Sending start... chunksCount - %d\n", chunksCount);
            
            {
				std::lock_guard<std::mutex> lk(clients_mutex);    
				int res = sprintf(plPosBuf, playlistPos, currentFile+1);
				nfds = epoll_wait(msgsPollFd, events, MAX_EVENTS, -1);
				for (n = 0; n < nfds; n++) {
					if (events[n].data.fd != 0) {
						write(events[n].data.fd, plPosBuf, res + 1);
						write(events[n].data.fd, start_msg, sizeof(start_msg));
					}
				} 
			
				nfds = epoll_wait(songsPollFd, events, MAX_EVENTS, -1);
				for (n = 0; n < nfds; n++) {
					if (events[n].data.fd != 0)
						write(events[n].data.fd, header, headerSize);
				}
			}  

            bytesSent += headerSize;         
            songSet = true;
            nextSongRequest = false;
            
		}
            
        if (songSet) {
                
			if (fileSize - bytesSent >= chunkSize) { // can send one whole chunk of data (or more)
				memcpy(dataChunk, buffer, chunkSize);
				buffer += chunkSize;
				{
					std::lock_guard<std::mutex> lk(clients_mutex);
					nfds = epoll_wait(songsPollFd, events, MAX_EVENTS, -1);
					for (n = 0; n < nfds; n++) {
						if (events[n].data.fd != 0)
							write(events[n].data.fd, dataChunk, chunkSize);
					}
					bytesSent += chunkSize;
				}
			}
			else { // less than chunkSize of data remains
				int rem = fileSize - bytesSent;
				memcpy(dataChunk, buffer, rem);
				buffer += rem; // (should be) end of file
				{
					std::lock_guard<std::mutex> lk(clients_mutex);
					nfds = epoll_wait(songsPollFd, events, MAX_EVENTS, -1);
					for (n = 0; n < nfds; n++) {
						if (events[n].data.fd != 0)
							write(events[n].data.fd, dataChunk, rem);
					}
				}
				bytesSent += rem;

			}
				
			nanosleep(&interval, NULL);
			i++;
                
        }
           
        if (songSet && bytesSent >= fileSize) {
			{
				std::lock_guard<std::mutex> lk(clients_mutex);
				nfds = epoll_wait(msgsPollFd, events, MAX_EVENTS, -1);
				for (n = 0; n < nfds; n++) {
					if (events[n].data.fd != 0) {
						write(events[n].data.fd, stop_msg, sizeof(stop_msg));
					}
				}
			}

			printf("Sent stop...\n");
			bytesSent = 0;

            songSet = false;
        }
    }
}

int countDigits(int n) {
	int count = 0;
	while (n != 0) {
		n /= 10;
		count++;
	}
	return count;  
} 

void checkClientFd(int sock) {
	printf("! got error when writing to %d fd : %s\n", sock, strerror(errno));
}

uint16_t readPort(char * txt){
	char * ptr;
	auto port = strtol(txt, &ptr, 10);
	if(*ptr!=0 || port<1 || (port>((1<<16)-1))) error(1,0,"illegal argument %s", txt);
	return port;
}

void setReuseAddr(int sock){
	const int one = 1;
	int res = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if(res) error(1,errno, "setsockopt failed");
}

void setKeepAlive(int sock) {
	const int one = 1;
	int res = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
	if(res) error(1,errno, "setkeepalive failed");
}

void ctrl_c(int){
	for (client c : clients) {
		close(c.msgSock);
		close(c.songSock);
	}
	
	for(std::string s : fileNames) {
		if (remove(s.c_str()) != 0) {
			printf("troubles with removing %s\n", s.c_str());
		}
		else {
			printf("removing file %s\n", s.c_str());
		}
	}
	close(servFd);
	close(servFdMsg);
	
	close(songsPollFd);
	close(msgsPollFd);
	
	printf("Closing server\n");
	exit(0);
}


