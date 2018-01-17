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

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stddef.h>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>

#include <sys/epoll.h>

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

std::atomic<bool> nextSongRequest (false);

std::atomic<int>  currentPlaying (0);

// server socket
int servFd;
int servFdMsg;

// client sockets
std::unordered_set<client> clients;

// client threads
std::vector<std::thread> clientThreads;

// files names
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

int main(int argc, char **argv){
	// get and validate port number
	if(argc != 4) error(1, 0, "Need 3 args: port + filename + port");
	auto port = readPort(argv[1]);
	auto portMsg = readPort(argv[3]);
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
        
	currentFile = -1;
	std::thread br(sendSongToClient);
    br.detach();
    	//currentPlaying = 0;
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

		auto clientFdMsg = accept(servFdMsg, (sockaddr*) &clientAddr, &clientAddrSize);
		if (clientFdMsg == -1) 
			error(1, errno, "[2] accept failed");
		printf("! [2] New connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFdMsg);

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
			playlistOn = true;
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
		//TODO: co jeśli jedna z zamienianych piosenek to ta co teraz gra?
		//if (posUp == currentPlaying) {currentPlaying--;}
		//else if(posUp == (currentPlaying-1)) {currentPlaying++;}
		auto temp = fileNames[posUp-1];
		fileNames[posUp-1] = fileNames[posUp];
		fileNames[posUp] = temp;
		updatePlaylistInfo();
	   
	}
	else if( strstr(msg, songDown) != nullptr) {
		char* down = strstr(msg, songDown);
		char songPos[4];
		memcpy(songPos, down + sizeof(songDown)-1, (newLinePtr-msg)-(down-msg+sizeof(songDown)-1));
		songPos[(newLinePtr-msg)-(down-msg+sizeof(songDown)-1)] = '\0';
		int posDown = atoi(songPos);
		//TODO: co jeśli jedna z zamienianych piosenek to ta co teraz gra?
		//if (posUp == currentPlaying) {currentPlaying--;}
		//else if(posUp == (currentPlaying-1)) {currentPlaying++;}
		auto temp = fileNames[posDown+1];
		fileNames[posDown+1] = fileNames[posDown];
		fileNames[posDown] = temp;
		updatePlaylistInfo(); 
	}
	else if (strstr(msg, songDelete) != nullptr) {
		char* del = strstr(msg, songDelete);
 		char songPos[4];
		memcpy(songPos, del + sizeof(songDelete)-1, (newLinePtr-msg)-(del-msg+sizeof(songDelete)-1));
		songPos[(newLinePtr-msg)-(del-msg+sizeof(songDelete)-1)] = '\0';
		int posDel = atoi(songPos);
		if(posDel < currentFile) {--currentFile;}
		auto fileN = fileNames[posDel];             
		fileNames.erase(fileNames.begin()+posDel);
             
		if (remove(fileN.c_str()) != 0) {
			printf("troubles with removing %s\n", fileN.c_str());
		}
		else {
			printf("removing file %s\n", fileN.c_str());
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
	clients.insert(newClient);
		
	std::thread tt(messagesChannel, msgSock, sock);	
	tt.detach();

	std::string fileName;
	std::string clientsFileName;	

	char buffer[1024]; // tu może więcej gazu
	std::string previousBuffer = std::string("");
	
	int bytesRead;
	//double songDuration = 0.0;
	
	std::string songInfoStr = std::string("");
	// BLA BLA BLA
	
	std::string buf;
	buf.reserve(1024);
	
	while (1) {

		bytesRead = read(sock, buffer, sizeof(buffer));

		if (bytesRead > 0) {
			
			for (unsigned int i = bytesRead; i < sizeof(buffer); i++)
				buffer[i] = '\0';
							printf("%s\n", buffer);

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
			// cant read from sock = disconnection?
			printf("! Troubles with reading from %d :C\n", sock);
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
    
    while (1) {
		
        bytesRead = read(messageSock, message, msgBufSize);
        msgPtr = message;

        if (bytesRead < 0) {
            // cant read from sock = disconnection?
            printf("! Troubles with reading from %d :C\n", messageSock);
            return;
        }
        
        else if (bytesRead > 0) {
			
			// clear rest of the buffer
			for (int i = bytesRead; i < msgBufSize; i++)
				message[i] = '\0';
            //printf("Got message: %.8s :), it was %d bytes.\n ", message, bytesRead);
            
            if (tempBuffering) {
				temp += std::string(message);
				msgPtr = (char *) temp.c_str();
				// printf("After concat: %s\n", msgPtr);

			}
            
            checkNewLine = strstr(msgPtr, "\n");
       		// printf("What's going on? : %s, %d, %d, %d \n", checkNewLine, message, checkNewLine, checkNewLine-message);

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
}

void goodbyeSocket(int sock, int messageSock) {
	clients.erase(client(sock, messageSock));
	printf("\nSocket %d has sent goodbye...", sock);
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
	// printf("[%s], [%s]\n", clientsFileName.c_str(), songSize.c_str());
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
	while (bytesWritten < songSize) {
		bytesReceived = read(sock, buffer, sizeof(buffer));
		write(fileFd, buffer, bytesReceived);
		bytesWritten += bytesReceived;
	} // songSize + 1 - klient nadał "\n"
	printf("GOT IT!\n");

	std::string fileName = std::string(fN);
	fileNames.push_back(fileName);
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
	for (client c : clients) {
		write(c.msgSock, playlistFire, sizeof(playlistFire));
	}
	printf("Start playlist -> all clients notified.\n");
}

void playlistStopNotify() {
	for (client c : clients) {
		write(c.msgSock, playlistStop, sizeof(playlistStop));
	}
	printf("Stop playlist -> all clients notified.\n");
}


void sendNewDataToClient(int sock) {
	sendPlaylistInfo(sock, getPlayListString());
	if (playlistOn)
		write(sock, playlistFire, sizeof(playlistFire));
}

double getSongDuration(int songsize) {
	return (double) songsize/(44100.0 * 2.0 * (16.0/8.0));
}

void updatePlaylistInfo() {
	std::string playlist = getPlayListString();
	if (playlist.length() == 0) {
		return;
	}
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
	for (unsigned int i = 0; i < fileNames.size(); i++) {
		result += "<" + std::to_string(counter++) + ":";
		result += fileNamesDict[fileNames[i]] + ":" + playList[fileNames[i]];
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
	
	int writeRes = write(sock, data, sizeof(data));
	if (writeRes == -1) {
		checkClientFd(sock);
		return;
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
    int i, chunksCount;
    
    char *buffer = NULL;
    char *fileDataStart = NULL;
    
    char header[headerSize];
    char dataChunk[chunkSize];
    
    char plPosBuf[sizeof(playlistPos) + 1]; // ewentualnie dwu cyfrowa liczba
    
    bool songSet = false;
    
    int fileSize;
    int bytesSent = 0;
    
    while (1) {
		
        while (playlistOn) {
			
        	if (currentFile == -1) {
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
                currentFile = currentFile % fileNames.size();
				
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
                
                int res = sprintf(plPosBuf, playlistPos, currentFile+1);

                for (client c : clients) {
					write(c.msgSock, plPosBuf, res + 1);                    
                    write(c.msgSock, start_msg, sizeof(start_msg));

                    write(c.songSock, header, headerSize);
				}      
                
                bytesSent += headerSize;         
                songSet = true;
                nextSongRequest = false;
                
                printf("Song set\n");
                
                nanosleep(&interval, NULL);
            }
            
            if (songSet) {
                
                if (fileSize - bytesSent >= chunkSize) { // can send one whole chunk of data (or more)
					memcpy(dataChunk, buffer, chunkSize);
					buffer += chunkSize;
					for (client c : clients) {
						write(c.songSock, dataChunk, chunkSize);
					}
					bytesSent += chunkSize;
					// printf("Sent %d bytes, bytes left to send: %d\n", bytesSent, fileSize - bytesSent);
				}
				
				else { // less than chunkSize of data remains
					int rem = fileSize - bytesSent;
					memcpy(dataChunk, buffer, rem);
					buffer += rem; // (should be) end of file
					for (client c : clients) {
						write(c.songSock, dataChunk, rem);
					}
					bytesSent += rem;
					// printf("[REM PART!] Sent %d bytes, bytes left to send: %d\n", bytesSent, fileSize - bytesSent);

				}
				
                nanosleep(&interval, NULL);
                i++;
                
            }
            
            if (songSet && bytesSent >= fileSize) {

                for (client c : clients) {
					write(c.msgSock, stop_msg, sizeof(stop_msg));
				}
                
                printf("Sent stop...\n");
                bytesSent = 0;

                songSet = false;
            }
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
	// obsluga błędów
	// man7.org/linux/man-pages/man2/write.2.html - duzo ich tutaj
	// (usunięcie z cilentFds i zamknięcie wątku)
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
	printf("Closing server\n");
	exit(0);
}


