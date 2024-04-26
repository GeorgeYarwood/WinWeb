#include "Connection.h"
#include <iostream>
#include <fstream>

Connection::Connection(SOCKET sckt, sockaddr_in info, std::function<bool(SOCKET*)> readable, std::function<bool(SOCKET*)> writable)
{
	Readable = readable;
	Writable = writable;
	socket = sckt;
	Info = info;

	inet_ntop(AF_INET, &info.sin_addr, ip, INET_ADDRSTRLEN);

	conThread = std::thread([&] {this->RunConnection(); });
	conThread.detach();
}

Connection::~Connection()
{
}

void Connection::RunConnection()
{
	while (connected)
	{
		char recvBuf[PACKET_SIZE];
		int recvBytes = 0;
		int retries = 0;
		bool failed = false;

		if (!RecvFromSocket(recvBuf))
		{
			//TODO handle keep-alive req
			OnDisconnect();
			return;
		}
		ProcessRequest(&socket, recvBuf);
		std::this_thread::sleep_for(std::chrono::microseconds(500));
	}
}

void Connection::OnDisconnect()
{
	connected = false;
	std::this_thread::sleep_for(std::chrono::microseconds(50000));
	if (socket != INVALID_SOCKET)
	{
		closesocket(socket);
	}

	pendingDelete = true;
}

bool Connection::RecvFromSocket(char* buf)
{
	if (!buf || !Readable(&socket))
	{
		return false;
	}

	int recvBytes = 0;
	int retries = 0;
	while (recvBytes < PACKET_SIZE && retries < MAX_RETRIES)
	{
		int thisRecv = recv(socket, buf, PACKET_SIZE, 0);
		if (thisRecv == 0)
		{
			return false;
		}
		else if (thisRecv != SOCKET_ERROR)
		{
			recvBytes += thisRecv;
		}
		else
		{
			++retries;
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}

	return recvBytes < PACKET_SIZE ? false : true;
}

void Connection::CopyRange(char* start, char* end, char* buf, int size)
{
	memset(buf, 0, size);
	while (start != end)
	{
		*buf = *start;
		++buf; ++start;
	}
}

void Connection::ProcessRequest(SOCKET* socket, char* data)
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
			char* fileNameEnd = strchr(&data[5], '.');
			char* fileExt = strchr(fileNameEnd, ' ');
			if (fileNameEnd)
			{
				char fileName[150];
				CopyRange(&data[5], fileNameEnd, fileName, 150);
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

void Connection::GetHeader(ResponseCodes code, char* buf, int len, const char* loc, char* contentType)
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

bool Connection::GetFile(char* name, char* ext, char*& retBuf, int& len)
{
	//Finds file in the current directory, dynamically allocates a buffer and then returns true when completed sucessfully
	char nameBuf[200];
	sprintf_s(nameBuf, ".\\%s%s\0", name, ext);

	char* it = &nameBuf[0];
	int max = strlen(nameBuf);
	for(int i = 0; i < max - 2; i++)
	{
		char* first = it + 1;
		char* second = first + 1;

		if(!strncmp(it, "%", 1))
		{
			memcpy(it, second, max - i);
			memset(it, ' ', 1);
		}

		++it;
	}

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

void Connection::SendBuffer(char* buf, SOCKET* dest, int size)
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
		send(*dest, buf, sendAmount, 0);
	}
}

char* Connection::GetTypeFromExtension(char* ext)
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
