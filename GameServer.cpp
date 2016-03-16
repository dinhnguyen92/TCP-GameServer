#include "GameServer.h"


addrinfo* GameServer::getTCPServerAddrInfo(const char* portNum)
{
	// Written based on "socket-tutorial" by GauthierDickey
	
	// Set hints for creating server address info
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	
	// Create the server address info
	struct addrinfo* serverAddrInfo;
	
	int result = getaddrinfo(0, portNum, &hints, &serverAddrInfo);
	
	if (result != 0)
	{
		fprintf(stderr, "Error resolving port %s: %s\n", portNum, gai_strerror(result));
		return NULL;
	}
	
	return serverAddrInfo;
}


int GameServer::createSocketFD(struct addrinfo* hostAddr)
{
	// Written based on "socket-tutorial" by GauthierDickey
	
	int sockfd;
	struct addrinfo* p;
	
	// Iterate through the list of host address info
	// Create and bind the socket to the first one that workds
	for (p = hostAddr; p != NULL; p = p->ai_next)
	{
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		
		if (sockfd == - 1)
		{
			perror("Unable to create socket ");
			continue;
		}
		
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			perror("Unable to bind socket ");
			continue;
		}
		
		break;
	}
	
	if (p == NULL)
	{
		fprintf(stderr, "Failed to bind socket to a valid server address.\n");
		return -1;
	}
	
	return sockfd;
}


int GameServer::setSocketNonBlocking(int sockfd)
{
	// Written based on "socket-tutorial" by GauthierDickey
	
	// Get current flags of the socket
	int flags = fcntl(sockfd, F_GETFL);
	
	if (flags == -1)
	{
		perror("Failed to get socket flags using fcntl() ");
		return flags;
	}
	
	// Set socket flag to non-blocking
	int res = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	
	if (res == -1)
	{
		perror("Failed to set socket to non-blocking using fcntl() ");
	}
	
	return res;
}


int GameServer::setSocketListen(int sockfd, int backlog)
{
	// Written based on "socket-tutorial" by GauthierDickey
	
	int res = listen(sockfd, backlog);
	
	if (res == - 1)
	{
		fprintf(stderr, "Error setting socket as listening: %s\n", strerror(errno));
		return -1;
	}
	
	return res;
}


TCPHost* GameServer::createTCPServer(const char* portNum, int backlog)
{
	struct addrinfo* serverAddr = getTCPServerAddrInfo(portNum);
	
	if (serverAddr == NULL) return NULL;
	
	int sockfd = createSocketFD(serverAddr);
	
	if (sockfd == -1) return NULL;
	
	int res = setSocketNonBlocking(sockfd);
	
	if (res == -1) return NULL;
	
	res = setSocketListen(sockfd, backlog);
	
	if (res == -1) return NULL;
	
	// Create the TCPHost 
	TCPHost* host = (TCPHost*)malloc(sizeof(TCPHost));
	
	if (host == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for TCP host.\n");
		return NULL;
	}
	
	memset(host, 0, sizeof(TCPHost));
	
	host->sockfd = sockfd;
	host->hostName = 0; // Set host name to null since this machine is the host
	host->portNum = portNum;
	memcpy(&host->info, serverAddr, sizeof(struct addrinfo));
	memcpy(&host->addr, serverAddr->ai_addr, sizeof(struct sockaddr));
	host->addrlen = serverAddr->ai_addrlen;
	
	return host;
}


GameServer::GameServer(const char* portNum)
{
	server = createTCPServer(portNum, 5);
	
	if (server == NULL)
	{
		fprintf(stderr, "ERROR: game server not created\n");
		exit(EXIT_FAILURE);
	}
	
	// Since the server socket is the first socket
	// It is also the maximum socket fd
	maxfd = server->sockfd;
	
	numActiveSockets = 0;
	numAlivePlayers = 0;

	timeout.tv_sec = 0;
	timeout.tv_usec = 500;
	
	// Create players
	// Initialize the players' sockets to zero (empty)
	for (int i = 0; i < PLAYER_LIMIT; i++)
	{
		players[0].sockfd = 0;
	}
	
	fprintf(stdout, "Game server created at port %s\n", portNum);
}


