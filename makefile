# Compiler
CXX = g++
CXXFLAGS = -std=c++11 -Wall

# Executable names
SERVER = chat_server
CLIENT = chat_client

# Sources
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# Detect OS (Linux vs Windows/MinGW)
ifeq ($(OS),Windows_NT)
    RM = del /Q
    SERVER_EXE = $(SERVER).exe
    CLIENT_EXE = $(CLIENT).exe
else
    RM = rm -f
    SERVER_EXE = $(SERVER)
    CLIENT_EXE = $(CLIENT)
endif

# Build rules
all: $(SERVER_EXE) $(CLIENT_EXE)

$(SERVER_EXE): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(CLIENT_EXE): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Clean
clean:
	$(RM) $(SERVER_EXE) $(CLIENT_EXE) port.txt
