#include <stdio.h>

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
#elif PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

#if PLATFORM == PLATFORM_WINDOWS
#pragma comment(lib, "wsock32.lib")
#endif

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

bool setup() {
#if PLATFORM == PLATFORM_WINDOWS
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2,2), &wsaData) == NO_ERROR;
#else
    return true;
#endif
}

int createSocket(unsigned short port) {
    int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle <= 0) {
        printf("failed to create socket\n");
        return -1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short) port);

    if (bind(handle, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0) {
        printf("failed to bind socket\n");
        return -1;
    }

    return handle;
}

bool send(int handle, const Message* message, unsigned int messageSize) {
    unsigned int a = 127;
    unsigned int b = 0;
    unsigned int c = 0;
    unsigned int d = 7;
    unsigned short port = 3000;
    unsigned int address = ( a << 24 ) | 
        ( b << 16 ) | 
        ( c << 8  ) | 
        d;

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

void read(int socket) {
    while ( true )
    {
        Message msg;
        unsigned int maxPacketSize = sizeof(Message);
#if PLATFORM == PLATFORM_WINDOWS
        typedef int socklen_t;
#endif
        sockaddr_in from;
        socklen_t fromLength = sizeof(from);
        int bytes = recvfrom(socket, (char*) &msg.raw, maxPacketSize, 0, 
            (sockaddr*) &from, 
            &fromLength
        );

        if (bytes <= 0)
            break;

        unsigned int fromAddress = ntohl(from.sin_addr.s_addr);
        unsigned int fromPort = ntohs(from.sin_port);

        // for (int i = 0; i < bytes; ++i) {
        //     printf("%c\n", b);
        // }
        printf("protocol: %d, seq: %d, content: %s\n", 
            msg.protocol,
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

void destroySocket(int socket) {
#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
    close(socket);
#elif PLATFORM == PLATFORM_WINDOWS
    closesocket(socket);
#endif
}

int main() {
    if (!setup()) {
        return 1;
    }
    int socket = createSocket(3000);
    if (socket <= 0) {
        return 2;
    }

    printf("Server listening\n");
    
    Message msg;
    char data[] = "hello world first message sent over udp yay";
    memcpy(msg.content, data, sizeof(data));
    msg.protocol = 69;
    msg.sequence = 420;
    send(socket, &msg, sizeof(msg));
    send(socket, &msg, sizeof(msg));
    read(socket);

    destroySocket(socket);
    cleanup();
}
