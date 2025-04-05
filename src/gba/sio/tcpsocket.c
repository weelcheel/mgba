#include <mgba/internal/gba/sio/tcpsocket.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

#define BITS_PER_SECOND 115200
#define CYCLES_PER_BIT (GBA_ARM7TDMI_FREQUENCY / BITS_PER_SECOND)
#define CLOCK_GRAIN (CYCLES_PER_BIT * 8)

#define TCP_PORT 42069
#define POLL_WAIT 500

#define TCP_DATA_NOOP           0xFFFFFFFF
#define TCP_DATA_SUCCESS        0x0710
#define TCP_DATA_FAILURE        0x0711

#define TCP_HANDSHAKE           0xBAC7
#define TCP_HANDSHAKE_SUCCESS   TCP_HANDSHAKE | (TCP_DATA_SUCCESS << 16)
#define TCP_URL_META            0xBAC8
#define TCP_URL_META_SUCCESS    TCP_URL_META | (TCP_DATA_SUCCESS << 16)
#define TCP_URL_HEADER          0xBAC9
#define TCP_URL_SUCCESS         TCP_URL_HEADER | (TCP_DATA_SUCCESS << 16)
#define TCP_PORT_HEADER         0xBACA
#define TCP_PORT_SUCCESS        TCP_PORT_HEADER | (TCP_DATA_SUCCESS << 16)
#define TCP_CONNECT_SUCCESS     0xBACB | (TCP_DATA_SUCCESS << 16)
#define TCP_FROM_GBA_SUCCESS    0xBACC | (TCP_DATA_SUCCESS << 16)
#define TCP_TO_GBA_SUCCESS      0xBACD | (TCP_DATA_SUCCESS << 16)

#define TCP_PACKET_MAGIC        0x07100420

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver);
static void GBASIOTCPSocketReset(struct GBASIODriver* driver);
static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static bool GBASIOTCPSocketHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static int GBASIOTCPSocketConnectedDevices(struct GBASIODriver* driver);
static bool GBASIOTCPSocketStart(struct GBASIODriver* driver);
static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate);
static uint32_t GBASIOTCPSocketFinishNormal32(struct GBASIODriver* driver);

static void TcpDataQueuePush(struct TcpData** queue, uint8_t* data, uint16_t numBytes, bool shouldMergeNext) {
    if (!queue || !data || numBytes == 0 || numBytes > 1024) {
        return;
    }

    struct TcpData* newData = malloc(sizeof(struct TcpData));
    if (!newData) {
        return;
    }

    newData->data = data;
    newData->numBytes = numBytes;
    newData->next = NULL;
    newData->shouldMergeNext = shouldMergeNext;

    if (*queue == NULL) {
        *queue = newData;
    } else {
        struct TcpData* current = *queue;
        while (current->next) {
            current = current->next;
        }
        current->next = newData;
    }
}

static struct TcpData* popTcpData(struct TcpData** queue) {
    if (!queue || !*queue) {
        return NULL;
    }

    struct TcpData* poppedData = *queue;
    *queue = (*queue)->next;
    poppedData->next = NULL;

    return poppedData;
}

static struct TcpData* peekTcpData(struct TcpData** queue) {
    if (!queue || !*queue) {
        return NULL;
    }

    return *queue;
}

// ==========================================================

void GBASIOTCPSocketCreate(struct GBASIOTCPSocket* tcp) {
    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Creating SIO TCP socket driver...");
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
    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Destroying SIO TCP socket driver...");
}

bool GBASIOTCPSocketConnect(struct GBASIOTCPSocket* tcp) {
    return tcp->driver.init != NULL;
}

bool GBASIOTCPSocketIsConnected(struct GBASIOTCPSocket* tcp) {
    return true;
}

// ================== PRIVATE FUNCTIONS ==================

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver) {
    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Initializing SIO TCP socket...");
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    GBASIOTCPSocketReset(driver);
    return true;
}

