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
	lastRecv = std::chrono::steady_clock::now();

	conThread = std::thread([&] {this->RunConnection(); });
	conThread.detach();
}

Connection::~Connection()
{
}

void Connection::RunConnection()
{
	recvBuf = (char*)malloc(MAX_PACKET_SIZE);
	if (recvBuf)
	{
		while (connected)
		{
			memset(&recvBuf[0], 0, MAX_PACKET_SIZE);

			if (!RecvFromSocket(recvBuf))
			{
				bool kill = true;
				if (keepAlive)
				{
					auto currTime = std::chrono::steady_clock::now();
					auto timeDiff = std::chrono::duration_cast<std::chrono::microseconds>(currTime - lastRecv).count() / TO_SECONDS;
					if (timeDiff < KEEP_ALIVE_TIMEOUT)
					{
						kill = false;
					}
				}

				if (kill)
				{
					break;
				}
			}
			else
			{
				ProcessRequest(&socket, recvBuf);
				if (keepAlive)
				{
					lastRecv = std::chrono::steady_clock::now();
				}
			}
			std::this_thread::sleep_for(std::chrono::microseconds(500));
		}
	}


	OnDisconnect();
}

void Connection::OnDisconnect()
{
	connected = false;
	std::this_thread::sleep_for(std::chrono::microseconds(50000));

	if (recvBuf)
	{
		free(recvBuf);
	}

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
	int thisRecv = 0;

	while (thisRecv != SOCKET_ERROR)
	{
		thisRecv = recv(socket, buf, MAX_PACKET_SIZE, 0);
		if (thisRecv == 0)
		{
			break;
		}
		else if (thisRecv != SOCKET_ERROR)
		{
			recvBytes += thisRecv;
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}

	return recvBytes <= 0 ? false : true;
}

void Connection::CopyRange(char* start, char* end, char* buf, int size)
{
	memset(buf, 0, size);
	int written = 0;
	while (start != end && written < size)
	{
		*buf = *start;
		++buf; ++start;

		written++;
	}
}

int Connection::GetStrLen(char* start, char* end)
{
	int len = 0;
	char* it = start;
	while (it != end && it && end)
	{
		++it;

		len++;
	}

	return len;
}

void Connection::ProcessRequest(SOCKET* socket, char* data)
{
	if (!Writable(socket) || !data)
	{
		return;
	}

	char headerBuf[MAX_HEADER_BUF_SIZE];

	//Get a ptr to start of each parameter
	char* params[MAX_PARAMS];
	int index = 1;

	char* lst = &data[0];
	params[0] = lst;
	while (index < MAX_PARAMS)
	{
		char* nxt = strchr(lst, '\r\n');
		if (nxt)
		{
			nxt += 2;
			params[index] = nxt;
			lst = nxt;
		}
		else
		{
			break;
		}
		++index;
	}

	char* userAgent = nullptr;

	//Need to get the connection type first
	for (int i = 0; i < index; i++)
	{
		if (!strncmp(params[i], "Connection", 10))
		{
			char* req = params[i] + 11;
			if (req)
			{
				++req;
				if (!strncmp(req, "keep-alive", 10))
				{
					keepAlive = true;
				}
				else
				{
					keepAlive = false;
				}
			}
		}
		else if (!strncmp(params[i], "User-Agent", 10))
		{
			//This should be sent back to the client
			char* start = params[i] + 11;
			char* end = strchr(start, '\r\n');

			if (!start || !end)
			{
				continue;
			}

			int allocSize = GetStrLen(start, end) + 1;

			userAgent = (char*)malloc(allocSize);
			if (userAgent)
			{
				CopyRange(start, end, userAgent, allocSize - 1);
				userAgent[allocSize - 1] = 0;
			}
		}
	}

	//GetHeader(ResponseCodes::PROCESSING, userAgent, headerBuf, 0, "");
	//SendBuffer(headerBuf, socket);

	bool handled = false;

	for (int i = 0; i < index; i++)
	{
		if (!strncmp(params[i], "GET", 3))
		{
			char temp = *&data[5];
			if (isspace(temp)) //No site requested, redirect to index
			{
				GetHeader(ResponseCodes::TEMP_REDIRECT, userAgent, headerBuf, 0, "/index.html");
				handled = SendBuffer(headerBuf, socket);
			}
			else
			{
				char* httpVer = strchr(&data[5], 'H');
				char* fileNameEnd = strchr(&data[5], '.');
				char* fileExt = strchr(fileNameEnd, ' ');
				if (fileNameEnd && fileNameEnd < httpVer)
				{
					char fileName[MAX_FILE_NAME_LEN];
					CopyRange(&data[5], fileNameEnd, fileName, MAX_FILE_NAME_LEN);
					char ext[MAX_FILE_NAME_LEN];
					CopyRange(fileNameEnd, fileExt, ext, MAX_FILE_NAME_LEN);

					char* file = nullptr;
					int len = 0;
					if (GetFile(fileName, ext, file, len))
					{
						if (file && len > 0) //Don't bother sending an empty file
						{
							char* contentType = GetTypeFromExtension(ext);
							if (contentType)
							{
								GetHeader(ResponseCodes::OK, userAgent, headerBuf, len, nullptr, contentType);
								int totalSize = 0;
								char* resp = AppendDataToHeader(headerBuf, file, len, totalSize);

								if (resp)
								{
									handled = SendBuffer(resp, socket, totalSize);
									free(resp);
								}

								free(contentType);
							}

							free(file);
						}
						else
						{
							goto NotFound;
						}
					}
					else
					{
					NotFound:
						GetHeader(ResponseCodes::NOT_FOUND, userAgent, headerBuf, 0, "");
						handled = SendBuffer(headerBuf, socket);
					}
				}
				else
				{
					//No file ext, we've requested a directory listing
					char filePath[MAX_PATH];
					CopyRange(&data[5], httpVer - 1, filePath, MAX_PATH);
					char* retBuf = nullptr;
					if (GetDirectoryListing(filePath, retBuf) && retBuf)
					{
						int len = strnlen_s(retBuf, MAX_DIR_BUF_SIZE);
						char* contentType = GetTypeFromExtension((char*)".html");
						if (contentType)
						{
							GetHeader(ResponseCodes::OK, userAgent, headerBuf, len, nullptr, contentType);
							int totalSize = 0;
							char* resp = AppendDataToHeader(headerBuf, retBuf, len, totalSize);
							if (resp)
							{
								handled = SendBuffer(resp, socket, totalSize);
								free(resp);
							}

							free(contentType);
						}
					}
				}
			}
		}

		if (handled)
		{
			break;
		}
	}

	if (!handled)
	{
		GetHeader(ResponseCodes::NOT_IMPLEMENTED, userAgent, headerBuf, 0, "");
		SendBuffer(headerBuf, socket);
	}

	free(userAgent);
}

char* Connection::AppendDataToHeader(char* headerBuf, char* data, int dataLen, int& totalSize)
{
	totalSize = 1 + dataLen + strlen(headerBuf);
	char* resp = (char*)malloc(totalSize);
	if (resp)
	{
		memset(resp, 0, totalSize);

		memcpy(&resp[0], headerBuf, strlen(headerBuf));
		memcpy(&resp[strlen(headerBuf)], data, dataLen);
	}

	return resp;
}

void Connection::GetHeader(ResponseCodes code, char* userAgent, char* buf, int len, const char* loc, char* contentType)
{
	if (!buf)
	{
		return;
	}

	memset(buf, 0, MAX_HEADER_BUF_SIZE);

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

	char keepAliveBuf[200];
	sprintf(keepAliveBuf, "keep-alive\r\nKeep-Alive: timeout=%i, max=%i", KEEP_ALIVE_TIMEOUT, MAX_KEEP_ALIVE_REQS);
	const char* closeStr = "close";

	sprintf_s(buf, MAX_HEADER_BUF_SIZE, "%s %i \r\nConnection:%s\r\nServer:%s/%i.%i\r\nDate:%s, %i %s %i\r\nContent-Type:%s\r\nContent-Length:%i\r\nLocation:%s\r\nUser-Agent:%s\r\n\r\n", HTTP_VER, code,
		keepAlive ? keepAliveBuf : closeStr, SERVER_NAME, SERVER_MAJOR, SERVER_MINOR, dayStr, lTm.tm_mday, monStr, START_YEAR + lTm.tm_year, getContentType, len, loc, userAgent);

	buf[strlen(buf)] = 0;

	if (cleanup)
	{
		free(getContentType);
	}
}

bool Connection::GetDirectoryListing(char* loc, char*& retBuf)
{
	//Get list of all files/folders in this directory
	//Generate HTML table with hyperlink to each file/folder
	//Return as response

	for (int i = 0; i < strlen(loc); i++)
	{
		if (loc[i] == '%')
		{
			memset(&loc[i], ' ', 3);
			//Shunt up twice to fill gap
			for (int j = 0; j < 2; j++)
			{
				for (int k = i; k < strlen(loc); k++)
				{
					loc[k] = loc[k + 1];
				}
			}
		}
	}

	char basePath[MAX_PATH];
	strcpy(basePath, loc);

	strcat(loc, "\\*\0");
	WIN32_FIND_DATAA data;
	HANDLE hFind = FindFirstFileA(loc, &data);

	retBuf = (char*)malloc(MAX_DIR_BUF_SIZE);

	if (!retBuf)
	{
		return false;
	}

	char tblBuf[MAX_DIR_TABLE_SIZE];
	tblBuf[0] = 0;

	if (hFind != INVALID_HANDLE_VALUE)
	{
		while (FindNextFileA(hFind, &data) && strlen(tblBuf) < MAX_DIR_TABLE_SIZE)
		{
			bool isParentDir = false;
			char parentPath[MAX_PATH];

			if (!strcmp(data.cFileName, "\.."))
			{
				char* lst = strchr(&basePath[0], '/');
				char* lstGood = nullptr;

				while(lst)
				{
					lstGood = lst;
					char* nxt = lstGood + 1;
					if (nxt) 
					{
						lst = strchr(lstGood + 1, '/');
					}
					else
					{
						break;
					}
				}

				if(lstGood)
				{
					CopyRange(&basePath[0], lstGood, parentPath, MAX_PATH);
					isParentDir = true;
				}
			}

			if(basePath[strlen(basePath) - 1] != '/')
			{
				strcat(basePath, "/");
			}

			FILETIME time = data.ftLastWriteTime;
			SYSTEMTIME sysTime;
			FileTimeToSystemTime(&time, &sysTime);

			char fileInfo[200];
			sprintf_s(fileInfo, "%u-%u-%u %u:%u", sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute);

			char entryBuf[400 + (MAX_PATH * 2)];
			sprintf_s(entryBuf, "<tr><td valign=\"top\">&nbsp;</td><td><a href=\"/%s%s\">%s</a></td><td align=\"right\">%s  </td><td align=\"right\">  </td><td>&nbsp;</td></tr>\n", isParentDir ? parentPath : basePath,
				isParentDir ? "" : data.cFileName, data.cFileName, fileInfo);
			strcat(tblBuf, entryBuf);
		}

		FindClose(hFind);
	}

	sprintf_s(retBuf, MAX_DIR_BUF_SIZE, "<!DOCTYPE HTML - WinWeb auto-generated directory listing>\n<html>\n<head>\n<title>Index of %s</title>\n<head>\n<body>\n<h1>Index of %s</h1>\n<table>%s</table>\n</body>\n</html>", basePath, basePath, tblBuf);
	return true;
}

bool Connection::GetFile(char* name, char* ext, char*& retBuf, int& len)
{
	//Finds file in the current directory, dynamically allocates a buffer and then returns true when completed sucessfully
	if (strnlen_s(name, MAX_FILE_NAME_LEN) + strnlen_s(ext, MAX_FILE_NAME_LEN) > MAX_FILE_NAME_LEN)
	{
		return false;
	}

	char nameBuf[MAX_FILE_NAME_LEN];
	sprintf_s(nameBuf, ".\\%s%s\0", name, ext);

	char* it = &nameBuf[0];
	int max = strlen(nameBuf);
	for (int i = 0; i < max - 2; i++)
	{
		char* first = it + 1;
		char* second = first + 1;

		if (!strncmp(it, "%", 1))
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

bool Connection::SendBuffer(char* buf, SOCKET* dest, int size)
{
	if (!buf)
	{
		return false;
	}

	int sendAmount = size;

	if (sendAmount == -1) //If we're sending binary data we can't rely on strlen, so default to it but let us override if when needed
	{
		sendAmount = strlen(buf);
	}

	int sentBytes = 0;
	int retryCount = 0;
	char* pos = &buf[0];
	if (Writable(dest))
	{
		while (sentBytes < sendAmount)
		{
			int thisSent = send(*dest, pos, sendAmount - sentBytes, 0);

			if (thisSent == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
			{
				std::cout << WSAGetLastError();
				break;
			}
			else if (thisSent > 0)
			{
				sentBytes += thisSent;
				//Only seems to happen on Linux, where send will not send all bytes and we must 
				//manually iterate along the buffer and calculate what remains to be sent
				pos += thisSent;
			}

			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}

	return true;
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
