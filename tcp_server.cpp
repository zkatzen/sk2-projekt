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

// server socket
int servFd;

// client sockets
std::unordered_set<int> clientFds;

// client threads
std::vector<std::thread> clientThreads;

// files names
std::vector<std::string> fileNames;

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
void sendNewDataToClient(int sock);

void sendPlaylistInfo(int sock, std::string playlist);
void checkClientFd(int sock);
std::string getPlayListString();

void updatePlaylistInfo();

int main(int argc, char ** argv){
	// get and validate port number
	if(argc != 3) error(1, 0, "Need 2 args: port + filename");
	auto port = readPort(argv[1]);
	
	// create socket
	servFd = socket(AF_INET, SOCK_STREAM, 0);
	if(servFd == -1) error(1, errno, "socket failed");
	
	// graceful ctrl+c exit
	signal(SIGINT, ctrl_c);
	// prevent dead sockets from throwing pipe errors on write
	signal(SIGPIPE, SIG_IGN);
	
	setReuseAddr(servFd);
	
	// bind to any address and port provided in arguments
	sockaddr_in serverAddr{.sin_family=AF_INET, .sin_port=htons((short)port), .sin_addr={INADDR_ANY}};
	int res = bind(servFd, (sockaddr*) &serverAddr, sizeof(serverAddr));
	if(res) error(1, errno, "bind failed");
	
	// enter listening mode
	res = listen(servFd, 1);
	if(res) error(1, errno, "listen failed");
	
	char *fileName = argv[2];
	
	// get rozmiar pliku v1
	struct stat results;
	int fileSize = -1;
	if (stat(fileName, &results) == 0) {
			fileSize = results.st_size;
	}
	else {
		printf("Error! Does the file exist?\n");
		return -1;
	}

	std::ifstream myFile (fileName, std::ios::in | std::ios::binary);
	if (myFile.is_open()) {
		
		// get rozmiar pliku v2
		myFile.seekg (0, myFile.end);
		fileSize = myFile.tellg();
		myFile.seekg (0, myFile.beg);
		
		printf("File %s opened, size: %d bytes\n", fileName, fileSize);
	}
	
	// wczytanie wszystkiego do bufora :)
	char *buffer = new char[fileSize]{};
	myFile.read(buffer, fileSize);
	if (myFile) {
		printf("%s read! \n", fileName);
		myFile.close();
	}
	else {
		printf("Troubles reading a song.\n");
	}
	
/****************************/

	// prowizoryczne znaczniki start i koniec przesylania
	char start_msg[] = "start";
	char stop_msg[] = "stop";
	
	while(true) {

		// prepare placeholders for client address
		sockaddr_in clientAddr{0};
		socklen_t clientAddrSize = sizeof(clientAddr);
		
		// accept new connection
		auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
		if (clientFd == -1) error(1, errno, "accept failed");
		
		// add client to all clients set
		clientFds.insert(clientFd);
		
		// tell who has connected
		printf("new connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
		
		// sends a welcome song
		write(clientFd, start_msg, sizeof(start_msg));
				
		int write_result = write(clientFd, buffer, fileSize);
		if (write_result != fileSize)
				printf("some troubles sendin' to fd %d...\n", clientFd); 
		else
				printf("sent all I had to %d <3\n", clientFd);
		
		write(clientFd, stop_msg, sizeof(stop_msg));
		
		std::thread d(sendNewDataToClient, clientFd);
		std::thread t(receiveDataFromClient, clientFd);
		d.detach();
		t.detach();

	}

	return 0;
	
}

void receiveDataFromClient(int sock) {
	printf("hello, im a thread from %d sock, readin' data\n", sock);
	
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
	
	char songNameStart[] = "fn:";
	char songNameEnd[] = ".wav";
	char songDataStart[] = "RIFF";
	
	char byeMsg[] = "^bye^";
	
	while (1) {
		
		// <???>
		if(fileFd != -1 && (unsigned int)(songSize-bytesSong) < sizeof(buffer)) {
			bytesRead = read(sock, buffer, songSize-bytesSong);
		}
		else {
			bytesRead = read(sock, buffer, sizeof(buffer));
		}
		// </???>
		
		if (bytesRead > 0) {
			bytesTotal += bytesRead;
			// printf("<got %d bytes>", bytesRead);
			
			char* goodbyeCheck = strstr(buffer, byeMsg);
			if (goodbyeCheck != nullptr) {
				// client has disconnected
				clientFds.erase(sock);
				printf("\nsocket %d has sent goodbye...", sock);
				break;
			}
			
			snStart = strstr(buffer, songNameStart); // przydałoby się czyścić bufor
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
			//sdStart = snEnd + sizeof(songNameEnd);
			
			if (snEnd!=nullptr && sdStart != nullptr) {
				
				printf("'songDataStart' found\n");
				bytesSong = 0;
				
				memcpy(songS, snEnd + sizeof(songNameEnd)-1, (int)((sdStart-buffer)-(snEnd-buffer+sizeof(songNameEnd)-1))); // jaaa...
				songSize = atoi(songS);
				songDuration = (double) songSize/(44100.0 * 2.0 * (16.0/8.0));
				printf("Song duration: %f\n", songDuration);
				printf("creating new file... ");
				
				char fN[] = "songXXXXXX.wav";
				fileFd = mkostemps(fN,4, O_APPEND);
				printf("success!\n");
				
				fileName = std::string(fN);
				printf("(created file '%s' for '%s')\n", fileName.c_str(), clientsFileName.c_str());
				
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
				printf("\nclosing file '%s'...\n", fileName.c_str());
				if (close(fileFd) == 0) {
					printf("closed!\n");
					fileFd = -1;
				} 
				else {
					printf("error, file couldn't be closed properly!\n");
					// i co? idk
				}
				
				fileNamesDict[fileName] = clientsFileName;
				playList[fileName] = "socket '" + std::to_string(sock) + "'";
				printf("\nSong recived, reseting...bytes total: %d\n", bytesTotal);

				updatePlaylistInfo();
				// new song = info o zmianach na playliście do wszystkich klientów!
				// od momentu dodania piosenki do playlisty musi być atomowy dostęp do niej,
				// aż z-update'ujemy (:d) playlistę każdego klienta

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
			printf("troubles with reading from %d :C\n", sock);
			return;
		}
		buffer[0] = '\0';	

	}
	
	printf("\n%d sock's thread has escaped while loop :)\n", sock);	
}

void sendNewDataToClient(int sock) {
	sendPlaylistInfo(sock, getPlayListString());
}

void updatePlaylistInfo() {
	std::string playlist = getPlayListString();
	if (playlist.length() == 0) {
		return;
	}
	for (const auto& client : clientFds) {
		sendPlaylistInfo(client, playlist);
	}
	printf("playlist -> all clients updated\n");
};

std::string getPlayListString() {
	
	std::string result = "";
	
	if (playList.size() == 0) {
		printf("was to get playlist info, but playlist is empty\n");
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
	
	if (plString.length() == 0) {
		return;
	}
	
	int plSize = dataStr.length();

	char data[plSize + 1];
	dataStr.copy(data, plSize);
	data[plSize] = '\0';
	
	int writeRes = write(sock, data, sizeof(data));
	// printf("wrote with result %d to socket %d, playlist %s\n", writeRes, sock, data);
	if (writeRes == -1) {
		checkClientFd(sock);
		return;
	}

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
	for(int clientFd : clientFds)
		close(clientFd);
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

void sendToAllBut(int fd, char * buffer, int count){
	int res;
	decltype(clientFds) bad;
	for(int clientFd : clientFds){
		if(clientFd == fd) continue;
		res = write(clientFd, buffer, count);
		if(res!=count)
			bad.insert(clientFd);
	}
	for(int clientFd : bad){
		printf("removing %d\n", clientFd);
		clientFds.erase(clientFd);
		close(clientFd);
	}
}