static void GBASIOTCPSocketReset(struct GBASIODriver* driver) {
    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Resetting SIO TCP socket...");

    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;

    if (!tcp) {
        return;
    }

    tcp->socket = INVALID_SOCKET;
    tcp->isConnected = false;
    tcp->state = TCP_STATE_INIT;
    tcp->port = 0;
    tcp->receivedDataQueue = NULL;
    tcp->dataToSendQueue = NULL;
    tcp->isActive = false;
    tcp->errorState = ERROR_STATE_NONE;
    tcp->inValue = 0xFFFFFFFF;

    mTimingDeschedule(&tcp->driver.p->p->timing, &tcp->event);
    mTimingSchedule(&tcp->driver.p->p->timing, &tcp->event, 0);
}

static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Setting mode in SIO TCP socket to %d...", mode);
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;

    if (mode == GBA_SIO_NORMAL_32) {
        tcp->state = TCP_STATE_HANDSHAKE;
        tcp->nextState = TCP_STATE_HANDSHAKE;
        tcp->isActive = true;
    }
    else {
        tcp->isActive = false;
        tcp->state = TCP_STATE_INIT;
        tcp->nextState = TCP_STATE_INIT;
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

static void GBASIOTCPSocketReadPacket(struct GBASIOTCPSocket* tcp) {
    uint8_t buffer[1031];
    int readBytes = SocketRecv(tcp->socket, buffer, sizeof(buffer));

    // add the data to the incomplete packet buffer
    if (readBytes > 0) {
        memcpy(tcp->incompletePacketBytes + tcp->incompletePacketBytesCount, buffer, readBytes);
        tcp->incompletePacketBytesCount += readBytes;
    }

    bool shouldKeepReading = true;
    while (shouldKeepReading) {
        shouldKeepReading = false;
        switch (tcp->packetReadingState) {
            case PACKET_READING_STATE_HEADER:
                if (tcp->incompletePacketBytesCount >= 4) {
                    // get the first 4 bytes of the incomplete packet as a 32 bit integer
                    uint32_t packetHeader = *(uint32_t*) tcp->incompletePacketBytes;          
                    if (packetHeader == TCP_PACKET_MAGIC) {
                        tcp->packetReadingState = PACKET_READING_STATE_DATA_LENGTH;
                    }
                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + 4, tcp->incompletePacketBytesCount - 4);
                    tcp->incompletePacketBytesCount -= 4;
                    memset(tcp->incompletePacketBytes + tcp->incompletePacketBytesCount, 0, sizeof(tcp->incompletePacketBytes) - tcp->incompletePacketBytesCount);
                    shouldKeepReading = true;
                }
                break;
            case PACKET_READING_STATE_DATA_LENGTH:
                if (tcp->incompletePacketBytesCount >= 2) {
                    // read the first two bytes of the incomplete packet as a 16 bit integer
                    uint16_t dataLength = *(uint16_t*) (tcp->incompletePacketBytes);
                    tcp->incompleteDataBytesToRead = dataLength;
                    tcp->packetReadingState = PACKET_READING_STATE_SHOULD_MERGE;
                    
                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + 2, tcp->incompletePacketBytesCount - 2);
                    tcp->incompletePacketBytesCount -= 2;
                    memset(tcp->incompletePacketBytes + tcp->incompletePacketBytesCount, 0, sizeof(tcp->incompletePacketBytes) - tcp->incompletePacketBytesCount);
                    shouldKeepReading = true;
                }
                break;
            case PACKET_READING_STATE_SHOULD_MERGE:
                if (tcp->incompletePacketBytesCount >= 1) {
                    // read the first byte of the incomplete packet as a boolean
                    uint8_t mergeNext = *(uint8_t*) (tcp->incompletePacketBytes);
                    tcp->shouldMergeNextIncompleteData = mergeNext;
                    tcp->packetReadingState = PACKET_READING_STATE_DATA;

                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + 1, tcp->incompletePacketBytesCount - 1);
                    tcp->incompletePacketBytesCount -= 1;
                    memset(tcp->incompletePacketBytes + tcp->incompletePacketBytesCount, 0, sizeof(tcp->incompletePacketBytes) - tcp->incompletePacketBytesCount);
                    shouldKeepReading = true;
                }
                break;
            case PACKET_READING_STATE_DATA:
                if (tcp->incompletePacketBytesCount >= tcp->incompleteDataBytesToRead) {
                    uint8_t data[tcp->incompleteDataBytesToRead];
                    memcpy(data, tcp->incompletePacketBytes, tcp->incompleteDataBytesToRead);
                    TcpDataQueuePush(tcp->receivedDataQueue, data, tcp->incompleteDataBytesToRead, tcp->shouldMergeNextIncompleteData);

                    tcp->shouldMergeNextIncompleteData = false;
                    tcp->packetReadingState = PACKET_READING_STATE_HEADER;
                    
                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + tcp->incompleteDataBytesToRead, tcp->incompletePacketBytesCount - tcp->incompleteDataBytesToRead);
                    tcp->incompletePacketBytesCount -= tcp->incompleteDataBytesToRead;
                    memset(tcp->incompletePacketBytes + tcp->incompletePacketBytesCount, 0, sizeof(tcp->incompletePacketBytes) - tcp->incompletePacketBytesCount);
                    shouldKeepReading = true;

                    tcp->incompleteDataBytesToRead = 0;
                }
                break;
        }
    }
}

