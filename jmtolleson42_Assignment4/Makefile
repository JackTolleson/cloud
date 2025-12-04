CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
LIBS := -lzmq

all: lock_server lock_client

lock_server: lock_server.cpp
	$(CXX) $(CXXFLAGS) lock_server.cpp -o lock_server $(LIBS)

lock_client: lock_client.cpp
	$(CXX) $(CXXFLAGS) lock_client.cpp -o lock_client $(LIBS)

test: all
	@chmod +x test.sh
	@echo "Running test.sh..."
	@./test.sh

clean:
	rm -f lock_server lock_client

.PHONY: all clean test