GameServer::~GameServer()
{
	if (server != NULL)
	{
		close(server->sockfd);
		delete server;
	} 
	
	for (int32_t i = 0; i < PLAYER_LIMIT; i++)
	{
		if (players[i].sockfd != 0)
		{
			shutdown(players[i].sockfd, SHUT_RDWR);
		}
	}
}


void GameServer::run()
{
	fprintf(stdout, "Game server started\n");
	
	double start = clock();
	
	while (true)
	{
		// Reset the file descriptor master set
		// The master set is used to keep track of active sockets
		// Once all active sockets are added to the master set
		// The master set is copied to all other fd sets
		FD_ZERO(&masterSet);
		
		// Add all active sockets (server and active players) to the master set
		FD_SET(server->sockfd, &masterSet);
		for (int32_t i = 0; i < PLAYER_LIMIT; i++)
		{
			// If a player is active
			if (players[i].sockfd != 0)
			{
				FD_SET(players[i].sockfd, &masterSet);
			}
		}
		
		// Copy the master set to other fd sets
		readSet = masterSet;
		writeSet = masterSet;
		exceptSet = masterSet;
		
		// Use select to wait for socket activity
		int res = select(maxfd + 1, &readSet, &writeSet, &exceptSet, &timeout);
		
		// If there's an error
		if (res == -1)
		{
			fprintf(stderr, "Error waiting for socket activity: %s\n", strerror(errno));
			continue;
		}
		
		// If a client attempts to connect
		if (FD_ISSET(server->sockfd, &readSet))
		{
			int id = acceptNewPlayer();
			
			// if there's an error accepting the player, retry 3 times
			int count = 3;
			while (id == -1 && count > 0)
			{
				id = acceptNewPlayer();
				count--;
			}

			// If the player has been accepted
			if (id >= 0)
			{
				numAlivePlayers++;
				numActiveSockets++;
				
				int res = sendJoinResponse(id);
				
				// If there's an error sending join response, retry 3 times
				count = 3;
				while (res == -1 && count > 0)
				{
					res = sendJoinResponse(id);
					count--;
				}
				
				// For a real project, there should be more sophisticated error-handling
				// if the join response does not go through, or if there's problem
				// establish the connection between server and client
				// of if the socket's not ready to be written to
			}
		}	
		if (FD_ISSET(server->sockfd, &writeSet))
		{
			// No game logic for now
			// No error handling for now
		}
		if (FD_ISSET(server->sockfd, &exceptSet))
		{
			// No game logic for now
			// No error handling for now
		}
		
		// Check for socket activities in each player sockets
		for (int32_t i = 0; i < PLAYER_LIMIT; i++)
		{
			// Do nothing if the player socket is inactive
			if (players[i].sockfd == 0) continue;
			
			// If a message is received from a player
			if (FD_ISSET(players[i].sockfd, &readSet))
			{
				int code = processPlayerMessage(i);
				
				if (code == -1)
				{
					fprintf(stderr, "Error processing message from player %d\n", i);
				}	
			}	
			if (FD_ISSET(players[i].sockfd, &writeSet))
			{
				// No game logic for now
			}
			if (FD_ISSET(players[i].sockfd, &exceptSet))
			{
				// No game logic for now
				// No error handling for now
			}		
		}
		
		float millisec = ((clock() - start)/(double)CLOCKS_PER_SEC) * 1000;
			
		// If it's time to send map update and there are players still alive in map
		if (millisec >= MAP_UPDATE_MILLISEC && numAlivePlayers > 0)
		{
			//fprintf(stdout, "Update map\n");
			broadcastMapUpdate();
				
			// Reset the timing to send map udate
			start = clock();
				
			// broadcastMapUpdate returns the number of messages sent to players
			// If the number of messages is less than the number of active players
			// more sophisticated error handling will be needed to handle this error
		}
	}
}


/*
 * Game server utility functions 
 */
 
