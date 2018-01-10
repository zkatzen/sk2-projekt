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

const struct timespec interval = { 0, 18140 };

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
std::vector<int> clientFds;
std::vector<int> clientFdsMsg;

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
		clientFds.push_back(clientFd);
		
		// tell who has connected
		printf("! New connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
		
		//std::thread d(sendNewDataToClient, clientFd);
		std::thread t(receiveDataFromClient, clientFd);
		
		//d.detach();
		t.detach();

	}

	return 0;
	
}


void receiveDataFromClient(int sock) {
	
	sockaddr_in clientAddr{0};
	socklen_t clientAddrSize = sizeof(clientAddr);
	auto clientFdMsg = accept(servFdMsg, (sockaddr*) &clientAddr, &clientAddrSize);
	if (clientFdMsg == -1) error(1, errno, "accept failed");
	clientFdsMsg.push_back(clientFdMsg);
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
			// printf("<got %d bytes>", bytesRead);
			
			/*char* goodbyeCheck = strstr(buffer, byeMsg);
			if (goodbyeCheck != nullptr) {
				// client has disconnected
				ptrdiff_t pos = std::distance(clientFds.begin(), std::find(clientFds.begin(), clientFds.end(), sock));
				clientFds.erase(clientFds.begin() + pos);
				printf("\nSocket %d has sent goodbye...", sock);
				break;
			}*/		
			/*char* plCheck = strstr(buffer, playlistFire);
			if (plCheck != nullptr) { // otrzymano START PLAYLIST
				if (!playlistOn) { // playlista 'nie leci'
					if (fileNames.size() > 0) { // są piosenki
						
						std::string firstSong = fileNames.at(0);
                                                currFilename = fileNames.at(0);
                                                
						playlistOn = true;
						playlistStartNotify();
						
						//std::thread tt(broadcastToAll, firstSong);	
						//tt.detach();
                                                
				}
				continue; // ?
                            }
                        }*/
			/*char* plStopCheck = strstr(buffer, playlistStop);
			if (plStopCheck != nullptr) { // otrzymano STOP PLAYLIST
				if (playlistOn) { // playlista ' leci'
					playlistStopNotify();
					playlistOn = false;
				}
				continue; // ?
			}*/
			
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
				memcpy(songS, snEnd + sizeof(songNameEnd)-1, (int)((sdStart-buffer)-(snEnd-buffer+sizeof(songNameEnd)-1))); // jaaa...
				songSize = atoi(songS);
								
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
		buffer[0] = '\0';	

	}
	
	printf("\n%d sock's thread has escaped while loop :)\n", sock);	
}

void messagesChannel(int messageSock, int sock) {
    char message[13];
    int bytesRead;
    sendNewDataToClient(messageSock);
    int run = 1;
    while (run == 1) {
        bytesRead = read(messageSock, message, sizeof(message));
        if (bytesRead < 0) {
            // cant read from sock = disconnection?
            printf("! Troubles with reading from %d :C\n", messageSock);
            return;
        }
        else {
			
            printf("Got message: %s :), it was %d bytes. \n ", message, bytesRead);
            
            char* checkBye = strstr(message, byeMsg);
            char* checkFirePlList = strstr(message, playlistFire);
            char* checkPlStop = strstr(message, playlistStop);
            char* checkNext = strstr(message, nextSong);
            
            if(checkBye != nullptr) {
                // client has disconnected
                ptrdiff_t pos = std::distance(clientFds.begin(), std::find(clientFds.begin(), clientFds.end(), sock));
                clientFds.erase(clientFds.begin() + pos);
                
                pos = std::distance(clientFdsMsg.begin(), std::find(clientFdsMsg.begin(), clientFdsMsg.end(), messageSock));
                clientFdsMsg.erase(clientFdsMsg.begin() + pos);
                
                printf("\nSocket %d has sent goodbye...", sock);
                run = 0;
                break;
            }
            
            else if(checkFirePlList != nullptr) {
                if (!playlistOn && fileNames.size() > 0) { // playlista 'nie leci'                   
                    //docelowo klient musi wysyłać nr z playlisty do serwera, wtedy serwer moze ustawic piosenke
                    //teraz wybierana jest piosenka pierwsza i gra sie ją w kółko
                    playlistOn = true;
                    playlistStartNotify();  
                }
            }
            
            else if(checkPlStop != nullptr) {
                if (playlistOn) { // playlista ' leci'
                    playlistStopNotify();
                    playlistOn = false;
                }
            }
            
            else if (checkNext != nullptr) {
				nextSongRequest = true;
			}
            else {
                printf("Warning, couldn't recognise message!\n");
            }
            
        }
    }
}
    
    
    
void playlistStartNotify() {
	for (unsigned int i = 0; i < clientFdsMsg.size(); i++) {
		write(clientFdsMsg[i], playlistFire, sizeof(playlistFire));
	}
	printf("Start playlist -> all clients notified.\n");
}

