#include <iostream>
#include "Server.h"
#include <chrono>

int main()
{
	Server* newServer = new Server();
	newServer->Init("ANY", 8888);

	while (newServer->servState != State::SHUTDOWN)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(10));
	}
}
