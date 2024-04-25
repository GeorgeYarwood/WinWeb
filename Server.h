#pragma once
#include <WinSock2.h>
#include <thread>
#include <functional>

#define MAX_FILE_SIZE 99999999999999999
#define PACKET_SIZE 200
#define SERVER_NAME "WinWeb"
#define SERVER_MAJOR 0
#define SERVER_MINOR 2
#define HTTP_VER "HTTP/1.1"
#define START_YEAR 1900
#define MAX_RETRIES 10
enum ShutdownReason 
{
	DLL_ERR,
	SOCKET_CREATE_ERR,
	SOCKET_BIND_ERR,
	SOCKET_LISTEN_ERR,
	SET_NON_BLOCK_ERR,
	NONE
};

enum State
{
	UNINITIALISED,
	STARTUP,
	RUNNING,
	SHUTDOWN
};

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

class Server
{
private:
	void ListenLoop();
	void CopyRange(char* start, char* end, char* buf, int size);
	void ShutdownInternal(ShutdownReason err);
	SOCKET servSocket = INVALID_SOCKET;
	void PrintToLog(const char* msg);
	bool Readable(SOCKET* socket);
	bool Writable(SOCKET* socket);
	void SetNonBlocking(SOCKET* socket);
	void ProcessRequest(SOCKET* socket, char* data);
	void GetHeader(ResponseCodes code, char* buf, int len, const char* loc = nullptr, char* contentType = nullptr);
	bool GetFile(char* name, char* ext, char* &retBuf, int &len);
	void SendBuffer(char* buf, SOCKET* dest, int size = -1);
	char* GetTypeFromExtension(char* ext);
	std::thread listenThread;
	std::function<void(SOCKET*, char*)> recvFunc;
	std::function<bool(SOCKET*)> readableFunc;
	std::function<bool(SOCKET*)> writableFunc;

public:
	Server();
	~Server();
	void Init(const char* ip, int port);
	State servState = State::UNINITIALISED;
};