void playlistStopNotify() {
	for (unsigned int i = 0; i < clientFdsMsg.size(); i++) {
		write(clientFdsMsg[i], playlistStop, sizeof(playlistStop));
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
	for (unsigned int i = 0; i < clientFds.size(); i++) {
		sendPlaylistInfo(clientFdsMsg[i], playlist);
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
    int chunkSize = 32;
    int headerSize = 44; 
    int i, chunksCount;
    
    char *buffer = NULL;
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
					currentFile = 0;
				}
				else {
					printf("Error, tried to start playlist but there is no file to play.\n");
					return;
				}
			}
					
			
            if (!songSet && nextSongRequest) {
				
                std::ifstream myFile (fileNames[currentFile], std::ios::in | std::ios::binary);
                fileSize = getFileSize(myFile);
                buffer = getFileData(myFile);	
                printf("Broadcasting song %s!\n", fileNames[currentFile].c_str());

                chunksCount = (fileSize-headerSize) / chunkSize + 1;
                i = 0;
   
				//songsize/(44100.0 * 2.0 * (16.0/8.0));
                memcpy(header, buffer, headerSize);
                buffer += headerSize;

                printf("Sending start... chunksCount - %d\n", chunksCount);
                
                int res = sprintf(plPosBuf, playlistPos, currentFile+1);

                for (unsigned int i = 0; i < clientFds.size(); i++) {	
					
                    write(clientFdsMsg[i], plPosBuf, res + 1);                    
                    write(clientFdsMsg[i], start_msg, sizeof(start_msg));

                    write(clientFds[i], header, headerSize);
                }
                nanosleep(&interval, NULL);
                
                songSet = true;
                nextSongRequest = false;
                
                printf("Song set\n");
                
            }
            
            if (songSet) {
                
                memcpy(dataChunk, buffer, chunkSize);
                buffer += chunkSize;
                for (unsigned int i = 0; i < clientFds.size(); i++) {
                    write(clientFds[i], dataChunk, chunkSize);
                }
                bytesSent += chunkSize;
                nanosleep(&interval, NULL);
                
                i++;
                
            }
            
            if (songSet && bytesSent >= fileSize - headerSize) {
                for (unsigned int i = 0; i < clientFds.size(); i++) {	
                    write(clientFdsMsg[i], stop_msg, sizeof(stop_msg));
                }
                bytesSent = 0;
                
                currentFile++;
                currentFile = currentFile % fileNames.size();
                
                songSet = false;
                
                //currentPlaying = 0;
                //buffer = NULL;
            }
            
        }
        
    }
    
}

void broadcastToAll(std::string filename) {
	
	// SIGNAL? W przypadku zatrzymania playlisty! Coś jak ctrl+c, x itd.
		
	std::ifstream myFile (filename, std::ios::in | std::ios::binary);
	int fileSize = getFileSize(myFile);
	char *buffer = getFileData(myFile);	
	printf("Broadcasting song %s!\n", filename.c_str());

	int chunkSize = 32;
	int chunksCount = fileSize / chunkSize;
	int headerSize = 44; // tak naprawdę to 44
	
	char dataChunk[chunkSize];
	char header[headerSize];
//songsize/(44100.0 * 2.0 * (16.0/8.0));
	memcpy(header, buffer, headerSize);
	buffer += headerSize;

	printf("Sending start... chunksCount - %d\n", chunksCount);
	
	for (unsigned int i = 0; i < clientFds.size(); i++) {	
		write(clientFds[i], start_msg, sizeof(start_msg));
		write(clientFds[i], header, headerSize);
	}
	nanosleep(&interval, NULL);
	unsigned int clientsCount = clientFds.size();
	for (int i = 0; i < chunksCount; i++) {
		
		// PLAYLIST SUSPEND, A NIE STOP!!!
		
		if (clientFds.size() > clientsCount) {
			// prowizorka max, jak mniej, to przypał...
			write(clientFds.back(), start_msg, sizeof(start_msg));
			write(clientFds.back(), header, headerSize);
			clientsCount = clientFds.size();
		}
		
		//if (i % 1024 == 0)
			//printf("Sending %d...\n", buffer);
			
		memcpy(dataChunk, buffer, chunkSize);
		buffer += chunkSize;
		
		for (unsigned int i = 0; i < clientFds.size(); i++) {
			write(clientFds[i], dataChunk, chunkSize);
		}
		nanosleep(&interval, NULL);
		
	}
	
	for (unsigned int i = 0; i < clientFds.size(); i++) {	
		write(clientFds[i], stop_msg, sizeof(stop_msg));
	}
	printf("ending\n");
}

void broadcastSong(int socket, std::string filename) {
			
	std::ifstream myFile (filename, std::ios::in | std::ios::binary);
	int fileSize = getFileSize(myFile);
	char *buffer = getFileData(myFile);	
	printf("Broadcasting song %s!\n", filename.c_str());
		
	int chunkSize = 32;
	int chunksCount = fileSize / chunkSize;
	int headerSize = 44; // tak naprawdę to 44
	
	char dataChunk[chunkSize];
	char header[headerSize];
	
	memcpy(header, buffer, headerSize);
	buffer += headerSize;

	printf("Sending start... chunksCount - %d\n", chunksCount);
	write(socket, start_msg, sizeof(start_msg));
	write(socket, header, headerSize);
	
	for (int i = 0; i < chunksCount; i++) {
		
		//if (i % 1024 == 0)
			//printf("Sending %d...\n", buffer);
			
		memcpy(dataChunk, buffer, chunkSize);
		buffer += chunkSize;
		
		write(socket, dataChunk, chunkSize);
		nanosleep(&interval, NULL);
		
	}
	
	write(socket, stop_msg, sizeof(stop_msg));
	
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
	for(unsigned int i = 0; i < clientFds.size(); i++)
		close(clientFds[i]);
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

