#include "Server.h"
#include <iostream>
#include <ws2tcpip.h>
#include <fstream>
#include <string>

Server::Server()
{
}

Server::~Server()
{

}

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
		char buf[128];
		buf[127] = 0;
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

	if (ret == SOCKET_ERROR || ret == 0)
	{
		return false;
	}

	return true;
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
		if (!Readable(&servSocket))
		{
			continue;
		}

		acceptSocket = accept(servSocket, (SOCKADDR*)&acceptInfo, &acceptSize);

		if (acceptSocket == INVALID_SOCKET)
		{
			continue;
		}

		SetNonBlocking(&acceptSocket);

		char recvBuf[PACKET_SIZE];
		int recvBytes = 0;
		int retries = 0;
		//TODO improve recv loop, doesn't always get req
		while (recvBytes < PACKET_SIZE && retries < MAX_RETRIES)
		{
			if(Readable(&acceptSocket))
			{
				int thisRecv = recv(acceptSocket, recvBuf, PACKET_SIZE, 0);
				if(thisRecv == 0 || thisRecv == SOCKET_ERROR)
				{
					break;
				}
				recvBytes += thisRecv;
			}
			else
			{
				retries++;
			}
		}

		if (strlen(recvBuf) == 0) //Something went wrong
		{
			closesocket(acceptSocket);
			continue;
		}

		ProcessRequest(&acceptSocket, recvBuf);
	}

	ShutdownInternal(ShutdownReason::NONE);
}

void Server::CopyRange(char* start, char* end, char* buf, int size)
{
	memset(buf, 0, size);
	while (start != end)
	{
		*buf = *start;
		++buf; ++start;
	}
}

void Server::ProcessRequest(SOCKET* socket, char* data)
{
	if (!Writable(socket) || data == nullptr)
	{
		closesocket(*socket);
		return;
	}

	char headerBuf[300];
	GetHeader(ResponseCodes::PROCESSING, headerBuf, 0);
	SendBuffer(headerBuf, socket);

	if (!strncmp(&data[0], "GET", 3))
	{
		if (!strncmp(&data[4], " ", 1)) //No site requested, send index.html
		{

		}
		else
		{
			char* fileNameEnd = strchr(&data[4], '.');
			if (fileNameEnd)
			{
				char fileName[150];
				CopyRange(&data[4], fileNameEnd, fileName, 150);
				char* htmlFile = (char*)malloc(MAX_FILE_SIZE);
				if (htmlFile)
				{
					int len = 0;
					if (GetFile(fileName, htmlFile, len))
					{
						GetHeader(ResponseCodes::OK, headerBuf, len);
						if(len > 0) //Don't bother sending an empty file
						{
							int totalSize = len + strlen(headerBuf);
							char* resp = (char*)malloc(totalSize);
							if(resp)
							{
								memset(resp, 0, totalSize);
								
								memcpy(&resp[0], headerBuf, strlen(headerBuf));
								memcpy(&resp[strlen(headerBuf)], htmlFile, strlen(htmlFile));
												
								SendBuffer(resp, socket);

								free(resp);
							}
						}
					}
					else
					{
						GetHeader(ResponseCodes::NOT_FOUND, headerBuf, 0);
						SendBuffer(headerBuf, socket);
					}
					free(htmlFile);
				}
			}
		}
	}
	else
	{
		//Send NOT_IMPLEMENTED
		GetHeader(ResponseCodes::NOT_IMPLEMENTED, headerBuf, 0);
		SendBuffer(headerBuf, socket);
	}

	closesocket(*socket);
}

void Server::GetHeader(ResponseCodes code, char* buf, int len)
{
	if (!buf) 
	{
		return;
	}

	struct tm lTm;
	time_t now = time(0);
	localtime_s(&lTm, &now);

	char timeBuf[200];

	ctime_s(&timeBuf[0], 200, &now);

	char dayStr[10];
	memset(dayStr, 0, 3);
	strncpy_s(dayStr, &timeBuf[0], 3);

	char monStr[10];
	memset(monStr, 0, 3);
	char* mon = &timeBuf[4];
	strncpy_s(monStr, &timeBuf[4], 3);

	//TODO clean this up
	char tempBuf[200];
	memset(tempBuf, 0, 200);
	sprintf_s(buf, 200, "%s %i \r\nServer:%s/%i.%i\r\nDate:%s, %i %s %i\r\nContent-Type:%s\r\nContent-Length:%i\r\n\r\n", HTTP_VER, code, SERVER_NAME, SERVER_MAJOR, SERVER_MINOR, dayStr, lTm.tm_mday, monStr, START_YEAR + lTm.tm_year, CONTENT_TYPE, len);
	memcpy(buf, tempBuf, strlen(tempBuf));
}

bool Server::GetFile(char* name, char* retBuf, int &len)
{
	char nameBuf[200];
	sprintf_s(nameBuf, ".%s.html", name);
	std::ifstream htmlFile(nameBuf);
	if (!htmlFile.is_open())
	{
		return false;
	}

	std::string wholeFile;
	std::string line;
	while (std::getline(htmlFile, line))
	{
		wholeFile += line;
	}

	len = wholeFile.size();
	memcpy(retBuf, wholeFile.c_str(), len);
	retBuf[len] = 0;
	htmlFile.close();
	return true;
}

void Server::SendBuffer(char* buf, SOCKET* dest)
{
	if(!buf)
	{
		return;
	}

	int sentBytes = 0;
	while (sentBytes < strlen(buf))
	{
		int thisSent = send(*dest, buf, strlen(buf), 0);
		if (thisSent == SOCKET_ERROR)
		{
			break;
		}
		sentBytes += thisSent;
	}
}

