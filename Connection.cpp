#include "Connection.h"

Connection::Connection(SOCKET sckt, sockaddr_in info, std::function<void(SOCKET*, char*)> delegate, std::function<bool(SOCKET*)> readable, std::function<bool(SOCKET*)> writable)
{
	OnRecv = delegate;
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

		while (recvBytes < PACKET_SIZE)
		{
			if (!RecvFromSocket(recvBuf)) 
			{
				//TODO handle keep-alive req
				OnDisconnect();
				return;
			}
			
			OnRecv(&socket, recvBuf);
		}

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
	delete this;
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
}