int GameServer::sendJoinResponse(int32_t playerID)
{
	int32_t convertedID = htonl(playerID);
	
	uint32_t numBytes = 10;
	uint32_t convertedBytes = htonl(numBytes);
	
	// Load the message into the buffer
	players[playerID].sendBuffer[0] = GET_BYTE_3(convertedBytes);
	players[playerID].sendBuffer[1] = GET_BYTE_2(convertedBytes);
	players[playerID].sendBuffer[2] = GET_BYTE_1(convertedBytes);
	players[playerID].sendBuffer[3] = GET_BYTE_0(convertedBytes);
	players[playerID].sendBuffer[4] = VERSION_NUM; 				// first byte: version num
	players[playerID].sendBuffer[5] = PLAYER_JOIN_RESPONSE; 	// second byte: message code
	players[playerID].sendBuffer[6] = GET_BYTE_3(convertedID); 	// third byte: byte 3 of ID
	players[playerID].sendBuffer[7] = GET_BYTE_2(convertedID); 	// 4th byte: byte 2 of ID
	players[playerID].sendBuffer[8] = GET_BYTE_1(convertedID); 	// 5th byte: byte 1 of ID
	players[playerID].sendBuffer[9] = GET_BYTE_0(convertedID);	// 6th byte: byte 0  of ID
	
	ssize_t bytes = -1;

	bytes = send(players[playerID].sockfd, players[playerID].sendBuffer, (int)numBytes, 0);
	
	if (bytes == -1)
	{
		fprintf(stderr, "Error sending join response: %s\n", strerror(errno));
		return -1;
	}
	if (bytes < numBytes)
	{
		fprintf(stderr, "Wrong number of bytes sent in join response: %ld bytes\n", bytes);
		return -1;
	}
	
	return 0;
}
 
 
int32_t GameServer::acceptNewPlayer()
{
	int32_t i;
				
	// Find the first available player slot
	for (i = 0; i < PLAYER_LIMIT; i++)
	{
		// If the player slot is available
		if (players[i].sockfd == 0)
		{
			break;
		}
	}		
	
	// If no available slot is found
	if (players[i].sockfd != 0)
	{
		fprintf(stdout, "No available player slot. Cannot accept new player.\n");
		return -2;
	}
				
	// Complete the TCP connection
	int sockfd = accept(server->sockfd, &players[i].addr, &players[i].addrlen);
						
	if (sockfd == -1)
	{
		fprintf(stderr, "Failed to accept new player: %s\n", strerror(errno));
		return -1;
	}
	else
	{			
		fprintf(stdout, "New player with ID %d created\n", i);
		// Initialize the player
		players[i].sockfd = sockfd;
		players[i].score = 0;	
		players[i].isAlive = false;	

		// Update the max file descriptor
		maxfd = (sockfd > maxfd) ? sockfd : maxfd;	
	}
	
	return i;
}


