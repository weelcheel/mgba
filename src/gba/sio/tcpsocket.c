#include <mgba/internal/gba/sio/tcpsocket.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver);
static void GBASIOTCPSocketReset(struct GBASIODriver* driver);
static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static bool GBASIOTCPSocketHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static int GBASIOTCPSocketConnectedDevices(struct GBASIODriver* driver);
static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate);

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket* tcp) {
    memset(&tcp->driver, 0, sizeof(tcp->driver));
    tcp->driver.init = GBASIOTCPSocketInit;
    tcp->driver.reset = GBASIOTCPSocketReset;
    tcp->driver.setMode = GBASIOTCPSocketSetMode;
    tcp->driver.handlesMode = GBASIOTCPSocketHandlesMode;
    tcp->driver.connectedDevices = GBASIOTCPSocketConnectedDevices;

    tcp->event.context = tcp;
    tcp->event.name = "GB SIO TCP Socket";
    tcp->event.callback = GBASIOTCPSocketProcessEvents;
    tcp->event.priority = 0x80;

    tcp->isActive = false;
}

void GBASIOTCPSocketDestroy(struct GBASIOTCPSocket* tcp) {
    // @TODO: cleanup socket
}

bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket* tcp) {
    // @TODO: create and connect socket
    return tcp->driver.init != NULL;
}

bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket* tcp) {
    return tcp->isActive;
}

// ================== PRIVATE FUNCTIONS ==================

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    GBASIOTCPSocketReset(driver);
    return true;
}

static void GBASIOTCPSocketReset(struct GBASIODriver* driver) {
    // @TODO: reset socket
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    tcp->isActive = false;
}

static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
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
    //@TODO: handle sending data to the GBA and receiving data from it
}