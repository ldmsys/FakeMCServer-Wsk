#pragma once

// WskMinecraft
#include <ntddk.h>
#include <wsk.h>

#ifndef htons
#define htons(x) (unsigned short)((x&0xFF00) >> 8|(x&0xFF) << 8)
#define ntohs(x) htons(x)
#define htonl(x) (unsigned long)((x&0xFF) << 24|(x&0xFF00) << 8|(x&0xFF0000) >> 8|(x&0xFF000000)>>24)
#define ntohl(x) htonl(x)
UINT64 htonll(UINT64 value)
{
    // Source: https://stackoverflow.com/questions/3022552
    // The answer is 42
    static const int num = 42;

    // Check the endianness
    if (*(const char*)(&num) == num)
    {
        const unsigned long high_part = htonl((unsigned long)(value >> 32));
        const unsigned long low_part = htonl((unsigned long)(value & 0xFFFFFFFFLL));

        return ((UINT64)(low_part) << 32) | high_part;
    }
    else
    {
        return value;
    }
}
#endif

#if 1
NTSTATUS WskMinecraftIRPComp(PDEVICE_OBJECT unused, PIRP irp, PVOID context) {
    UNREFERENCED_PARAMETER(unused); UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PRKEVENT)context, 2, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID WskMinecraftPrepareAwaitIRP(PIRP irp, PRKEVENT _event) {
    IoSetCompletionRoutine(irp, WskMinecraftIRPComp, _event, TRUE, TRUE, TRUE);
}

NTSTATUS WskMinecraftAwaitIRP(PIRP irp, PRKEVENT _event) {
    KeWaitForSingleObject(_event, Executive, KernelMode, FALSE, NULL);
    return irp->IoStatus.Status;
}
#else
// Usage: if (status == STATUS_PENDING) status = WskMinecraftAwaitIRP(irp);
NTSTATUS WskMinecraftAwaitIRP(PIRP irp) {
    KeWaitForSingleObject(irp->UserEvent, Executive, KernelMode, FALSE, NULL);
    return &irp->IoStatus && irp->IoStatus.Status || STATUS_UNSUCCESSFUL;
}
#endif

typedef struct _IOVEC {
    void* iov_base;
    size_t iov_len;
} IOVEC, *PIOVEC;

WSK_REGISTRATION WskMinecraftRegistration;
PWSK_SOCKET WskMinecraftListeningSocket;
WSK_PROVIDER_NPI wskProviderNpi;

SOCKADDR_IN6 ListenAddress = {AF_INET6,
							  htons(25565),
							  0, IN6ADDR_ANY_INIT, 0};


VOID NTAPI UnloadHandler(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS NTAPI PacketHandler(PWSK_SOCKET ListeningSocket);
NTSTATUS WSKAPI WskMinecraftAcceptEvent(
    _In_ PVOID SocketContext,
    _In_  ULONG         Flags,
    _In_  PSOCKADDR     LocalAddress,
    _In_  PSOCKADDR     RemoteAddress,
    _In_opt_  PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ PVOID* AcceptSocketContext,
    _Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH** AcceptSocketDispatch
);

const WSK_CLIENT_LISTEN_DISPATCH WskMinecraftClientListenDispatch = {
    WskMinecraftAcceptEvent,
    NULL, // WskInspectEvent is required only if conditional-accept is used.
    NULL  // WskAbortEvent is required only if conditional-accept is used.
};

const WSK_CLIENT_DISPATCH WskMinecraftClientDispatch = {
    MAKE_WSK_VERSION(1, 0), // This sample uses WSK version 1.0
    0, // Reserved
    NULL // WskClientEvent callback is not required in WSK version 1.0
};

#pragma region("Real Minecraft")
#define SEGMENT_BITS 0x7f
#define CONTINUE_BIT 0x80
#define KICKMODE 0x01
#define MTU 1500

size_t varintSize(unsigned char varint[4]) {
    int i;
    for (i = 0;i < 4;i++) if ((varint[i] & CONTINUE_BIT) == 0) break;
    return i + 1;
}

int varintToint(unsigned char varint[4]) {
    int value = 0;
    char currentByte;

    for (int i = 0;i < 4;i++) {
        currentByte = varint[i];
        value |= (currentByte & SEGMENT_BITS) << i * 7;

        if ((currentByte & CONTINUE_BIT) == 0) break;
    }

    return value;
}

size_t intTovarint(int data, unsigned char* varint) {
    int i;
    for (i = 0;i < 4;i++) {
        if ((data & ~SEGMENT_BITS) == 0) {
            varint[i] = (unsigned char)data;
            break;
        }

        varint[i] = (data & SEGMENT_BITS) | CONTINUE_BIT;

        data >>= 7;
    }
    return i + 1;
}

size_t appendLengthvarint(char* string, size_t length, char* mcstring) {
    unsigned char len[4];
    size_t headerlen = intTovarint((int)length, len);
    memcpy(mcstring, len, headerlen);
    memcpy(mcstring + headerlen, string, length);

    return headerlen + length;
}
#pragma endregion("Real Minecraft")