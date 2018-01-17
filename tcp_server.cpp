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

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stddef.h>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>

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
char playlistPos[] = "POS%d\n";

// zmienna 'czy nadajemy z playlisty, czy nie?'
bool playlistOn = false;
char *dataBuffer;

bool nextSongRequest = false;

std::string currFilename;
int currentPlaying = 0;

// server socket
int servFd;
int servFdMsg;

// client sockets
std::unordered_set<client> clients;

// client threads
std::vector<std::thread> clientThreads;

// files names
std::vector<std::string> fileNames;
int currentFile; // idx for file that we are broadcasting (or clients are listening to)

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
void receiveDataFromClient(int sock);
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
	if(res) error(1, errno, "bind failed");
	
	// enter listening mode
	res = listen(servFd, 1);
	if(res) error(1, errno, "listen failed");
	
	//again for port for special messages
	sockaddr_in serverAddrMsg{.sin_family=AF_INET, .sin_port=htons((short)portMsg), .sin_addr={INADDR_ANY}};
	res = bind(servFdMsg, (sockaddr*) &serverAddrMsg, sizeof(serverAddrMsg));
	if(res) error(1, errno, "bind failed");
	res = listen(servFdMsg, 1);
	if(res) error(1, errno, "listen failed");
        
	currentFile = -1;
/****************************/
	std::thread br(sendSongToClient);
    br.detach();
    
	while(true) {

		// prepare placeholders for client address
		sockaddr_in clientAddr{0};
		socklen_t clientAddrSize = sizeof(clientAddr);
		
		// accept new connection
		auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
		if (clientFd == -1) error(1, errno, "accept failed");
		
		// add client to all clients set
		// clientFds.push_back(clientFd);
		
		// tell who has connected
		printf("! New connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
		
		//std::thread d(sendNewDataToClient, clientFd);
		std::thread t(receiveDataFromClient, clientFd);
		
		//d.detach();
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
	else {
		printf("Warning, couldn't recognise message!\n");
	}
	return 0;
}


void receiveDataFromClient(int sock) {
	
	sockaddr_in clientAddr{0};
	socklen_t clientAddrSize = sizeof(clientAddr);
	auto clientFdMsg = accept(servFdMsg, (sockaddr*) &clientAddr, &clientAddrSize);
	if (clientFdMsg == -1) 
		error(1, errno, "accept failed");
	
	struct client newClient = client(clientFdMsg, sock);
	clients.insert(newClient);
	
	printf("! New messaging chanell from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFdMsg);
	
	std::thread tt(messagesChannel, clientFdMsg, sock);	
	tt.detach();
        
	int fileFd = -1;
	int bytesWritten;
	
	std::string fileName;
	std::string clientsFileName;	
	
	int songSize = 0;	
	char songS[20];
	
	char buffer[4096]; // tu może więcej gazu
	
	int bytesTotal = 0, bytesRead, bytesSong = 0;
	char *snStart, *snEnd, *sdStart;
	double songDuration = 0.0;

	
	while (1) {
                
		if(fileFd != -1 && (unsigned int)(songSize-bytesSong) < sizeof(buffer))
			bytesRead = read(sock, buffer, songSize-bytesSong);
		else 
			bytesRead = read(sock, buffer, sizeof(buffer));

		if (bytesRead > 0) {
			bytesTotal += bytesRead;

			snStart = strstr(buffer, songNameStart);
			if( snStart != nullptr) {
				//printf("\n'songNameStart' found:\n%s\n", snStart + sizeof(songNameStart) - 1);				
			}	
		
			snEnd = strstr(buffer, songNameEnd);
			if (snEnd != nullptr) {
				char getFn[snEnd - (snStart + sizeof(songNameStart) - 1)];
				strncpy(getFn, snStart + sizeof(songNameStart) - 1, snEnd - (snStart + sizeof(songNameStart) - 1));
				clientsFileName = std::string(getFn);
			}
			
			sdStart = strstr(buffer, songDataStart);
			if (snEnd != nullptr && sdStart != nullptr) {
				
				bytesSong = 0;
				memcpy(songS, snEnd + sizeof(songNameEnd)-1, (sdStart-buffer)-(snEnd-buffer+sizeof(songNameEnd)-1)); // jaaa...
				songS[(sdStart-buffer)-(snEnd-buffer+sizeof(songNameEnd)-1)] = '\0';
				songSize = atoi(songS);
				printf("SongSize = %d\n", songSize);
								
				char fN[] = "songXXXXXX.wav";
				fileFd = mkostemps(fN,4, O_APPEND);
				
				fileName = std::string(fN);				
				bytesWritten = write(fileFd, sdStart, bytesRead-(int)(sdStart-buffer));
				bytesSong += bytesWritten;
				
			}	
			
			if (fileFd != -1) {
				// file was created and we're writin'
				bytesWritten = write(fileFd, buffer, bytesRead);
				bytesSong += bytesWritten;
			}
			
			if (bytesSong == songSize) {
				// udalo sie zapisac do pliku całą piosenkę (na podstawie ilości bajtów)
				fileNames.push_back(fileName);
				printf("\nLoaded song, closing file '%s'...\n", fileName.c_str());
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
				bytesSong = 0;
				fileFd = -1;
				songS[0] = '\0';	
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
		// buffer[0] = '\0';	

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
    
    unsigned int clientsCount = 0;
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
	free(dataBuffer);
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
	printf("Closing server\n");
	exit(0);
}


