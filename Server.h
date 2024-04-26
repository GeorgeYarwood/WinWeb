#pragma once
#include <WinSock2.h>
#include <thread>
#include <functional>

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


class Server
{
private:
	void ListenLoop();
	void CleanupConnections();
	void CopyRange(char* start, char* end, char* buf, int size);
	void ShutdownInternal(ShutdownReason err);
	SOCKET servSocket = INVALID_SOCKET;
	void PrintToLog(const char* msg);
	bool Readable(SOCKET* socket);
	bool Writable(SOCKET* socket);
	void SetNonBlocking(SOCKET* socket);
	void ProcessRequest(SOCKET* socket, char* data);
	//void GetHeader(ResponseCodes code, char* buf, int len, const char* loc = nullptr, char* contentType = nullptr);
	bool GetFile(char* name, char* ext, char* &retBuf, int &len);
	void SendBuffer(char* buf, SOCKET* dest, int size = -1);
	char* GetTypeFromExtension(char* ext);
	std::thread listenThread;
	std::function<bool(SOCKET*)> readableFunc;
	std::function<bool(SOCKET*)> writableFunc;

public:
	Server();
	~Server();
	void Init(const char* ip, int port);
	State servState = State::UNINITIALISED;
};

