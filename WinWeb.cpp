#include <iostream>
#include "Server.h"
#include <chrono>
#include "Common.h"

int main()
{
	Server* newServer = new Server();
	newServer->Init("ANY", 4000);

	std::cout << "WinWeb " << SERVER_MAJOR << "." << SERVER_MINOR << "a, listening for connections..." << std::endl;

	while (newServer->servState != State::SHUTDOWN)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(10));
	}

	delete newServer;
}