int GameServer::processPlayerMessage(int32_t playerID)
{
	ssize_t bytes = recv(players[playerID].sockfd, players[playerID].recvBuffer, BUFFER_SIZE, 0); 
	
	if (bytes == -1)
	{
		fprintf(stderr, "Error receiving player message: %s\n", strerror(errno));
		return -1;
	}
	if (bytes == 0)
	{
		return 0;
	}
	
	// Read the number of bytes in the packet
	uint32_t rawBytes = 0;
	rawBytes |= ((uint32_t)players[playerID].recvBuffer[0]) << 24;
	rawBytes |= ((uint32_t)players[playerID].recvBuffer[1]) << 16;
	rawBytes |= ((uint32_t)players[playerID].recvBuffer[2]) << 8;
	rawBytes |= ((uint32_t)players[playerID].recvBuffer[3]);
	
	uint32_t numBytes = ntohl(rawBytes);
	
	// If the number of bytes actually received is less than the expected number of bytes
	if (bytes < numBytes)
	{
		fprintf(stderr, "Number of bytes received from server is less than expected. Received %ld bytes, expected %u bytes\n", bytes, numBytes);
		
		for (int i = 0; i < bytes; i++)
		{
			fprintf(stdout, "byte %d: %d\n", i, server->recvBuffer[i]);
		}
		
		return -1;
	}
	// Check the version number
	if (players[playerID].recvBuffer[4] != VERSION_NUM)
	{
		fprintf(stderr, "Wrong version number in player message\n");
		return -1;
	}
	
	int res = 0;
	
	// Check the message code
	switch(players[playerID].recvBuffer[5])
	{
		case PLAYER_MOVE:
		{
			// 18 bytes are expected for player move message
			if (numBytes != 18)
			{
				fprintf(stderr, "Wrong number of bytes received in player move message: %u\n", numBytes);
				for (int i = 0; i < (int)numBytes; i++)
				{
					fprintf(stdout, "Byte %d: %d\n", i, server->recvBuffer[i]);
				}
				res = -1;
			}
			else
			{
				uint32_t temp = 0;
				
				// Read the player's x coordinate
				temp |= players[playerID].recvBuffer[6] << 24; 	// byte 3 of x value
				temp |= players[playerID].recvBuffer[7] << 16; 	// byte 2 of x value
				temp |= players[playerID].recvBuffer[8] << 8; 	// byte 1 of x value
				temp |= players[playerID].recvBuffer[9]; 		// byte 0 of x value
				uint32_t binaryX = ntohl(temp);
				memcpy(&players[playerID].x, &binaryX, sizeof(float));
				
				temp = 0;
				
				// Read the player's y coordinate
				temp |= players[playerID].recvBuffer[10] << 24; 	// byte 3 of y value
				temp |= players[playerID].recvBuffer[11] << 16; 	// byte 2 of y value
				temp |= players[playerID].recvBuffer[12] << 8; 	// byte 1 of y value
				temp |= players[playerID].recvBuffer[13]; 		// byte 0 of y value
				uint32_t binaryY = ntohl(temp);
				memcpy(&players[playerID].y, &binaryY, sizeof(float));
				
				temp = 0;
				
				// Read the player's y coordinate
				temp |= players[playerID].recvBuffer[14] << 24; // byte 3 of y value
				temp |= players[playerID].recvBuffer[15] << 16; // byte 2 of y value
				temp |= players[playerID].recvBuffer[16] << 8; 	// byte 1 of y value
				temp |= players[playerID].recvBuffer[17]; 		// byte 0 of y value
				uint32_t binaryZ = ntohl(temp);
				memcpy(&players[playerID].z, &binaryZ, sizeof(float));
				
				fprintf(stdout, "Player %d moves to {%.2f, %.2f, %.2f}\n", playerID, players[playerID].x, players[playerID].y, players[playerID].z);
			}	
			break;
		}		
		case PLAYER_SELF_ANNIHILATE:
		{
			// 6 bytes are expected for player self annihilate message
			if (numBytes != 6)
			{
				fprintf(stderr, "Wrong number of bytes received in player self annihilate message: %u\n", numBytes);
				for (int i = 0; i < (int)numBytes; i++)
				{
					fprintf(stdout, "Byte %d: %d\n", i, server->recvBuffer[i]);
				}
				res = -1;
			}
			else
			{
				fprintf(stdout, "Player %d self-annihilated\n", playerID);
						
				// Set the player to "dead"
				players[playerID].isAlive = false;
				numAlivePlayers--;
				
				int32_t* killedPlayers = new int32_t[PLAYER_LIMIT - 1];
				
				// Simulate the result of the player's self destruction
				int numKills = simRecursiveExplosion(playerID, killedPlayers, 0);
				
				fprintf(stdout, "%d player(s) killed\n", numKills);
				
				for (int j = 0; j < numKills; j++)
				{
					fprintf(stdout, "Player %d killed\n", killedPlayers[j]);
				}
				
				// Update the player's score
				players[playerID].score += numKills;
				
				// Broadcast the self destruction to all players
				broadcastSelfDestruct(playerID, numKills, killedPlayers);

				delete[] killedPlayers;
						
				// broadcastSelfDestruct returns the number of messages sent to players
				// If the number of messages is less than the number of active players
				// more sophisticated error handling will be needed to handle this error
			}	
			break;
		}		
		case PLAYER_SPAWN:
		{
			// 18 bytes are expected for player spawn message
			if (numBytes != 18)
			{
				fprintf(stderr, "Wrong number of bytes received in player spawn message: %u\n", numBytes);
				for (int i = 0; i < (int)numBytes; i++)
				{
					fprintf(stdout, "Byte %d: %d\n", i, server->recvBuffer[i]);
				}
				res = -1;
			}
			else
			{
				uint32_t temp = 0;
				
				// Read the player's x coordinate
				temp |= players[playerID].recvBuffer[6] << 24; 	// byte 3 of x value
				temp |= players[playerID].recvBuffer[7] << 16; 	// byte 2 of x value
				temp |= players[playerID].recvBuffer[8] << 8; 	// byte 1 of x value
				temp |= players[playerID].recvBuffer[9]; 		// byte 0 of x value
				uint32_t binaryX = ntohl(temp);
				memcpy(&players[playerID].x, &binaryX, sizeof(float));
				
				temp = 0;
				
				// Read the player's y coordinate
				temp |= players[playerID].recvBuffer[10] << 24; 	// byte 3 of y value
				temp |= players[playerID].recvBuffer[11] << 16; 	// byte 2 of y value
				temp |= players[playerID].recvBuffer[12] << 8; 	// byte 1 of y value
				temp |= players[playerID].recvBuffer[13]; 		// byte 0 of y value
				uint32_t binaryY = ntohl(temp);
				memcpy(&players[playerID].y, &binaryY, sizeof(float));
				
				temp = 0;
				
				// Read the player's y coordinate
				temp |= players[playerID].recvBuffer[14] << 24; // byte 3 of y value
				temp |= players[playerID].recvBuffer[15] << 16; // byte 2 of y value
				temp |= players[playerID].recvBuffer[16] << 8; 	// byte 1 of y value
				temp |= players[playerID].recvBuffer[17]; 		// byte 0 of y value
				uint32_t binaryZ = ntohl(temp);
				memcpy(&players[playerID].z, &binaryZ, sizeof(float));
				
				// Set the player as alive
				players[playerID].isAlive = true;
				
				fprintf(stdout, "Player %d spawned at {%.2f, %.2f, %.2f}\n", playerID, players[playerID].x, players[playerID].y, players[playerID].z);
				
				broadcastNewSpawn(playerID);
						
				// broadcastNewSpawn returns the number of messages sent to players
				// If the number of messages is less than the number of active players
				// more sophisticated error handling will be needed to handle this error					
			}			
			break;		
		}	
		default:
		{
			fprintf(stderr, "Wrong message code in player message\n");
			res = -1;
			break;
		}		
	}
	
	return res;
}


