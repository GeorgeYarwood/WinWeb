#include "Server.h"
#include <iostream>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include "Connection.h"

std::vector<Connection*> connections;
Server* Server::instance = NULL;

Server::Server()
{
	instance = this;
}

Server::~Server()
{
	conMutex.lock();
	inputMutex.lock();

	if (listenThread.joinable())
	{
		listenThread.join();
	}
	if (inputThread.joinable())
	{
		inputThread.join();
	}

	conMutex.unlock();
	inputMutex.unlock();
}

void Server::Init(const char* ip, int port)
{
	SetConsoleCtrlHandler(ConsoleHandler, TRUE);

	servState = State::STARTUP;

	WSAData data;
	int err;
	WORD vReq = MAKEWORD(2, 2);
	err = WSAStartup(vReq, &data);

	if (err != 0)
	{
		ShutdownInternal(ShutdownReason::DLL_ERR);
		return;
	}

	servSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (servSocket == INVALID_SOCKET)
	{
		ShutdownInternal(ShutdownReason::SOCKET_CREATE_ERR);
		return;
	}

	sockaddr_in service;
	service.sin_family = AF_INET;

	if (!strcmp(ip, "ANY"))
	{
		service.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		char buf[INET_ADDRSTRLEN];
		buf[INET_ADDRSTRLEN - 1] = 0;
		inet_pton(AF_INET, ip, &buf);
		service.sin_addr.s_addr = (ULONG)buf;
	}
	service.sin_port = htons(port);

	if (bind(servSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR)
	{
		ShutdownInternal(ShutdownReason::SOCKET_BIND_ERR);
		return;
	}

	if (listen(servSocket, 1) == SOCKET_ERROR)
	{
		ShutdownInternal(ShutdownReason::SOCKET_LISTEN_ERR);
		return;
	}

	SetNonBlocking(&servSocket);

	if (servState == State::SHUTDOWN)
	{
		return;
	}

	readableFunc = [this](SOCKET* sckt)
		{
			return Readable(sckt);
		};


	writableFunc = [this](SOCKET* sckt)
		{
			return Writable(sckt);
		};

	printFunc = [this](const char* msg)
		{
			return PrintToLog(msg);
		};


	SetConsoleCursor();

	listenThread = std::thread(&Server::ListenLoop, this);
	listenThread.detach();
	inputThread = std::thread(&Server::InputLoop, this);
	inputThread.detach();
}

void Server::InputLoop()
{
	HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
	DWORD consoleMode = 0;
	GetConsoleMode(inputHandle, &consoleMode);
	SetConsoleMode(inputHandle, consoleMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
	std::cout << ">> ";
	while (servState != State::SHUTDOWN)
	{
		//Poll user input and push to buffer
		INPUT_RECORD record;
		DWORD read;
		ReadConsoleInput(inputHandle, &record, 1, &read);

		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
		{
			char c = record.Event.KeyEvent.uChar.AsciiChar;
			inputMutex.lock();

			if(c == '\r') //Return key
			{
				std::string cpyBuf = inputBuffer;
				for (int i = 0; i < cpyBuf.length(); i++)
				{
					cpyBuf[i] = (char)tolower(cpyBuf[i]);
				}

				if (cpyBuf == "help")
				{
					PrintToLogNoLock("---------------- Help ----------------");
					PrintToLogNoLock("Shutdown - Gracefully shutdown the server");
					PrintToLogNoLock("Help - Displays this menu");
					PrintToLogNoLock("Connections - Displays the current connections");
				}
				else if (cpyBuf == "shutdown")
				{
					inputMutex.unlock();
					ShutdownInternal(ShutdownReason::REQUESTED);
					return;
				}
				else if (cpyBuf == "connections")
				{
					PrintToLogNoLock("---------------- Connections ----------------");

					conMutex.lock();
					char buf[256];
					sprintf_s(buf, "%zu current connections", connections.size());
					PrintToLogNoLock(buf);

					for (int c = 0; c < connections.size(); c++)
					{
						memset(&buf[0], 0, 255);
						sprintf_s(buf, "%i: %s", c, connections[c]->ip);
						PrintToLogNoLock(buf);
					}

					conMutex.unlock();
				}
				else
				{
					char buf[256];
					sprintf_s(buf, "Unrecognised command '%s'", inputBuffer.c_str());

					PrintToLogNoLock(buf);
				}
				
				inputBuffer.clear();
				RedrawInputPrompt();
			}
			else if (c == '\b') //Backspace
			{
				if (!inputBuffer.empty())
				{
					inputBuffer.pop_back();
					RemoveChar();
				}
			}
			else
			{
				inputBuffer += c;
				AppendChar((char*) &inputBuffer.c_str()[inputBuffer.size() - 1]);
			}

			//RedrawInputPrompt();
			inputMutex.unlock();
		}

		std::this_thread::sleep_for(std::chrono::microseconds(500));
	}
}

void Server::SetNonBlocking(SOCKET* socket)
{
	if (!socket || *socket == SOCKET_ERROR)
	{
		return;
	}

	u_long uL = 1;
	int ret = ioctlsocket(*socket, FIONBIO, &uL);

	if (ret == SOCKET_ERROR)
	{
		ShutdownInternal(ShutdownReason::SET_NON_BLOCK_ERR);
	}
}

void Server::ShutdownInternal(ShutdownReason err)
{
	servState = State::SHUTDOWN;

	switch (err)
	{
		case DLL_ERR:
			PrintToLog("ERROR-> Failed loading DLL <-ERROR");
			break;
		case SOCKET_CREATE_ERR:
			PrintToLog("ERROR-> Failed to create socket <-ERROR");
			break;
		case SOCKET_BIND_ERR:
			PrintToLog("ERROR-> Failed binding socket <-ERROR");
			break;
		case SOCKET_LISTEN_ERR:
			PrintToLog("ERROR-> Failed listening on socket <-ERROR");
			break;
		case REQUESTED:
			PrintToLog("WARNING-> Requested shutdown <-WARNING");
		case NONE:
			break;
	}

	conMutex.lock();

	TerminateAllConnections();

	if (servSocket != INVALID_SOCKET)
	{
		closesocket(servSocket);
	}

	WSACleanup();

	conMutex.unlock();
}

void Server::PrintToLogNoLock(const char* msg)
{
	PrintToLog(msg, false);
}

void Server::PrintToLog(const char* msg, bool ShouldLock)
{
	if (ShouldLock) 
	{
		inputMutex.lock();
	}
	
	//CONSOLE_SCREEN_BUFFER_INFO csbi;
	//GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	//COORD logPos = { 0, static_cast<SHORT>(csbi.srWindow.Bottom - 1) };
	//SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), logPos);

	std::cout << "\r" << msg << "                                       " <<  std::endl;
	RedrawInputPrompt();

	if (ShouldLock)
	{
		inputMutex.unlock();
	}
}

void Server::RedrawInputPrompt()
{
	COORD inputPos = SetConsoleCursor();

	std::cout << "\r>> " << inputBuffer << "                                       "; // clear line end
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), { static_cast<SHORT>(3 + inputBuffer.size()), inputPos.Y });
}

