#include <mgba/internal/gba/sio/tcpsocket.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

#define BITS_PER_SECOND 115200
#define CYCLES_PER_BIT (GBA_ARM7TDMI_FREQUENCY / BITS_PER_SECOND)
#define CLOCK_GRAIN (CYCLES_PER_BIT * 8)

#define TCP_PORT 42069
#define POLL_WAIT 500

#define EVENT_INTERVAL (CLOCK_GRAIN * 4)
#define TCP_POLL_INTERVAL 4

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
#define TCP_RECEIVE_FROM_GBA    0xBACC
#define TCP_SEND_TO_GBA         0xBACD

#define TCP_PACKET_MAGIC        0x07100420

static Socket sTcpSocket;

static bool GBASIOTCPSocketInit(struct GBASIODriver* driver);
static void GBASIOTCPSocketReset(struct GBASIODriver* driver);
static void GBASIOTCPSocketSetMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static bool GBASIOTCPSocketHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static int GBASIOTCPSocketConnectedDevices(struct GBASIODriver* driver);
static bool GBASIOTCPSocketStart(struct GBASIODriver* driver);
static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate);
static uint32_t GBASIOTCPSocketFinishNormal32(struct GBASIODriver* driver);

static void TcpDataQueuePush(struct TcpData** queue, uint8_t* data, uint16_t numBytes) {
    if (!queue || !data || numBytes == 0 || numBytes > 1024) {
        return;
    }

    struct TcpData* newData = malloc(sizeof(struct TcpData));
    if (!newData) {
        return;
    }
    newData->next = NULL;
    newData->numBytes = numBytes;
    memcpy(newData->data, data, numBytes);

    if (*queue == NULL) {
        *queue = newData;
    } 
    else {
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

    sTcpSocket = INVALID_SOCKET;
    tcp->isConnected = false;
    tcp->state = TCP_STATE_INIT;
    tcp->port = 0;
    tcp->dataToSendToGBAQueue = NULL;
    tcp->dataToSendToServerQueue = NULL;
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

static void GBASIOTCPSocketReadPacketFromServer(struct GBASIOTCPSocket* tcp) {
    uint8_t buffer[5002];
    int readBytes = SocketRecv(sTcpSocket, buffer, sizeof(buffer));

    // add the data to the incomplete packet buffer
    if (readBytes > 0) {
        mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Received %d bytes of data from the server", readBytes);
        memcpy(tcp->incompletePacketBytes + tcp->incompletePacketBytesCount, buffer, readBytes);
        tcp->incompletePacketBytesCount += readBytes;
    }
    else {
        return;
    }

    bool shouldKeepReading = true;
    while (shouldKeepReading) {
        shouldKeepReading = false;
        switch (tcp->fromServerPacketReadingState) {
            case PACKET_READING_STATE_MAGIC:
                if (tcp->incompletePacketBytesCount >= 4) {
                    // get the first 4 bytes of the incomplete packet as a 32 bit integer
                    uint32_t packetHeader = *(uint32_t*) tcp->incompletePacketBytes;          
                    if (packetHeader == TCP_PACKET_MAGIC) {
                        tcp->fromServerPacketReadingState = PACKET_READING_STATE_DATA_LENGTH;
                    }
                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + 4, tcp->incompletePacketBytesCount - 4);
                    tcp->incompletePacketBytesCount -= 4;
                    shouldKeepReading = true;
                }
                break;
            case PACKET_READING_STATE_DATA_LENGTH:
                if (tcp->incompletePacketBytesCount >= 2) {
                    uint16_t dataLength = *(uint16_t*) (tcp->incompletePacketBytes);
                    tcp->incompleteDataBytesToRead = dataLength;
                    tcp->fromServerPacketReadingState = PACKET_READING_STATE_DATA;
                    
                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + 2, tcp->incompletePacketBytesCount - 2);
                    tcp->incompletePacketBytesCount -= 2;
                    shouldKeepReading = true;
                }
                break;
            case PACKET_READING_STATE_DATA:
                if (tcp->incompletePacketBytesCount >= tcp->incompleteDataBytesToRead) {
                    uint8_t data[tcp->incompleteDataBytesToRead];
                    memcpy(data, tcp->incompletePacketBytes, tcp->incompleteDataBytesToRead);
                    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Queueing %d bytes of data to send to the GBA", tcp->incompleteDataBytesToRead);
                    TcpDataQueuePush(tcp->dataToSendToGBAQueue, data, tcp->incompleteDataBytesToRead);

                    tcp->fromServerPacketReadingState = PACKET_READING_STATE_MAGIC;
                    
                    memmove(tcp->incompletePacketBytes, tcp->incompletePacketBytes + tcp->incompleteDataBytesToRead, tcp->incompletePacketBytesCount - tcp->incompleteDataBytesToRead);
                    tcp->incompletePacketBytesCount -= tcp->incompleteDataBytesToRead;
                    shouldKeepReading = true;

                    tcp->incompleteDataBytesToRead = 0;
                }
                break;
        }
    }
}