static void GBASIOTCPSocketSendPacket(struct GBASIOTCPSocket* tcp, struct TcpData* data) {
    if (!tcp || !data) {
        return;
    }

    // conver the tcp data struct into an array of bytes then send it
    uint8_t* dataToSend = malloc(data->numBytes + 7);
    if (!dataToSend) {
        return;
    }
    uint32_t packetHeader = htonl(TCP_PACKET_MAGIC);
    uint16_t dataLength = htons(data->numBytes);
    uint8_t shouldMergeNext = data->shouldMergeNext ? 1 : 0;
    memcpy(dataToSend, &packetHeader, 4);
    memcpy(dataToSend + 4, &dataLength, 2);
    memcpy(dataToSend + 6, &shouldMergeNext, 1);
    memcpy(dataToSend + 7, data->data, data->numBytes);

    SocketSend(tcp->socket, dataToSend, data->numBytes + 7);
}

static void GBASIOTCPSocketSendToGBA(struct GBASIOTCPSocket* tcp) {

}

static void GBASIOTCPSocketReceiveFromGBA(struct GBASIOTCPSocket* tcp, uint32_t receivedData) {
    // break down the 32 bit inValue into four 8 bit integers
    uint8_t data[4];
    data[0] = receivedData & 0xFF;
    data[1] = (receivedData >> 8) & 0xFF;
    data[2] = (receivedData >> 16) & 0xFF;
    data[3] = (receivedData >> 24) & 0xFF;
}

static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) context;

    int32_t nextEvent = CLOCK_GRAIN;
    mTimingSchedule(timing, &tcp->event, nextEvent);

    if (!tcp || !tcp->isActive) {
        return;
    }

    tcp->state = tcp->nextState;

    switch (tcp->state) {
        case TCP_STATE_CONNECTING:
            if (!tcp->isConnected && tcp->errorState == ERROR_STATE_NONE) {
                struct Address address;
                address.version = IPV4;
                address.ipv4 = tcp->ipAddress;
                if (!tcp->port) {
                    tcp->port = TCP_PORT;
                }

                char ipAddrStr[INET_ADDRSTRLEN];
                uint32_t ipAddr = htonl(tcp->ipAddress);
                inet_ntop(AF_INET, &(ipAddr), ipAddrStr, sizeof(ipAddrStr));
                mLOG(GBA_SIO, INFO, "TCP Socket Driver: Connecting to %s:%d", ipAddrStr, tcp->port);

                tcp->socket = SocketConnectTCP(tcp->port, &address);
                if (SOCKET_FAILED(tcp->socket)) {
                    tcp->socket = INVALID_SOCKET;
                    tcp->errorState = ERROR_STATE_FAILED_TO_CONNECT;
                    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Failed to connect to %s:%d", ipAddrStr, tcp->port);
                    break;
                }

                SocketSetBlocking(tcp->socket, false);
                SocketSetTCPPush(tcp->socket, true);

                tcp->isConnected = true;
                tcp->dataToSendQueue = NULL;
                tcp->receivedDataQueue = NULL;
                tcp->packetReadingState = PACKET_READING_STATE_HEADER;
                
                tcp->currentDataToReceive = malloc(sizeof(struct TcpData));
                tcp->currentDataToReceive->data = NULL;
                tcp->currentDataToReceive->numBytes = 0;
                tcp->currentDataToReceive->next = NULL;
                tcp->currentDataToReceive->shouldMergeNext = false;

                tcp->currentDataToSend = malloc(sizeof(struct TcpData));
                tcp->currentDataToSend->data = NULL;
                tcp->currentDataToSend->numBytes = 0;
                tcp->currentDataToSend->next = NULL;
                tcp->currentDataToSend->shouldMergeNext = false;

                tcp->incompletePacketBytesCount = 0;
                tcp->incompleteDataBytesToRead = 0;
                tcp->shouldMergeNextIncompleteData = false;
                memset(tcp->incompletePacketBytes, 0, sizeof(tcp->incompletePacketBytes));

                mLOG(GBA_SIO, INFO, "TCP Socket Driver: Connected to %s:%d", ipAddrStr, tcp->port);
            }
            break;
        case TCP_STATE_CONNECTED:
            GBASIOTCPSocketReadPacket(tcp);
            GBASIOTCPSocketSendPacket(tcp, popTcpData(tcp->dataToSendQueue));
            break;
        default:
            break;
    }

    if (!GBASIONormalIsIdleSo(tcp->driver.p->siocnt)) {
        uint32_t newValue = GBASIONormalFillIdleSo(tcp->driver.p->siocnt);
        GBASIOWriteSIOCNT(tcp->driver.p, GBASIONormalFillStart(newValue));
    }
}

