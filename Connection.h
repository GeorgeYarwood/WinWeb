#pragma once
#include <WinSock2.h>
#include <thread>
#include <functional>
#include <ws2tcpip.h>

#define PACKET_SIZE 200
#define MAX_RETRIES 10

class Connection
{
public:
	Connection(SOCKET sckt, sockaddr_in info, std::function<void(SOCKET*, char*)> delegate, std::function<bool(SOCKET*)> readable, std::function<bool(SOCKET*)> writable);
	~Connection();
	char ip[INET_ADDRSTRLEN];
private:
	bool connected = true;
	void RunConnection();
	void OnDisconnect();
	bool RecvFromSocket(char* buf);
	std::thread conThread;
	std::function<void(SOCKET*, char*)> OnRecv;
	std::function<bool(SOCKET*)> Readable;
	std::function<bool(SOCKET*)> Writable;
	sockaddr_in Info;
	SOCKET socket;
};

