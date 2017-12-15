#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <unistd.h>
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

// server socket
int servFd;

// client sockets
std::unordered_set<int> clientFds;

// handles SIGINT
void ctrl_c(int);

// sends data to clientFds excluding fd
void sendToAllBut(int fd, char * buffer, int count);

// converts cstring to port
uint16_t readPort(char * txt);

// sets SO_REUSEADDR
void setReuseAddr(int sock);

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
	printf("%s read! \n", fileName);
	myFile.close();
	
	/*
	std::thread
	t([&] {
			
			char start_msg[] = "song start";
			char stop_msg[] = "song stop";
			
			while (1) {
				for (int clientFd : clientFds) {
					
					write(clientFd, start_msg, sizeof(start_msg));
					
					int write_result = write(clientFd, buffer, fileSize);
					if (write_result != fileSize) {
							printf("some troubles sendin' to fd %d...\n", clientFd); 
					}
					else {
							printf("sent to %d\n", clientFd);
					}
					
					write(clientFd, stop_msg, sizeof(stop_msg));

				}
				sleep(5);
			}
	});
	*/
/****************************/

	// prowizoryczne znaczniki start i koniec przesylania
	char start_msg[] = "start";
	char stop_msg[] = "stop";
	
	while(true){

		// prepare placeholders for client address
		sockaddr_in clientAddr{0};
		socklen_t clientAddrSize = sizeof(clientAddr);
		
		// accept new connection
		auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
		if(clientFd == -1) error(1, errno, "accept failed");
		
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


	}
/****************************/
		
		// read a message
		/*char buffer[255];
		int count = read(clientFd, buffer, 255);
		
		if(count < 1) {
			printf("removing %d\n", clientFd);
			clientFds.erase(clientFd);
			close(clientFd);
			continue;
		} else {
			// broadcast the message
			sendToAllBut(clientFd, buffer, count);
		}*/
		
	//}
/****************************/

	return 0;
	
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

void ctrl_c(int){
	for(int clientFd : clientFds)
		close(clientFd);
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
