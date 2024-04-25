#include <iostream>
#include "Server.h"
#include <chrono>

int main()
{
	Server* newServer = new Server();
	newServer->Init("ANY", 80);

	std::cout << "WinWeb 0.2a, listening for connections..." << std::endl;

	while (newServer->servState != State::SHUTDOWN)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(10));
	}
}
