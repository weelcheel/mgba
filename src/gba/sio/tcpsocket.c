#include <mgba/internal/gba/sio/tcpsocket.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

#include <libwebsockets.h>

#define BITS_PER_SECOND 115200
#define CYCLES_PER_BIT (GBA_ARM7TDMI_FREQUENCY / BITS_PER_SECOND)
#define CLOCK_GRAIN (CYCLES_PER_BIT * 8)

#define TCP_HANDSHAKE   0xBAC7
#define TCP_URL_META    0xBAC8
#define TCP_URL_HEADER  0xBAC9

const uint16_t TCP_PORT = 6969;

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver);
static void GBASIOTCPSocketReset(struct GBASIODriver* driver);
static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static bool GBASIOTCPSocketHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static int GBASIOTCPSocketConnectedDevices(struct GBASIODriver* driver);
static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate);
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
}

void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket* tcp) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Destroying SIO TCP socket driver...");
}

bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket* tcp) {
    return tcp->driver.init != NULL;
}

bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket* tcp) {
    return true;
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

    tcp->state = TCP_STATE_HANDSHAKE;
    tcp->hasReceivedValidHandshake = false;
    tcp->hasReceivedUrlHeader = false;
    tcp->urlLength = 0;
    tcp->result = 0;
    tcp->isActive = false;

    mTimingDeschedule(&tcp->driver.p->p->timing, &tcp->event);
	mTimingSchedule(&tcp->driver.p->p->timing, &tcp->event, 0);
}

static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Setting mode in SIO TCP socket to %d...", mode);
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;

    if (mode == GBA_SIO_NORMAL_32 && driver->start) {
        tcp->state = TCP_STATE_INIT;
        tcp->isActive = true;
    } else {
        mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Unsupported mode %d", mode);
    }
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
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) context;
   
    //@TODO: send and receive websocket frames

    int32_t nextEvent = CLOCK_GRAIN;
    mTimingSchedule(timing, &tcp->event, nextEvent);
}

static bool GBASIOTCPSocketStart(struct GBASIODriver* driver) {
    // process data received from the gba
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;

    if (!tcp->isActive) {
        return false;
    }

    uint32_t inValue = tcp->driver.p->p->memory.io[GBA_REG(SIODATA32_LO)] | (tcp->driver.p->p->memory.io[GBA_REG(SIODATA32_HI)] << 16);
    switch (tcp->state) {
        case TCP_STATE_HANDSHAKE:
            // expect the gba to send the handshake value, then send it back when it is received
            mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Checking handshake value %04X", inValue);
            if (inValue == TCP_HANDSHAKE) {
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Handshake value is valid");
                tcp->hasReceivedValidHandshake = true;
                tcp->state = TCP_STATE_INIT_URL_TRANSFER;
                tcp->result = TCP_HANDSHAKE;
            } else {
                tcp->hasReceivedValidHandshake = false;
                return false;
            }
            break;
        case TCP_STATE_INIT:
            // break down the 32 bit inValue to two 16 bit integers
            // if the first 16 bits are the URL header, then the second 16 bits are the URL string length
            uint16_t values[2] = { inValue & 0xFFFF, (inValue >> 16) & 0xFFFF };
            if (values[0] == TCP_URL_META) {
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Received TCP_URL_META expecting a URL of length %d", values[1]);
                tcp->urlLength = values[1];
                tcp->state = TCP_STATE_INIT_URL_TRANSFER;
                tcp->result = TCP_URL_META;
                tcp->hasReceivedUrlHeader = false;
            }
            else {
                tcp->state = TCP_STATE_INIT;
                tcp->urlLength = 0;
                return false;
            }
            break;
        case TCP_STATE_INIT_URL_TRANSFER:
            if (tcp->hasReceivedUrlHeader) {
                // break the incoming 32 bit integer down into four 8 bit characters
                // keep reading 8 bit characters until the expected urlLength is reached
                uint8_t url[4] = { inValue & 0xFF, (inValue >> 8) & 0xFF, (inValue >> 16) & 0xFF, (inValue >> 24) & 0xFF };
                uint8_t charsToRead = tcp->urlLength - (tcp->urlRecvPosition + 1);
                if (charsToRead > 4) {
                    charsToRead = 4;
                }
                for (uint8_t i = tcp->urlRecvPosition; i < tcp->urlRecvPosition + charsToRead; i++) {
                    tcp->url[i] = url[i - tcp->urlRecvPosition];
                }
                tcp->urlRecvPosition += charsToRead;
                if (tcp->urlRecvPosition >= tcp->urlLength) {
                    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: URL transfer complete, value is %s", tcp->url);
                    tcp->state = TCP_STATE_CONNECTING;
                    tcp->result = TCP_URL_HEADER;
                }
                else {
                    return false;
                }
            }
            else if (inValue == TCP_URL_HEADER) {
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: URL header is valid, expecting string data of length %d", tcp->urlLength);
                tcp->hasReceivedValidHandshake = true;
                tcp->state = TCP_STATE_INIT_URL_TRANSFER;
                tcp->result = TCP_URL_HEADER;
                tcp->url = malloc(tcp->urlLength);
                tcp->urlRecvPosition = 0;
            }
            else {
                tcp->hasReceivedUrlHeader = false;
                return false;
            }
            break;
        case TCP_STATE_CONNECTING:
            break;
    }
    return true;
}

static uint32_t GBASIOTCPSocketFinishNormal32(struct GBASIODriver* driver) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    uint32_t result = tcp->result;
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Finish 32 bit transfer - sending back %04X", result);
    return result;
}