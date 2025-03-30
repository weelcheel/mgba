#ifndef SIO_TCPSOCKET_H
#define SIO_TCPSOCKET_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/core/timing.h>
#include <mgba/internal/gba/sio.h>

#include <mgba-util/socket.h>

struct GBASIOTCPSocket {
    struct GBASIODriver driver;
    struct mTimingEvent event;

    bool isActive;
};

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket*);
void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket*);
bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket*, const struct Address* address);
bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket*);

CXX_GUARD_END

#endif