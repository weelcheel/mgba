#ifndef SIO_TCPSOCKET_H
#define SIO_TCPSOCKET_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/core/timing.h>
#include <mgba/internal/gba/sio.h>
#include <mgba-util/socket.h>

enum TcpState {
    TCP_STATE_INIT,
    TCP_STATE_HANDSHAKE,
    TCP_STATE_INIT_URL_META,
    TCP_STATE_INIT_URL_TRANSFER,
    TCP_STATE_PORT_TRANSFER,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_DISCONNECTED,
};

enum ErrorState {
    ERROR_STATE_NONE,
    ERROR_STATE_TIMEOUT,
    ERROR_STATE_FAILED_TO_CONNECT,
    ERROR_STATE_DISCONNECTED,
};

enum PacketReadingState {
    PACKET_READING_STATE_MAGIC,
    PACKET_READING_STATE_DATA_LENGTH,
    PACKET_READING_STATE_DATA,
};

struct TcpData {
    struct TcpData* next;

    uint16_t numBytes;
    uint8_t data[4096];
};

struct GBASIOTCPSocket {
    //bool (*convertAddress)(const uint8_t* input, struct Address* output);

    struct GBASIODriver driver;
    struct mTimingEvent event;

    bool isActive;
    bool isConnected;

    uint32_t inValue;

    enum TcpState state;
    enum TcpState nextState;
    enum ErrorState errorState;

    uint32_t ipAddress;
    uint16_t port;

    uint8_t incompletePacketBytes[8192];
    uint16_t incompletePacketBytesCount;
    uint16_t incompleteDataBytesToRead;

    enum PacketReadingState fromServerPacketReadingState;
    struct TcpData** dataToSendToGBAQueue;
    struct TcpData** dataToSendToServerQueue;

    struct TcpData* currentDataToSendToGBA;
    uint16_t currentDataToSendToGBABytesSent;

    uint8_t incomingDataBuffer[1024];
    uint16_t incomingDataBufferCount;
    uint16_t expectedIncomingBytesToRead;
    bool isExpectingIncomingData;
};

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket* tcp);
void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket*);

CXX_GUARD_END

#endif