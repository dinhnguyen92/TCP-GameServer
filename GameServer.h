#ifndef GAME_SERVER_H
#define GAME_SERVER_H


/********************************************************************************************************************************************
 * 
 * TCP is a streaming protocol and is not a "packet-based" protocol like UDP.
 * As such, small data "packets" in TCP are often concatenated/accumulated in the sender's buffer before being sent.
 * This is the result of Nagle's algorithm, which is implemented for congestion control purpose.
 * For this application, most of the packets sent are small, and there's no streaming.
 * At first glance, Nagle's algorithm should be turned off to prevent packets from being concatenated.
 * If the algorithm is not disabled, extra overhead is needed on both the sender and receiver to packetize and process concatenated packets.
 * However, testing showed that even with Nagle's algorithm disabled (by setting the socket option TCP_NODELAY),
 * packet concatenation still occurs, even though less frequently.
 * The program was tested on an Ubuntu 15.10 virtual machine on a Window machine.
 * Information from the internet suggests that this is not a problem on real Linux machines.
 * This means that TCP_NODELAY is system-dependent, which means that it should not be used for networking applications.
 * As such, 4 extra header bytes are added to the beginning of every packet to store the number of bytes in the packet
 * in a packet to allow the receiver to parse concatenated packets.
 * 
 *********************************************************************************************************************************************/


#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctime>


#define VERSION_NUM					1

// Message code
#define PLAYER_MOVE 				1
#define PLAYER_SELF_ANNIHILATE 		2
#define PLAYER_SPAWN 				3
#define PLAYER_JOIN_RESPONSE 		4
#define SERVER_MAP_UPDATE 			5
#define PLAYER_SPAWN_WITH_ID 		6
#define ANNIHILATION_RESULTS		7

#define EXPLOSION_RADIUS 			0.25
#define BUFFER_SIZE 				1024
#define MAP_UPDATE_MILLISEC			50
#define PLAYER_LIMIT				20

// Macros for extracting bytes
#define GET_BYTE_3(x)	((x & 0xFF000000) >> 24)
#define GET_BYTE_2(x)	((x & 0x00FF0000) >> 16)		
#define GET_BYTE_1(x)	((x & 0x0000FF00) >> 8)
#define GET_BYTE_0(x)	(x & 0x000000FF)


using namespace std;


// Struct writtent based on udp-client.c UDPClient
typedef struct
{
	int sockfd;
	const char* hostName;
	const char* portNum;
	
	// 4kB buffers
	uint8_t recvBuffer[BUFFER_SIZE];
	uint8_t sendBuffer[BUFFER_SIZE];
	
	struct addrinfo info;
	struct sockaddr addr;
	socklen_t addrlen;
	
} TCPHost;


typedef struct
{
	int sockfd;
	
	uint8_t recvBuffer[BUFFER_SIZE];
	uint8_t sendBuffer[BUFFER_SIZE];
	
	struct addrinfo info;
	struct sockaddr addr;
	socklen_t addrlen;
	
	float x, y, z;
	bool isAlive;
	int score;
	
} Player;

class GameServer
{
	private:
		
		TCPHost* server;
		Player players[PLAYER_LIMIT];
		struct timeval timeout;
		int32_t playerLimit;
		int32_t numActiveSockets;
		int32_t numAlivePlayers;
		int maxfd;
		
		fd_set masterSet;
		fd_set readSet;
		fd_set writeSet;
		fd_set exceptSet;
		
		
		/*
		 * Functions to set up sockets and hosts
		 */
		
		// Get the addrinfo of the server at the specified port number
		addrinfo* getTCPServerAddrInfo(const char* portNum);

		// Create a socket file descriptor for the host address
		// Return the socket file descriptor or -1 if unsuccessful
		int createSocketFD(struct addrinfo* hostAddr);
		
		// Set the passed-in socket to non-blocking
		// Return -1 if error
		int setSocketNonBlocking(int sockdf);
		
		// Mark the socket as accepting connections
		// Return -1 if error
		int setSocketListen(int sockfd, int backlog);
		
		// Create a TCP server at the specified port number
		TCPHost* createTCPServer(const char* portNum, int backlog);
		
		
		/*
		 * Game Server utility functions 
		 */
		  
		// Send a join response to player when they first join the server
		// playerID: ID assigned to the new player
		// Return 0 on success, -1 if there's error
		int sendJoinResponse(int32_t playerID);
		
		// Send map update to a all players
		// The update contains ID, position, and score of each player
		// Return number of messages sent successfully
		int broadcastMapUpdate();
		
		// Announce self-destruct event to all players
		// The message contains: ID of self-destructed player, and IDs of players taken out
		// Return number of messages sent successfully
		int broadcastSelfDestruct(int32_t playerID, int numKills, int32_t* killedPlayers);
		
		// Announce a new spawn event to all players
		// The message contains: ID and location of newly spawned player
		// Return 0 on success, -1 if there's error
		int broadcastNewSpawn(int32_t playerID);
		
		// Accept a new player 
		// return player's ID on success, -1 if there's error, -2 if no available player slot
		int32_t acceptNewPlayer();
		
		// Process a message from the player with the specified ID
		// Return 0 on success, -1 on error
		int processPlayerMessage(int32_t playerID);
		
		// Simulate the chain reaction caused by explosion of player specified by playerID
		// The players killed will be set to not alive
		// The IDs of killed players are saved to killedPlayers
		// Index: where to save killed player into killedPlayers array
		// Return the number of players killed
		int simRecursiveExplosion(int32_t playerID, int32_t* killedPlayers, int index);
		
		// Calculate the distance between 2 players
		float getDistance(int32_t playerID1, int32_t playerID2);
		
		
	public:

		// Create a game server at the specified port num
		GameServer(const char* portNum);
		
		~GameServer();
		
		void run();
};

#endif
