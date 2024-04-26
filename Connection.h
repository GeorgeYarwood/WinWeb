#pragma once
#include <WinSock2.h>
#include <thread>
#include <functional>
#include <ws2tcpip.h>
#include "Common.h"
#define PACKET_SIZE 200
#define MAX_RETRIES 10
#define MAX_FILE_SIZE 99999999999999999
#define PACKET_SIZE 200
#define SERVER_NAME "WinWeb"

#define HTTP_VER "HTTP/1.1"
#define START_YEAR 1900
#define MAX_RETRIES 10
enum ResponseCodes
{
	ACCEPTED = 202,
	PROCESSING = 102,
	OK = 200,
	INTERNAL_SERVER_ERROR = 500,
	NOT_IMPLEMENTED = 501,
	HTTP_VER_NOT_SUPPORTED = 503,
	NOT_FOUND = 404,
	TEMP_REDIRECT = 302
};

class Connection
{
public:
	Connection(SOCKET sckt, sockaddr_in info, std::function<bool(SOCKET*)> readable, std::function<bool(SOCKET*)> writable);
	~Connection();
	char ip[INET_ADDRSTRLEN];
	bool pendingDelete = false;
private:
	bool connected = true;
	void RunConnection();
	void OnDisconnect();
	bool RecvFromSocket(char* buf);
	void CopyRange(char* start, char* end, char* buf, int size);
	void ProcessRequest(SOCKET* socket, char* data);
	void GetHeader(ResponseCodes code, char* buf, int len, const char* loc = nullptr, char* contentType = nullptr);
	bool GetFile(char* name, char* ext, char*& retBuf, int& len);
	void SendBuffer(char* buf, SOCKET* dest, int size = -1);
	char* GetTypeFromExtension(char* ext);
	std::thread conThread;
	std::function<void(SOCKET*, char*)> OnRecv;
	std::function<bool(SOCKET*)> Readable;
	std::function<bool(SOCKET*)> Writable;
	sockaddr_in Info;
	SOCKET socket;
};

