#include "Server.h"
#include <iostream>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include "Connection.h"

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

	recvFunc = [this](SOCKET* sckt, char* data)
		{
			ProcessRequest(sckt, data);
		};


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
		if (Readable(&servSocket))
		{
			acceptSocket = accept(servSocket, (SOCKADDR*)&acceptInfo, &acceptSize);

			if (acceptSocket == INVALID_SOCKET)
			{
				continue;
			}

			SetNonBlocking(&acceptSocket);

			Connection* newCon = new Connection(acceptSocket, acceptInfo, recvFunc, readableFunc, writableFunc);

			char logBuf[200];
			sprintf_s(logBuf, "Accepted connection from %s", newCon->ip);
			PrintToLog(logBuf);
		}

		std::this_thread::sleep_for(std::chrono::microseconds(500));
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
		char temp = *&data[5];
		if (isspace(temp)) //No site requested, redirect to index
		{
			GetHeader(ResponseCodes::TEMP_REDIRECT, headerBuf, 0, "/index.html");
			SendBuffer(headerBuf, socket);
		}
		else
		{
			char* fileNameEnd = strchr(&data[4], '.');
			char* fileExt = strchr(fileNameEnd, ' ');
			if (fileNameEnd)
			{
				char fileName[150];
				CopyRange(&data[4], fileNameEnd, fileName, 150);
				char ext[150];
				CopyRange(fileNameEnd, fileExt, ext, 150);

				char* file = nullptr;
				int len = 0;
				if (GetFile(fileName, ext, file, len))
				{
					if (file && len > 0) //Don't bother sending an empty file
					{
						char* contentType = GetTypeFromExtension(ext);
						if (contentType)
						{
							GetHeader(ResponseCodes::OK, headerBuf, len, nullptr, contentType);

							int totalSize = 1 + len + strlen(headerBuf);
							char* resp = (char*)malloc(totalSize);
							if (resp)
							{
								memset(resp, 0, totalSize);

								memcpy(&resp[0], headerBuf, strlen(headerBuf));
								memcpy(&resp[strlen(headerBuf)], file, len);

								SendBuffer(resp, socket, totalSize);

								free(resp);
							}

							free(contentType);
						}

						free(file);
					}
					else
					{
						GetHeader(ResponseCodes::NOT_FOUND, headerBuf, 0);
						SendBuffer(headerBuf, socket);
					}
				}
			}
		}
	}
	else
	{
		GetHeader(ResponseCodes::NOT_IMPLEMENTED, headerBuf, 0);
		SendBuffer(headerBuf, socket);
	}
}