int GameServer::simRecursiveExplosion(int32_t playerID, int32_t* killedPlayers, int index)
{
	int numKills = 0;
	
	// Iterate through each player
	// and check if they are killed by the explosion
	// Note: the exploding player will not be added to the kill list
	for (int i = 0; i < PLAYER_LIMIT; i++)
	{
		// If player is not the one exploded
		// Player is valid (sockfd is not zero)
		// Player is still alive
		// And player is within explosion radius
		if (i != playerID && players[i].sockfd != 0 && players[i].isAlive && getDistance(playerID, i) <= EXPLOSION_RADIUS)
		{
			killedPlayers[numKills] = i;
			players[i].isAlive = false;
			numKills++;
			numAlivePlayers--;
			
			// simulate the chain reaction caused by the explosion of the killed player
			numKills += simRecursiveExplosion(i, killedPlayers, numKills + 1);
		}
	}
	
	return numKills;
}


int GameServer::broadcastSelfDestruct(int32_t playerID, int numKills, int32_t* killedPlayers)
{
	int numSent = 0;
	
	// Since all sockets get the same message,
	// A single common message buffer is used instead of individual player's buffer
	
	// Preparing the broadcast message
	// 4 bytes num bytes in message
	// 1 byte version number
	// 1 byte message code
	// 4 bytes ID of player exploded
	// 2 bytes for number of players killed
	// 4 bytes ID of each player killed
	int messageSize = 12 + numKills * 4;
	
	uint8_t* message = new uint8_t[messageSize];
	
	uint32_t convertedBytes = htonl(messageSize);
	int32_t convertedID = htonl(playerID);
	int16_t convertedNumKills = htons((int16_t)numKills);
	
	message[0] = GET_BYTE_3(convertedBytes);
	message[1] = GET_BYTE_2(convertedBytes);
	message[2] = GET_BYTE_1(convertedBytes);
	message[3] = GET_BYTE_0(convertedBytes);
	message[4] = VERSION_NUM;
	message[5] = ANNIHILATION_RESULTS;
	message[6] = GET_BYTE_3(convertedID); 			// third byte: byte 3 of ID
	message[7] = GET_BYTE_2(convertedID); 			// 4th byte: byte 2 of ID
	message[8] = GET_BYTE_1(convertedID);			// 5th byte: byte 1 of ID
	message[9] = GET_BYTE_0(convertedID);			// 6th byte: byte 0  of ID 
	message[10] = GET_BYTE_1(convertedNumKills);	// 7th byte: byte 1 of numKills
	message[11] = GET_BYTE_0(convertedNumKills);	// 8th byte: byte 0 of numKills
	
	// Note: although the player's ID is 32 bits,
	// only 16 bits are used to store the number of players killed
	// Hence the maximum num of players killed is smaller than maximum number of players
	// However, since the protocol specifies that 16 bits are used, I'll go with it
	
	int index = 12;
	
	// Load the IDs of players killed
	for (int i = 0; i < numKills; i++)
	{
		convertedID = htonl(killedPlayers[i]);
		
		message[index] = GET_BYTE_3(convertedID); 		// Byte 3 of ID
		message[index + 1] = GET_BYTE_2(convertedID); 	// Byte 2 of ID
		message[index + 2] = GET_BYTE_1(convertedID); 	// Byte 1 of ID
		message[index + 3] = GET_BYTE_0(convertedID);	// Byte 0 of ID
		
		// Move the index to the next 4 bytes block
		index += 4;
	}
	
	// Iterate through each player and send the message
	for (int i = 0; i < PLAYER_LIMIT; i++)
	{
		// If the player is active
		if (players[i].sockfd != 0)
		{
			ssize_t bytes = -1;
	
			// If the socket is ready for writing
			if (FD_ISSET(players[i].sockfd, &writeSet))
			{
				bytes = send(players[i].sockfd, message, messageSize, 0);
			}
			
			// If an error occurs (bytes is -1 or the wrong number of bytes sent)
			// Retry at most 3 times
			int count = 3;
			while (bytes != messageSize && count > 0)
			{
				bytes = send(players[i].sockfd, message, messageSize, 0);
				count--;
			}
			
			// If the message is sent successfully
			if (bytes == messageSize) numSent++;
			
			// More sophisticated error handling is obviously needed
		}
	}
	
	// Clear the buffer
	delete[] message;
	
	return numSent;
}