static void GBASIOTCPSocketSendPacketToServer(struct GBASIOTCPSocket* tcp, struct TcpData* data) {
    if (!tcp || !data) {
        return;
    }

    // conver the tcp data struct into an array of bytes then send it
    uint8_t dataToSend[data->numBytes + 6];
    if (!dataToSend) {
        return;
    }
    uint32_t packetHeader = TCP_PACKET_MAGIC;
    uint16_t dataLength = data->numBytes;
    memcpy(dataToSend, &packetHeader, 4);
    memcpy(dataToSend + 4, &dataLength, 2);
    memcpy(dataToSend + 6, data->data, data->numBytes);

    uint32_t sentBytes = SocketSend(sTcpSocket, dataToSend, data->numBytes + 6);
    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Sent %d bytes of data to the server", sentBytes);
}

static void GBASIOTCPSocketProcessEvents(struct mTiming* timing, void* context, uint32_t cyclesLate) {
    struct GBASIOTCPSocket* tcp = (struct GBASIOTCPSocket*) context;

    int32_t nextEvent = EVENT_INTERVAL;
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

                sTcpSocket = SocketConnectTCP(tcp->port, &address);
                if (SOCKET_FAILED(sTcpSocket)) {
                    sTcpSocket = INVALID_SOCKET;
                    tcp->errorState = ERROR_STATE_FAILED_TO_CONNECT;
                    mLOG(GBA_SIO, INFO, "TCP Socket Driver: Failed to connect to %s:%d", ipAddrStr, tcp->port);
                    break;
                }
                SocketSetBlocking(sTcpSocket, false);

                tcp->isConnected = true;
                tcp->dataToSendToGBAQueue = malloc(sizeof(struct TcpData*));
                *tcp->dataToSendToGBAQueue = NULL;
                tcp->dataToSendToServerQueue = malloc(sizeof(struct TcpData*));
                *tcp->dataToSendToServerQueue = NULL;
                tcp->fromServerPacketReadingState = PACKET_READING_STATE_MAGIC;

                tcp->currentDataToSendToGBA = NULL;

                tcp->incompletePacketBytesCount = 0;
                tcp->incompleteDataBytesToRead = 0;
                memset(tcp->incompletePacketBytes, 0, sizeof(tcp->incompletePacketBytes));
                tcp->tcpPollCounter = 0;

                mLOG(GBA_SIO, INFO, "TCP Socket Driver: Socket %d Connected to %s:%d", sTcpSocket, ipAddrStr, tcp->port);
            }
            break;
        case TCP_STATE_CONNECTED:
            if (SOCKET_FAILED(sTcpSocket)) {
                mLOG(GBA_SIO, INFO, "TCP Socket Driver: Socket %d is invalid, disconnecting...", sTcpSocket);
                tcp->isConnected = false;
                tcp->nextState = TCP_STATE_DISCONNECTED;
                break;
            }
            tcp->tcpPollCounter++;
            if (tcp->tcpPollCounter >= TCP_POLL_INTERVAL) {
                tcp->tcpPollCounter = 0;
                GBASIOTCPSocketReadPacketFromServer(tcp);
                while (peekTcpData(tcp->dataToSendToServerQueue)) {
                    struct TcpData* data = popTcpData(tcp->dataToSendToServerQueue);
                    GBASIOTCPSocketSendPacketToServer(tcp, data);
                    free(data);
                }
            }
            break;
        case TCP_STATE_DISCONNECTED:
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

                tcp->incomingDataBufferCount = 0;
                memset(tcp->incomingDataBuffer, 0, sizeof(tcp->incomingDataBuffer));
                tcp->isExpectingIncomingData = false;
            }
            else if (tcp->errorState != ERROR_STATE_NONE) {
                result = TCP_DATA_FAILURE | (tcp->errorState << 16);
                tcp->nextState = TCP_STATE_DISCONNECTED;
            }
            break;
        case TCP_STATE_CONNECTED:
            // receive incoming data from the GBA
            // break the inValue into two 16 bit integers, first one being a magic number, second one being the length of the incoming data
            uint16_t header = tcp->inValue & 0xFFFF;
            uint16_t length = (tcp->inValue >> 16) & 0xFFFF;
            if (!tcp->isExpectingIncomingData && header == TCP_RECEIVE_FROM_GBA) {
                tcp->isExpectingIncomingData = true;
                tcp->expectedIncomingBytesToRead = length;
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Expecting %d bytes of data from the GBA", length);
            }
            else if (tcp->isExpectingIncomingData) {
                // the incoming 32 bit value should be four 8 bit bytes of data
                mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Received the value %d from the GBA", tcp->inValue);
                
                // only copy the bytes that are expected, dont copy bytes beyond the expected length
                uint8_t bytesToCopy = tcp->expectedIncomingBytesToRead - tcp->incomingDataBufferCount;
                if (bytesToCopy > 4) {
                    bytesToCopy = 4;
                }
                memcpy(tcp->incomingDataBuffer + tcp->incomingDataBufferCount, (uint8_t*)&tcp->inValue, bytesToCopy);
                tcp->incomingDataBufferCount += bytesToCopy;
                if (tcp->incomingDataBufferCount >= tcp->expectedIncomingBytesToRead) {
                    mLOG(GBA_SIO, DEBUG, "TCP Socket Driver: Received %d bytes of data from the GBA", tcp->expectedIncomingBytesToRead);

                    // we have received all the expected data, send it to the server
                    TcpDataQueuePush(tcp->dataToSendToServerQueue, tcp->incomingDataBuffer, tcp->expectedIncomingBytesToRead);
                    tcp->isExpectingIncomingData = false;
                    tcp->incomingDataBufferCount = 0;
                    memset(tcp->incomingDataBuffer, 0, sizeof(tcp->incomingDataBuffer));
                }
            }

            // send data to the GBA from the server
            if (!tcp->currentDataToSendToGBA && peekTcpData(tcp->dataToSendToGBAQueue)) {
                tcp->currentDataToSendToGBA = popTcpData(tcp->dataToSendToGBAQueue);
                tcp->currentDataToSendToGBABytesSent = 0;
                if (tcp->currentDataToSendToGBA)
                {
                    result = TCP_SEND_TO_GBA | (tcp->currentDataToSendToGBA->numBytes << 16); // send the header to let the GBA know how many bytes to expect
                }
            }
            else if (tcp->currentDataToSendToGBA) {
                // get up to the next 4 bytes of data to send to the GBA
                uint32_t dataToSend = 0;
                for (int i = 0; i < 4; i++) {
                    if (tcp->currentDataToSendToGBABytesSent < tcp->currentDataToSendToGBA->numBytes) {
                        dataToSend |= (tcp->currentDataToSendToGBA->data[tcp->currentDataToSendToGBABytesSent] << (i * 8));
                        tcp->currentDataToSendToGBABytesSent++;
                    }
                }
                if (tcp->currentDataToSendToGBABytesSent >= tcp->currentDataToSendToGBA->numBytes) {
                    free(tcp->currentDataToSendToGBA);
                    tcp->currentDataToSendToGBA = NULL;
                }
                result = dataToSend;
            }
            break;
        default:
            break;
    }

    return result;
}