void Server::GetHeader(ResponseCodes code, char* buf, int len, const char* loc, char* contentType)
{
	if (!buf)
	{
		return;
	}

	bool cleanup = false;
	char* getContentType = contentType;
	if (!getContentType)
	{
		cleanup = true;
		getContentType = GetTypeFromExtension((char*)".html");
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
	char tempBuf[300];
	memset(tempBuf, 0, 300);
	if (loc)
	{
		sprintf_s(buf, 300, "%s %i \r\nServer:%s/%i.%i\r\nDate:%s, %i %s %i\r\nContent-Type:%s\r\nContent-Length:%i\r\nLocation:%s\r\n\r\n", HTTP_VER, code, SERVER_NAME, SERVER_MAJOR, SERVER_MINOR, dayStr, lTm.tm_mday, monStr, START_YEAR + lTm.tm_year, getContentType, len, loc);
	}
	else
	{
		sprintf_s(buf, 300, "%s %i \r\nServer:%s/%i.%i\r\nDate:%s, %i %s %i\r\nContent-Type:%s\r\nContent-Length:%i\r\n\r\n", HTTP_VER, code, SERVER_NAME, SERVER_MAJOR, SERVER_MINOR, dayStr, lTm.tm_mday, monStr, START_YEAR + lTm.tm_year, getContentType, len);
	}

	tempBuf[strlen(tempBuf)] = 0;
	memcpy(buf, tempBuf, strlen(tempBuf));

	if (cleanup)
	{
		free(getContentType);
	}
}

bool Server::GetFile(char* name, char* ext, char*& retBuf, int& len)
{
	//Finds file in the current directory, dynamically allocates a buffer and then returns true when completed sucessfully
	char nameBuf[200];
	sprintf_s(nameBuf, ".%s%s\0", name, ext);
	std::ifstream file(nameBuf, std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		return false;
	}

	len = file.tellg();
	if (len > MAX_FILE_SIZE)
	{
		return false;
	}

	retBuf = (char*)malloc(len);

	if (!retBuf)
	{
		return false;
	}

	file.seekg(0, std::ios::beg);
	file.read(retBuf, len);
	file.close();
	return true;
}

void Server::SendBuffer(char* buf, SOCKET* dest, int size)
{
	if (!buf)
	{
		return;
	}

	int sendAmount = size;

	if (sendAmount == -1) //If we're sending binary data we can't rely on strlen, so default to it but let us override if when needed
	{
		sendAmount = strlen(buf);
	}

	if (Writable(dest))
	{
		//Not sure if this is required for send?
		int sentBytes = 0;
		while (sentBytes < sendAmount)
		{
			int thisSentBytes = send(*dest, buf, sendAmount, 0);
			if (thisSentBytes == 0) 
			{
				return;
			}
			else
			{
				sentBytes += thisSentBytes;
			}
		}
	}
}

char* Server::GetTypeFromExtension(char* ext)
{
	int max = strlen(ext);
	char* lowerExt = (char*)malloc(max + 1);
	char* it = ext;
	for (int i = 0; i < max; i++)
	{
		char low = (char)tolower(*it);
		memcpy(&lowerExt[i], &low, 1);
		++it;
	}
	
	lowerExt[max] = 0;
	char* retbuf = (char*)malloc(50);
	if (!retbuf)
	{
		return nullptr;
	}

	memset(retbuf, 0, 50);

	//Website content
	if (!strcmp(lowerExt, ".html") || !strcmp(lowerExt, ".htm"))
	{
		strcpy(retbuf, "text/html");
	}
	else if (!strcmp(lowerExt, ".js") || !strcmp(lowerExt, ".mjs"))
	{
		strcpy(retbuf, "application/javascript");
	}
	else if (!strcmp(lowerExt, ".css"))
	{
		strcpy(retbuf, "text/css");
	}

	//Media
	else if (!strcmp(lowerExt, ".jpg") || !strcmp(lowerExt, ".jpeg"))
	{
		strcpy(retbuf, "image/jpg");
	}
	else if (!strcmp(lowerExt, ".png"))
	{
		strcpy(retbuf, "image/png");
	}
	else if (!strcmp(lowerExt, ".webp"))
	{
		strcpy(retbuf, "image/webp");
	}
	else if (!strcmp(lowerExt, ".gif"))
	{
		strcpy(retbuf, "image/gif");
	}
	else if (!strcmp(lowerExt, ".mp3"))
	{
		strcpy(retbuf, "audio/mpeg");
	}
	else if (!strcmp(lowerExt, ".mp4"))
	{
		strcpy(retbuf, "video/mp4");
	}
	else if (!strcmp(lowerExt, ".mpeg"))
	{
		strcpy(retbuf, "video/mpeg");
	}

	//Text/documents
	else if (!strcmp(lowerExt, ".txt"))
	{
		strcpy(retbuf, "text/plain");
	}
	else if (!strcmp(lowerExt, ".doc"))
	{
		strcpy(retbuf, "application/msword");
	}
	else if (!strcmp(lowerExt, ".doc"))
	{
		strcpy(retbuf, "application/msword");
	}

	//Archives
	else if (!strcmp(lowerExt, ".zip"))
	{
		strcpy(retbuf, "application/zip");
	}
	else if (!strcmp(lowerExt, ".7z"))
	{
		strcpy(retbuf, "application/x-7z-compressed");
	}

	//Misc/default
	else
	{
		//Default to binary stream
		strcpy(retbuf, "application/octet-stream");
	}

	free(lowerExt);
	return retbuf;

}

