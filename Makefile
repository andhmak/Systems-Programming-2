bin/dataServer: build/dataServer.o build/serverWorker.o build/serverCommunication.o build/commonFuncs.o
	@echo " Link dataServer ...";
	g++ -g ./build/dataServer.o build/serverWorker.o build/serverCommunication.o build/commonFuncs.o -o ./bin/dataServer -lpthread

build/dataServer.o: src/dataServer.cpp
	@echo " Compile dataServer ...";
	g++ -I ./include/ -g -c -o ./build/dataServer.o ./src/dataServer.cpp

build/serverWorker.o: src/serverWorker.cpp
	@echo " Compile serverWorker ...";
	g++ -I ./include/ -g -c -o ./build/serverWorker.o ./src/serverWorker.cpp

build/serverCommunication.o: src/serverCommunication.cpp
	@echo " Compile serverCommunication ...";
	g++ -I ./include/ -g -c -o ./build/serverCommunication.o ./src/serverCommunication.cpp

bin/remoteClient: build/remoteClient.o build/commonFuncs.o
	@echo " Link remoteClient ...";
	g++ -g ./build/remoteClient.o ./build/commonFuncs.o -o ./bin/remoteClient -lpthread

build/remoteClient.o: src/remoteClient.cpp
	@echo " Compile remoteClient ...";
	g++ -I ./include/ -g -c -o ./build/remoteClient.o ./src/remoteClient.cpp

build/commonFuncs.o: src/commonFuncs.cpp
	@echo " Compile commonFuncs ...";
	g++ -I ./include/ -g -c -o ./build/commonFuncs.o ./src/commonFuncs.cpp

all: bin/dataServer bin/remoteClient

run_server: bin/dataServer
	@echo " Run dataServer with default arguments ...";
	./bin/dataServer -p 12500 -s 2 -q 2 -b 512

run_client: bin/remoteClient
	@echo " Run remoteClient with default arguments ...";
	./bin/remoteClient -i 127.0.0.1 -p 12500 -d Server

clean:
	@echo " Delete binary and build ...";
	-rm -f ./bin/* ./build/*

cleanup:
	@echo " Delete output ...";
	-rm -r -f ./output/*