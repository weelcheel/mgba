#include <mgba/internal/gba/sio/tcpsocket.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver);
static void GBASIOTCPSocketReset(struct GBASIODriver* driver);
static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static bool GBASIOTCPSocketHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static int GBASIOTCPSocketConnectedDevices(struct GBASIODriver* driver);
static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate);
static uint16_t GBASIOTCPSocketWriteSIOCNT(struct GBASIODriver* driver, uint16_t value);
static bool GBASIOTCPSocketStart(struct GBASIODriver* driver);
static uint32_t GBASIOTCPSocketFinishNormal32(struct GBASIODriver* driver);

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket* tcp) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Creating SIO TCP socket driver...");
    memset(&tcp->driver, 0, sizeof(tcp->driver));
    tcp->driver.init = GBASIOTCPSocketInit;
    tcp->driver.reset = GBASIOTCPSocketReset;
    tcp->driver.setMode = GBASIOTCPSocketSetMode;
    tcp->driver.handlesMode = GBASIOTCPSocketHandlesMode;
    tcp->driver.connectedDevices = GBASIOTCPSocketConnectedDevices;
    tcp->driver.start = GBASIOTCPSocketStart;
    tcp->driver.finishNormal32 = GBASIOTCPSocketFinishNormal32;

    tcp->event.context = tcp;
    tcp->event.name = "GB SIO TCP Socket";
    tcp->event.callback = GBASIOTCPSocketProcessEvents;
    tcp->event.priority = 0x80;

    tcp->isActive = false;
    tcp->state = TCP_STATE_INIT;
    tcp->nextState = TCP_STATE_INIT;
}

void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket* tcp) {
    // @TODO: cleanup socket
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Destroying SIO TCP socket driver...");
}

bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket* tcp) {
    // @TODO: create and connect socket
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Connecting SIO TCP socket...");
    return tcp->driver.init != NULL;
}

bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket* tcp) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Getting is connected...");
    return tcp->isActive;
}

// ================== PRIVATE FUNCTIONS ==================

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Initializing SIO TCP socket...");
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    GBASIOTCPSocketReset(driver);
    return true;
}

static void GBASIOTCPSocketReset(struct GBASIODriver* driver) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Resetting SIO TCP socket...");

    // @TODO: reset socket
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    tcp->isActive = false;
    mTimingDeschedule(&tcp->driver.p->p->timing, &tcp->event);
	mTimingSchedule(&tcp->driver.p->p->timing, &tcp->event, 0);
    tcp->nextState = TCP_STATE_HANDSHAKE;
}

static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Setting mode in SIO TCP socket...");
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    tcp->isActive = mode == GBA_SIO_NORMAL_32;
}

static bool GBASIOTCPSocketHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
    UNUSED(driver);
    return mode == GBA_SIO_NORMAL_32;
}

static int GBASIOTCPSocketConnectedDevices(struct GBASIODriver* driver) {
    UNUSED(driver);
    return 1;
}

static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Process events callback...");
}

static uint16_t GBASIOTCPSocketWriteSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	UNUSED(driver);
	mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: SIOCNT <- %04X", value);
	return value;
}

static bool GBASIOTCPSocketStart(struct GBASIODriver* driver) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    uint32_t value = driver->p->p->memory.io[GBA_REG(SIODATA32_LO)] | (driver->p->p->memory.io[GBA_REG(SIODATA32_HI)] << 16);
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Received value <- %04X", value);

    switch (tcp->state)
    {
        case TCP_STATE_HANDSHAKE:
            if (value == 0xBAC7)
            {
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Handshake complete");
                tcp->nextState = TCP_STATE_CONNECTING;
            }
            else
            {
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Unexpected value during handshake.");
            }
            break;
        case TCP_STATE_CONNECTING:
            //@TODO: get URL string from value and connect TCP socket
            break;
    }

    tcp->isActive = true;
    return true;
}

static uint32_t GBASIOTCPSocketFinishNormal32(struct GBASIODriver* driver) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    uint32_t result = 0;
    switch (tcp->state)
    {
        case TCP_STATE_HANDSHAKE:
            result = 0xBAC7;
            break;
        case TCP_STATE_CONNECTING:
            result = 0xBAC8;
            break;
    }

    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Finish normal32: %04X | Next state: %d", result, tcp->nextState);
    tcp->state = tcp->nextState;
    return result;
}