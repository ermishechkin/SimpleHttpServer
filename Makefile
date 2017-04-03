all:
	g++ -c -O3 -std=c++11 -fPIC  -o main.o main.cpp
	g++ -c -O3 -std=c++11 -fPIC  -o network.o network.cpp
	g++ -c -O3 -std=c++11 -fPIC  -o httparser.o httparser.cpp
	g++ -c -O3 -std=c++11 -fPIC  -o config.o config.cpp
	g++ -Wl,-O1 -o httpd main.o network.o httparser.o config.o -levent -lpthread
	rm main.o network.o httparser.o config.o

