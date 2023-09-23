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
// Expected outgoing packets per sec * packet timeout in sec * 2 + 2 (just to be sure)
#define PACKET_BUFFER_SIZE 100 * 2 * 2 + 2

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
    bool ark;
    time_t timestamp;
    time_t arkTimestamp;
};

struct Connection {
    unsigned int address;
    unsigned int sequence;
    unsigned int remoteSequence;
    unsigned int arkFlags;

    int currentPacketPtr;
    int currentArkPtr;
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
    void tick();
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
    message->arkFlags = connections[connectionId].arkFlags;
    
    int currentPacketPtr = connections[connectionId].currentPacketPtr;
    PacketRef packetRef;
    packetRef.sequence = sequence;
    packetRef.ark = false;
    packetRef.timestamp = time(NULL);
    connections[connectionId].packetBuffer[currentPacketPtr] = packetRef;

    if (++currentPacketPtr >= PACKET_BUFFER_SIZE) {
        currentPacketPtr = 0;
    }
    assert(currentPacketPtr != connections[connectionId].currentArkPtr);
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
    time_t currentTimestamp = time(NULL);
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

        unsigned int remoteSequence = connections[connectionId].remoteSequence;
        unsigned int arkFlags = connections[connectionId].arkFlags;
        if (msg.sequence > remoteSequence) {
            connections[connectionId].remoteSequence = msg.sequence;
            unsigned int diff = msg.sequence - remoteSequence;
            if (diff >= sizeof(arkFlags) * 8) {
                arkFlags = 0;
            } else {
                arkFlags = (arkFlags << diff) | (1 << (diff - 1));
            }
            remoteSequence = connections[connectionId].remoteSequence;
        } else {
            unsigned int diff = remoteSequence - msg.sequence;
            if (diff <= 32) {
                arkFlags = arkFlags | (1 << (diff - 1));
            }
        }
        connections[connectionId].arkFlags = arkFlags;

        printf("seq: %d, content: %s, from: %d\n", 
            msg.sequence,
            msg.content,
            connectionId
        );

        int currentArkPtr = connections[connectionId].currentArkPtr;
        const int currentPacketPtr = connections[connectionId].currentPacketPtr;
        PacketRef* packetBuffer = connections[connectionId].packetBuffer;
        if (currentArkPtr < 0)
            continue;

        while (true) {
            if (currentArkPtr == currentPacketPtr) {
                break;
            }

            PacketRef packet = packetBuffer[currentArkPtr];

            if (packet.sequence <= msg.ark && !packet.ark) {
                unsigned int sequenceDiff = msg.ark - packet.sequence;
                // need to use arkMask to only perform bitshift when
                // right operand >= 0 and < width of left operand
                // otherwise its UB!
                unsigned int arkMask = (sequenceDiff != 0 && sequenceDiff <= sizeof(msg.arkFlags) * 8) ? 
                    1 << (sequenceDiff - 1) : 
                    0;
                if (sequenceDiff == 0 || (msg.arkFlags & arkMask)) {
                    packet.ark = true;
                    packet.arkTimestamp = currentTimestamp;
                    packetBuffer[currentArkPtr] = packet;
                }
            }
            if (++currentArkPtr == PACKET_BUFFER_SIZE) {
                currentArkPtr = 0;
            }
        }
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
        connections[connectionId].currentArkPtr = -1;
        connections[connectionId].arkFlags = 0;
    }

    return connectionId;
}

void UdpSocket::tick() {
    time_t currentTimestamp = time(NULL);
    for (int i = 0; i < currentConnections; ++i) {
        int currentArkPtr = connections[i].currentArkPtr;
        int currentPacketPtr = connections[i].currentPacketPtr;
        if (currentArkPtr == -1 && currentPacketPtr != 0) {
            currentArkPtr = 0;
        }

        // TODO: Skip timed out or acknowledged packets here
        while (true) {
            int nextArkPtr = currentArkPtr;
            if (++nextArkPtr == PACKET_BUFFER_SIZE)
                nextArkPtr = 0;
            
            if (nextArkPtr == currentPacketPtr)
                break;

            PacketRef packet = connections[i].packetBuffer[nextArkPtr];
            if (packet.ark || currentTimestamp - packet.timestamp >= 2) {
                // TODO: Track metrics like rtt and packet loss
                currentArkPtr = nextArkPtr;

                if (!packet.ark) {
                    printf("Packet %u timeouted\n", packet.sequence);
                }
            } else {
                break;
            }
        }

        connections[i].currentArkPtr = currentArkPtr;
    }
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
    if (argc < 5) {
        printf("Expected 4 arguments\n");
        return 1;
    }
    int port = strtol(argv[1], NULL, 10);
    int targetPort = strtol(argv[2], NULL, 10);
    unsigned int address = parseAddress(argv[3]);
    int messagesPerTick = strtol(argv[4], NULL, 10);

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

    while(true) {
        for (int i = 0; i < messagesPerTick; ++i) {
            socket.send(targetPort, address, &msg, sizeof(msg));
        }
        socket.read();
        socket.tick();
        // wait(1.0 / 30.0);
        wait(1.0);
    }

    socket.destroy();
    cleanup();
}
