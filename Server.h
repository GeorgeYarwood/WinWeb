#pragma once
#include <WinSock2.h>
#include <thread>
#include <functional>
#include <mutex>

enum ShutdownReason 
{
	DLL_ERR,
	SOCKET_CREATE_ERR,
	SOCKET_BIND_ERR,
	SOCKET_LISTEN_ERR,
	SET_NON_BLOCK_ERR,
	REQUESTED,
	NONE
};

enum State
{
	UNINITIALISED,
	STARTUP,
	RUNNING,
	SHUTDOWN
};

//TODO expose this and the port we run on in a config file
#define MAX_CONNECTIONS 100

class Server
{
private:
	void ListenLoop();
	void TerminateAllConnections();
	void CleanupConnections();
	void ShutdownInternal(ShutdownReason err);
	void PrintToLogNoLock(const char* msg);
	SOCKET servSocket = INVALID_SOCKET;
	void PrintToLog(const char* msg, bool ShouldLock = true);
	void RedrawInputPrompt();
	COORD SetConsoleCursor();
	bool Readable(SOCKET* socket);
	bool Writable(SOCKET* socket);
	void SetNonBlocking(SOCKET* socket);
	void InputLoop();
	static BOOL ConsoleHandler(DWORD ctrlType);
	std::thread listenThread;
	std::thread inputThread;
	std::function<bool(SOCKET*)> readableFunc;
	std::function<bool(SOCKET*)> writableFunc;
	std::function<void(const char*)> printFunc;
	static Server* instance;
	std::mutex conMutex;
	std::mutex inputMutex;
	std::string inputBuffer;
public:
	Server();
	~Server();
	void Init(const char* ip, int port);
	State servState = State::UNINITIALISED;
};

