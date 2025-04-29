#pragma once
#include <WinSock2.h>
#include <thread>
#include <functional>
#include <ws2tcpip.h>
#include "Common.h"
#include <mutex>

#define MAX_HEADER_BUF_SIZE 500
#define MAX_KEEP_ALIVE_REQS 1000
#define KEEP_ALIVE_TIMEOUT 5
#define TO_SECONDS 1000000
#define MAX_PARAMS 20
#define MAX_FILE_SIZE 99999999999999999
#define MAX_PACKET_SIZE 65535 //Max TCP packet size
#define MAX_DIR_TABLE_SIZE 20000
#define MAX_DIR_BUF_SIZE MAX_DIR_TABLE_SIZE + (MAX_PATH * 2)
#define SERVER_NAME "WinWeb"
#define MAX_FILE_NAME_LEN 200


#define HTTP_VER "HTTP/1.1"
#define START_YEAR 1900
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
	Connection(SOCKET sckt, sockaddr_in info, std::function<bool(SOCKET*)> readable, std::function<bool(SOCKET*)> writable, std::function<void(const char*)> printFunc);
	~Connection();
	char ip[INET_ADDRSTRLEN];
	bool pendingDelete = false;
	SOCKET socket;
	std::mutex tickMutex;
	void OnDisconnect();
private:
	std::chrono::steady_clock::time_point lastRecv;
	std::chrono::steady_clock::time_point initTime;
	bool connected = true;
	bool keepAlive = false;
	char* recvBuf;
	void RunConnection();
	bool RecvFromSocket(char* buf);
	void CopyRange(char* start, char* end, char* buf, int size);
	void ProcessRequest(SOCKET* socket, char* data);
	char* AppendDataToHeader(char* headerBuf, char* data, int dataLen, int& totalSize);
	void GetHeader(ResponseCodes code, char* userAgent, char* buf, int len, const char* loc, char* contentType = nullptr);
	bool GetDirectoryListing(char* loc, char*& retBuf);
	void GetConsistentString(char* Buf, int Val);
	int GetStrLen(char* start, char* end);
	bool GetFile(char* name, char* ext, char*& retBuf, int& len);
	bool SendBuffer(char* buf, SOCKET* dest, int size = -1);
	char* GetTypeFromExtension(char* ext);
	std::thread conThread;
	std::function<void(SOCKET*, char*)> OnRecv;
	std::function<bool(SOCKET*)> Readable;
	std::function<bool(SOCKET*)> Writable;
	std::function<void(const char*)> PrintFunc;
	sockaddr_in Info;
};

