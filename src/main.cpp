#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include <unordered_map>

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

// TODO: use fixed size types
union Message {
    char raw[256];
    struct {
        unsigned int protocol;
        unsigned int sequence;
        unsigned int ark;
        int arkFlags;

        char content[];
    };
};

struct Connection {
    unsigned int sequence;
    unsigned int remoteSequence;
};

struct UdpSocket {
    std::unordered_map<unsigned int, Connection> connections;

    int handle;

    bool init(unsigned short port);
    void destroy();

    void read();
    bool send(int port, const Message* message, unsigned int messageSize);
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

    return true;
}

bool UdpSocket::send(int port, const Message* message, unsigned int messageSize) {
    unsigned int a = 127;
    unsigned int b = 0;
    unsigned int c = 0;
    unsigned int d = 7;
    unsigned int address = ( a << 24 ) | ( b << 16 ) | ( c << 8  ) | d;

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

        // TODO: validate request
        if (msg.protocol != PROTOCOL_VERSION) {
            continue;
        }

        printf("seq: %d, content: %s\n", 
            msg.sequence,
            msg.content
        );
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
    closesocket(socket);
#endif
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Expected 2 arguments\n");
        return 1;
    }
    int port = strtol(argv[1], NULL, 10);
    int targetPort = strtol(argv[2], NULL, 10);

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
    msg.sequence = 420;

    while (true) {
        socket.send(targetPort, &msg, sizeof(msg));
        socket.read();
        // wait(1.0 / 30.0);
        wait(1.0);
    }

    socket.destroy();
    cleanup();
}
