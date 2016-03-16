#include "GameServer.h"


int main(int argc, const char* argv[])
{
	// 1 argument is expected for server port number
	if (argc != 2)
	{
		fprintf(stderr, "Server port number is expected as argument\n");
		return 0;
	}
	
	GameServer* gameServer = new GameServer(argv[1]);
	
	gameServer->run();
	
	return 0;
}
