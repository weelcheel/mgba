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
    TCP_STATE_CONNECT,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_DISCONNECTED,
};

struct GBASIOTCPSocket {
    struct GBASIODriver driver;
    struct mTimingEvent event;

    bool isActive;
    enum TcpState state;
    enum TcpState nextState;
};

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket*);
void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket*);

CXX_GUARD_END

#endif