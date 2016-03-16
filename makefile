all: server

objects = main.o GameServer.o

server: $(objects)
	g++ -std=c++11 -g -Wall -o server $(objects)

main.o: main.cpp
	g++ -std=c++11 -g -Wall -c main.cpp

GameServer.o: GameServer.cpp
	g++ -std=c++11 -g -Wall -c GameServer.cpp
	
.Phony: clean
clean:
	rm $(objects)

