#include <iostream>
#include "Server.h"
#include <chrono>
#include "Common.h"

#define PORT 4000 //Linux Server is using 4000 (Ignore if you're not me)
int main()
{
	Server* newServer = new Server();
	newServer->Init("ANY", PORT); 

	if(newServer->servState != State::SHUTDOWN)
	{
		std::cout << "WinWeb " << SERVER_MAJOR << "." << SERVER_MINOR << "a, listening for connections on port " << PORT <<  "..." << std::endl;

		while (newServer->servState != State::SHUTDOWN)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(10));
		}
	}

	delete newServer;
}