float GameServer::getDistance(int32_t playerID1, int32_t playerID2)
{
	float x = abs(players[playerID1].x - players[playerID2].x);
	float y = abs(players[playerID1].y - players[playerID2].y);
	float z = abs(players[playerID1].z - players[playerID2].z);
	
	return sqrt(x * x + y * y + z * z);
}


int GameServer::broadcastNewSpawn(int32_t playerID)
{
	int numSent = 0;
	
	// Since all sockets get the same message,
	// A single common message buffer is used instead of individual player's buffer
	
	// Preparing the broadcast message
	// 4 bytes number of bytes in message
	// 1 byte version number
	// 1 byte message code
	// 4 bytes ID of player spawned
	// 4 bytes x coordinate of player spawned
	// 4 bytes y coordinate of player spawned
	// 4 bytes z coordinate of player spawned
	int messageSize = 22;
	
	uint32_t binaryX;
	uint32_t binaryY;
	uint32_t binaryZ;
	
	// Copy the binary bits in the float coordinates into uint32_t variables
	// This must be done instead of casting because
	// casting the float values to uint32_t is equivalent to rounding
	memcpy(&binaryX, &players[playerID].x, sizeof(float));
	memcpy(&binaryY, &players[playerID].y, sizeof(float));
	memcpy(&binaryZ, &players[playerID].z, sizeof(float));
	
	uint8_t* message = new uint8_t[messageSize];
	uint32_t convertedBytes = htonl(messageSize);
	int32_t convertedID = htonl(playerID);
	uint32_t convertedX = htonl(binaryX);
	uint32_t convertedY = htonl(binaryY);
	uint32_t convertedZ = htonl(binaryZ);
	
	message[0] = GET_BYTE_3(convertedBytes);
	message[1] = GET_BYTE_2(convertedBytes);
	message[2] = GET_BYTE_1(convertedBytes);
	message[3] = GET_BYTE_0(convertedBytes);
	message[4] = VERSION_NUM;
	message[5] = PLAYER_SPAWN_WITH_ID;
	message[6] = GET_BYTE_3(convertedID); 	// third byte: byte 3 of ID
	message[7] = GET_BYTE_2(convertedID); 	// 4th byte: byte 2 of ID
	message[8] = GET_BYTE_1(convertedID);	// 5th byte: byte 1 of ID
	message[9] = GET_BYTE_0(convertedID); 	// 6th byte: byte 0  of ID 
	message[10] = GET_BYTE_3(convertedX); 	// byte 3 of x coordinate
	message[11] = GET_BYTE_2(convertedX);	// byte 2 of x coordinate
	message[12] = GET_BYTE_1(convertedX); 	// byte 1 of x coordinate
	message[13] = GET_BYTE_0(convertedX); 	// byte 0 of x coordinate
	message[14] = GET_BYTE_3(convertedY); 	// byte 3 of y coordinate
	message[15] = GET_BYTE_2(convertedY); 	// byte 2 of y coordinate
	message[16] = GET_BYTE_1(convertedY);	// byte 1 of y coordinate
	message[17] = GET_BYTE_0(convertedY);	// byte 0 of y coordinate
	message[18] = GET_BYTE_3(convertedZ); 	// byte 3 of z coordinate
	message[19] = GET_BYTE_2(convertedZ); 	// byte 2 of z coordinate
	message[20] = GET_BYTE_1(convertedZ); 	// byte 1 of z coordinate
	message[21] = GET_BYTE_0(convertedZ);	// byte 0 of z coordinate
	
	// Iterate through each player and send the message
	for (int i = 0; i < PLAYER_LIMIT; i++)
	{
		// If the player is active and not the player spawned
		if (players[i].sockfd != 0 && i != playerID)
		{
			ssize_t bytes = -1;
	
			// If the socket is ready for writing
			if (FD_ISSET(players[i].sockfd, &writeSet))
			{
				bytes = send(players[i].sockfd, message, messageSize, 0);
			}
			
			// If an error occurs (bytes is -1 or the wrong number of bytes sent)
			// Retry at most 3 times
			int count = 3;
			while (bytes != messageSize && count > 0)
			{
				bytes = send(players[i].sockfd, message, messageSize, 0);
				count--;
			}
			
			// If the message is sent successfully
			if (bytes == messageSize)
			{
				fprintf(stdout, "New spawn broadcast sent to player %d\n", i);
				numSent++;
			}
			
			// More sophisticated error handling is obviously needed
		}
	}
	
	// Clear the buffer
	delete[] message;
	
	return numSent;
}


