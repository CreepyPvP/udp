#include <bits/types/time_t.h>
#include <cstdint>
#include <cstdlib>
#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>

#define PLATFORM_WINDOWS  1
#define PLATFORM_MAC      2
#define PLATFORM_UNIX     3

#if defined(_WIN32)
#define PLATFORM PLATFORM_WINDOWS
#elif defined(__APPLE__)
#define PLATFORM PLATFORM_MAC
#else
#define PLATFORM PLATFORM_UNIX
#endif

#if PLATFORM == PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#elif PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#define PLATFORM PLATFORM_UNIX
#endif

#if PLATFORM == PLATFORM_WINDOWS
#pragma comment(lib, "wsock32.lib")
#endif

#define PROTOCOL_VERSION 1
#define CONNECTION_COUNT 16
#define PACKET_TIMEOUT_SEC 2.0
// Expected outgoing packets per sec * packet timeout in sec * 2 (just to be sure)
#define PACKET_BUFFER_SIZE 1 * 2 * 2

union Message {
    char raw[256];
    struct {
        std::uint32_t protocol;
        std::uint32_t sequence;
        std::uint32_t ark;
        std::int32_t arkFlags;

        char content[];
    };
};

struct PacketRef {
    unsigned int sequence;
    bool ack;
    time_t timestamp;
};

struct Connection {
    unsigned int address;
    unsigned int sequence;
    unsigned int remoteSequence;

    int currentPacketPtr;
    int currentAckPtr;
    PacketRef packetBuffer[PACKET_BUFFER_SIZE];
};

struct UdpSocket {
    int handle;
    int currentConnections;
    Connection connections[CONNECTION_COUNT];

    bool init(unsigned short port);
    void destroy();
    void read();
    bool send(int port, unsigned int address, Message* message, unsigned int messageSize);
    int findConnection(unsigned int address);
};

bool setup() {
#if PLATFORM == PLATFORM_WINDOWS
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2,2), &wsaData) == NO_ERROR;
#else
    return true;
#endif
}

void wait(double seconds) {
#if PLATFORM == PLATFORM_WINDOWS
    Sleep(seconds * 1000);
#else
    sleep(seconds);
#endif
}

bool UdpSocket::init(unsigned short port) {
    handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle <= 0) {
        printf("failed to create socket\n");
        return false;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short) port);

    if (bind(handle, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0) {
        printf("failed to bind socket\n");
        return false;
    }

#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
    int nonBlocking = 1;
    if (fcntl(handle, F_SETFL, O_NONBLOCK, nonBlocking) == -1) {
        printf("failed to set non-blocking\n");
        return false;
    }
#elif PLATFORM == PLATFORM_WINDOWS
    DWORD nonBlocking = 1;
    if (ioctlsocket(handle, FIONBIO, &nonBlocking) != 0) {
        printf("failed to set non-blocking\n");
        return false;
    }
#endif

    currentConnections = 0;

    return true;
}

bool UdpSocket::send(int port, unsigned int address, Message* message, unsigned int messageSize) {
    int connectionId = findConnection(address);
    unsigned int sequence = ++(connections[connectionId].sequence); 
    message->sequence = sequence;
    message->ark = connections[connectionId].remoteSequence;
    
    
    int currentPacketPtr = connections[connectionId].currentPacketPtr;
    PacketRef packetRef;
    packetRef.sequence = sequence;
    packetRef.ack = false;
    packetRef.timestamp = time(NULL);
    connections[connectionId].packetBuffer[currentPacketPtr] = packetRef;

    if (++currentPacketPtr >= PACKET_BUFFER_SIZE) {
        currentPacketPtr = 0;
    }
    connections[connectionId].currentPacketPtr = currentPacketPtr;

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address);
    addr.sin_port = htons(port);

    int sent_bytes = sendto(handle, message->raw, messageSize, 0, 
        (sockaddr*) &addr, 
        sizeof(sockaddr_in)
    );

    if (sent_bytes != messageSize) {
        printf("failed to send packet\n");
        return false;
    }

    return true;
}

void UdpSocket::read() {
    while (true)
    {
        Message msg;
        unsigned int maxPacketSize = sizeof(Message);
#if PLATFORM == PLATFORM_WINDOWS
        typedef int socklen_t;
#endif
        sockaddr_in from;
        socklen_t fromLength = sizeof(from);
        int bytes = recvfrom(handle, (char*) &msg.raw, maxPacketSize, 0, 
            (sockaddr*) &from, 
            &fromLength
        );

        if (bytes <= 0)
            break;

        unsigned int fromAddress = ntohl(from.sin_addr.s_addr);
        unsigned int fromPort = ntohs(from.sin_port);

        if (msg.protocol != PROTOCOL_VERSION) {
            continue;
        }

        int connectionId = findConnection(fromAddress);
        connections[connectionId].remoteSequence = msg.sequence;

        printf("seq: %d, content: %s, from: %d\n", 
            msg.sequence,
            msg.content,
            connectionId
        );
     }
}

int UdpSocket::findConnection(unsigned int address) {
    int connectionId = -1;
    for (int i = 0; i < currentConnections; ++i) {
        if (connections[i].address == address) {
            connectionId = i;
            break;
        }
    }
    if (connectionId < 0) {
        assert(currentConnections < CONNECTION_COUNT);
        connectionId = currentConnections;
        ++currentConnections;

        connections[connectionId].sequence = 0;
        connections[connectionId].address = address;
        connections[connectionId].remoteSequence = 0;
        connections[connectionId].currentPacketPtr = 0;
        connections[connectionId].currentAckPtr = -1;
    }

    return connectionId;
}

void cleanup() {
#if PLATFORM == PLATFORM_WINDOWS
    WSACleanup();
#endif
}

void UdpSocket::destroy() {
#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
    close(handle);
#elif PLATFORM == PLATFORM_WINDOWS
    closesocket(handle);
#endif
}

unsigned int parseAddress(const char* str) {
    unsigned int a = strtol(str, (char**) &str, 10);
    ++str;
    unsigned int b = strtol(str, (char**) &str, 10);
    ++str;
    unsigned int c = strtol(str, (char**) &str, 10);
    ++str;
    unsigned int d = strtol(str, (char**) &str, 10);
    ++str;
    return ( a << 24 ) | ( b << 16 ) | ( c << 8  ) | d;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Expected 3 arguments\n");
        return 1;
    }
    int port = strtol(argv[1], NULL, 10);
    int targetPort = strtol(argv[2], NULL, 10);
    unsigned int address = parseAddress(argv[3]);

    if (!setup()) {
        return 1;
    }
    UdpSocket socket;
    if (!socket.init(port)) {
        return 2;
    }

    printf("Server listening on port: %d, with target port: %d\n", port, targetPort);
    
    Message msg;
    char data[] = "hello world first message sent over udp yay";
    memcpy(msg.content, data, sizeof(data));
    msg.protocol = PROTOCOL_VERSION;

    while (true) {
        socket.send(targetPort, address, &msg, sizeof(msg));
        socket.read();
        // wait(1.0 / 30.0);
        wait(1.0);
    }

    socket.destroy();
    cleanup();
}