void Server::AppendChar(char* newChar)
{
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(handle, &csbi);
	COORD cursorPos = csbi.dwCursorPosition;
	cursorPos.X += 1;
	WriteConsoleA(handle, newChar, 1, NULL, NULL);
	//SetConsoleCursorPosition(handle, cursorPos);
}

void Server::RemoveChar()
{
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(handle, &csbi);

	COORD cursorPos = csbi.dwCursorPosition;
	cursorPos.X -= 1;
	SetConsoleCursorPosition(handle, cursorPos);

	char buf[1];
	buf[0] = ' ';
	WriteConsoleA(handle, &buf[0], 1, NULL, NULL);

	GetConsoleScreenBufferInfo(handle, &csbi);

	cursorPos = csbi.dwCursorPosition;
	cursorPos.X -= 1;
	SetConsoleCursorPosition(handle, cursorPos);
}

COORD Server::SetConsoleCursor()
{
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(handle, &csbi);
	COORD inputPos = { 0, csbi.srWindow.Bottom };

	SetConsoleCursorPosition(handle, inputPos);
	return inputPos;
}

bool Server::Readable(SOCKET* socket)
{
	if (socket == nullptr)
	{
		return false;
	}
	static timeval dontBlock = { 0,0 }; //We poll this, don't make us block
	fd_set rfd;
	FD_ZERO(&rfd);
	FD_SET(*socket, &rfd);
	int ret = select(0, &rfd, NULL, NULL, &dontBlock);
	return ret != SOCKET_ERROR && ret > 0;
}

bool Server::Writable(SOCKET* socket)
{
	if (socket == nullptr)
	{
		return false;
	}
	static timeval dontBlock = { 0,0 };
	fd_set wfd;
	FD_ZERO(&wfd);
	FD_SET(*socket, &wfd);
	int ret = select(0, NULL, &wfd, NULL, &dontBlock);
	return ret != SOCKET_ERROR && ret > 0;
}

void Server::ListenLoop()
{
	if (servSocket == INVALID_SOCKET)
	{
		ShutdownInternal(ShutdownReason::SOCKET_CREATE_ERR);
		return;
	}

	servState = State::RUNNING;

	SOCKET acceptSocket;

	sockaddr_in acceptInfo;
	int acceptSize = sizeof(acceptInfo);

	while (servState == State::RUNNING)
	{
		conMutex.lock();
		if (connections.size() < MAX_CONNECTIONS && Readable(&servSocket))
		{
			acceptSocket = accept(servSocket, (SOCKADDR*)&acceptInfo, &acceptSize);

			if (acceptSocket == INVALID_SOCKET)
			{
				continue;
			}

			SetNonBlocking(&acceptSocket);

			Connection* newCon = new Connection(acceptSocket, acceptInfo, readableFunc, writableFunc, printFunc);
			connections.push_back(newCon);
			char logBuf[200];
			sprintf_s(logBuf, "Accepted connection from %s", newCon->ip);
			PrintToLog(logBuf);
		}

		CleanupConnections();
		conMutex.unlock();

		std::this_thread::sleep_for(std::chrono::microseconds(500));
	}
}

void Server::TerminateAllConnections()
{
	if (connections.size() == 0)
	{
		return;
	}

	for (int i = connections.size() - 1; i >= 0; i--)
	{
		connections[i]->tickMutex.lock();
		connections[i]->OnDisconnect();
		connections[i]->tickMutex.unlock();

		char logBuf[200];
		sprintf_s(logBuf, "Terminated connection from %s for shutdown", connections[i]->ip);

		delete connections[i];
		connections.erase(connections.begin() + i);
		PrintToLog(logBuf);

	}
}

void Server::CleanupConnections()
{
	if (connections.size() == 0)
	{
		return;
	}
	for (int i = connections.size() - 1; i >= 0; i--)
	{
		if (connections[i]->pendingDelete)
		{
			char logBuf[200];
			sprintf_s(logBuf, "Closing connection from %s", connections[i]->ip);

			delete connections[i];
			connections.erase(connections.begin() + i);
			PrintToLog(logBuf);
		}
	}
}

BOOL Server::ConsoleHandler(DWORD ctrlType)
{
	if (instance)
	{
		instance->ShutdownInternal(ShutdownReason::NONE);

		return TRUE;
	}
	return FALSE;
}