int GameServer::broadcastMapUpdate()
{	
	int numSent = 0;
	
	// Since all sockets get the same message,
	// A single common message buffer is used instead of individual player's buffer
	
	// Preparing the broadcast message
	// 4 bytes of num bytes in message
	// 1 byte version number
	// 1 byte message code
	// 2 bytes number of players alive on map
	// 4 bytes ID of each alive player
	// 4 bytes x coordinate of each alive player
	// 4 bytes y coordinate of each alive player
	// 4 bytes z coordinate of each alive player
	int messageSize = 8 + 16 * numAlivePlayers;
	
	uint8_t* message = new uint8_t[messageSize];
	uint32_t convertedBytes = htonl(messageSize);
	uint16_t convertedNumPlayers = htons((uint16_t)numAlivePlayers);
	
	message[0] = GET_BYTE_3(convertedBytes);
	message[1] = GET_BYTE_2(convertedBytes);
	message[2] = GET_BYTE_1(convertedBytes);
	message[3] = GET_BYTE_0(convertedBytes);
	message[4] = VERSION_NUM;
	message[5] = SERVER_MAP_UPDATE;
	message[6] = GET_BYTE_1(convertedNumPlayers); 	// byte 1 of numAlivePlayers
	message[7] = GET_BYTE_0(convertedNumPlayers); 	// byte 0 of numAlivePlayers
	
	// Note: although the player's ID is 32 bits,
	// only 16 bits are used to store the number of players on map
	// Hence the maximum num of players allowed on map is smaller than maximum number of players
	// However, since the protocol specifies that 16 bits are used, I'll go with it
	
	int index = 8;
	
	// Load info of each alive player into message
	for (int32_t i = 0; i < PLAYER_LIMIT; i++)
	{	
		// If player is active and alive
		if (players[i].sockfd != 0 && players[i].isAlive)
		{	
			uint32_t binaryX;
			uint32_t binaryY;
			uint32_t binaryZ;
			
			// Copy the binary bits in the float coordinates into uint32_t variables
			// This must be done instead of casting because
			// casting the float values to uint32_t is equivalent to rounding
			memcpy(&binaryX, &players[i].x, sizeof(float));
			memcpy(&binaryY, &players[i].y, sizeof(float));
			memcpy(&binaryZ, &players[i].z, sizeof(float));
			
			int32_t convertedID = htonl(i);
			uint32_t convertedX = htonl(binaryX);
			uint32_t convertedY = htonl(binaryY);
			uint32_t convertedZ = htonl(binaryZ);	
			
			message[index] = GET_BYTE_3(convertedID); 		// third byte: byte 3 of ID
			message[index + 1] = GET_BYTE_2(convertedID); 	// 4th byte: byte 2 of ID
			message[index + 2] = GET_BYTE_1(convertedID); 	// 5th byte: byte 1 of ID
			message[index + 3] = GET_BYTE_0(convertedID);	// 6th byte: byte 0  of ID 
			message[index + 4] = GET_BYTE_3(convertedX); 	// byte 3 of x coordinate
			message[index + 5] = GET_BYTE_2(convertedX); 	// byte 2 of x coordinate
			message[index + 6] = GET_BYTE_1(convertedX);	// byte 1 of x coordinate
			message[index + 7] = GET_BYTE_0(convertedX); 	// byte 0 of x coordinate
			message[index + 8] = GET_BYTE_3(convertedY); 	// byte 3 of y coordinate
			message[index + 9] = GET_BYTE_2(convertedY); 	// byte 2 of y coordinate
			message[index + 10] = GET_BYTE_1(convertedY); 	// byte 1 of y coordinate
			message[index + 11] = GET_BYTE_0(convertedY); 	// byte 0 of y coordinate
			message[index + 12] = GET_BYTE_3(convertedZ); 	// byte 3 of z coordinate
			message[index + 13] = GET_BYTE_2(convertedZ); 	// byte 2 of z coordinate
			message[index + 14] = GET_BYTE_1(convertedZ); 	// byte 1 of z coordinate
			message[index + 15] = GET_BYTE_0(convertedZ); 	// byte 0 of z coordinate 
			
			// Move the index to the next 16 bytes block
			index += 16;
		}
	}
	
	// Iterate through active each player and send the message
	for (int i = 0; i < PLAYER_LIMIT; i++)
	{
		// If the player is active
		if (players[i].sockfd)
		{
			ssize_t bytes = -1;
	
			// If the socket is ready for writing
			if (FD_ISSET(players[i].sockfd, &writeSet))
			{
				bytes = send(players[i].sockfd, message, messageSize, 0);
			}
			
			// If an error occurs (bytes is -1 or the wrong number of bytes sent)
			// Retry at most 3 times
			int count = 3;
			while (bytes != messageSize && count > 0)
			{
				bytes = send(players[i].sockfd, message, messageSize, 0);
				count--;
			}
			
			// If the message is sent successfully
			if (bytes == messageSize) numSent++;
			
			// More sophisticated error handling is obviously needed
		}
	}
	
	// Clear the buffer
	delete[] message;
	
	return numSent;
}