static bool GBASIOTCPSocketStart(struct GBASIODriver* driver) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    
    tcp->inValue = tcp->driver.p->p->memory.io[GBA_REG(SIODATA32_LO)] | (tcp->driver.p->p->memory.io[GBA_REG(SIODATA32_HI)] << 16);
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Starting transfer with value %08X", tcp->inValue);

    return true;
}

static uint32_t GBASIOTCPSocketFinishNormal32(struct GBASIODriver* driver) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) driver;
    
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Finish transfer with inValue %08X in state %d", tcp->inValue, tcp->state);
    uint32_t result = TCP_DATA_NOOP;

    switch (tcp->state) {
        case TCP_STATE_HANDSHAKE:
            // expect the gba to send the handshake value, then send it back when it is received
            if (tcp->inValue == TCP_HANDSHAKE) {
                mLOG(GBA_SIO, INFO, "TCP Socket Driver: Handshake value is valid");
                tcp->nextState = TCP_STATE_INIT_URL_META;
                result = TCP_HANDSHAKE_SUCCESS;
            }
            break;
        case TCP_STATE_INIT_URL_META:
            if (tcp->inValue == TCP_URL_META) {
                mLOG(GBA_SIO, INFO, "TCP Socket Driver: Is ready to expect IP address");
                tcp->ipAddress = tcp->inValue;
                tcp->nextState = TCP_STATE_INIT_URL_TRANSFER;
                result = TCP_URL_META_SUCCESS;
            }
            break;
        case TCP_STATE_INIT_URL_TRANSFER:
            tcp->ipAddress = tcp->inValue;
            mLOG(GBA_SIO, INFO, "TCP Socket Driver: Received IP address %08X", tcp->inValue);
            tcp->nextState = TCP_STATE_PORT_TRANSFER;
            result = TCP_URL_SUCCESS;
            break;
        case TCP_STATE_PORT_TRANSFER:
            tcp->port = tcp->inValue;
            mLOG(GBA_SIO, INFO, "TCP Socket Driver: Received port %d", tcp->inValue);
            tcp->nextState = TCP_STATE_CONNECTING;
            result = TCP_PORT_SUCCESS;
            break;
        case TCP_STATE_CONNECTING:
            if (tcp->isConnected) {
                result = TCP_CONNECT_SUCCESS;
                tcp->nextState = TCP_STATE_CONNECTED;
            }
            else if (tcp->errorState != ERROR_STATE_NONE) {
                result = TCP_DATA_FAILURE | (tcp->errorState << 16);
                tcp->nextState = TCP_STATE_DISCONNECTED;
            }
            break;
        case TCP_STATE_CONNECTED:
            break;
        default:
            break;
    }

    return result;
}