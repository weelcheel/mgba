#ifndef SIO_TCPSOCKET_H
#define SIO_TCPSOCKET_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/core/timing.h>
#include <mgba/internal/gba/sio.h>

#include <mgba-util/socket.h>

enum TcpState {
    TCP_STATE_HANDSHAKE,
    TCP_STATE_INIT,
    TCP_STATE_INIT_URL_TRANSFER,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_DISCONNECTED,
};

struct GBASIOTCPSocket {
    struct GBASIODriver driver;
    struct mTimingEvent event;

    // websocket functions
    uint8_t* (*receive)(struct GBASIOTCPSocket* tcp);
    void (*send)(struct GBASIOTCPSocket* tcp, uint8_t* data, uint32_t length);
    bool (*connect)(struct GBASIOTCPSocket* tcp, const uint8_t* url);
    void (*disconnect)(struct GBASIOTCPSocket* tcp);

    bool isActive;
    enum TcpState state;

    bool hasReceivedValidHandshake;

    bool hasReceivedUrlHeader;
    uint16_t urlLength;
    uint16_t urlRecvPosition;
    uint8_t* url;

    uint32_t result;
};

extern const uint16_t TCP_PORT;

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket* tcp);
void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket*);

CXX_GUARD_END

#endif