#include "Server.h"
#include <iostream>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include "Connection.h"

Server::Server()
{
}

Server::~Server()
{
}

std::vector<Connection*> connections;

void Server::Init(const char* ip, int port)
{
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

	/*recvFunc = [this](SOCKET* sckt, char* data)
		{
			ProcessRequest(sckt, data);
		};*/


	readableFunc = [this](SOCKET* sckt)
		{
			return Readable(sckt);
		};


	writableFunc = [this](SOCKET* sckt)
		{
			return Writable(sckt);
		};


	listenThread = std::thread(&Server::ListenLoop, this);
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
		PrintToLog("Failed loading DLL");
		break;
	case SOCKET_CREATE_ERR:
		PrintToLog("Failed to create socket");
		break;
	case SOCKET_BIND_ERR:
		PrintToLog("Failed binding socket");
		break;
	case SOCKET_LISTEN_ERR:
		PrintToLog("Failed listening on socket");
		break;
	case NONE:
		break;
	}

	if (servSocket != INVALID_SOCKET)
	{
		closesocket(servSocket);
	}

	WSACleanup();
}

void Server::PrintToLog(const char* msg)
{
	std::cout << msg << std::endl;
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
		if (connections.size() < MAX_CONNECTIONS && Readable(&servSocket))
		{
			acceptSocket = accept(servSocket, (SOCKADDR*)&acceptInfo, &acceptSize);

			if (acceptSocket == INVALID_SOCKET)
			{
				continue;
			}

			SetNonBlocking(&acceptSocket);

			Connection* newCon = new Connection(acceptSocket, acceptInfo, readableFunc, writableFunc);
			connections.push_back(newCon);
			char logBuf[200];
			sprintf_s(logBuf, "Accepted connection from %s", newCon->ip);
			PrintToLog(logBuf);
		}

		CleanupConnections();
		std::this_thread::sleep_for(std::chrono::microseconds(500));
	}

	ShutdownInternal(ShutdownReason::NONE);
}

void Server::CleanupConnections()
{
	for (int i = 0; i < connections.size(); i++)
	{
		if (connections[i]->pendingDelete)
		{
			delete connections[i];
			connections.erase(connections.begin() + i);
			i = 0;
		}
	}
}

