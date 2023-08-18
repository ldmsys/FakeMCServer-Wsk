#include "main.h"
//#include "main.tmh"
// Reference: https://github.com/microsoft/Windows-driver-samples/blob/main/network/wsk/echosrv/wsksmple.c


NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
	NTSTATUS status;
	WSK_CLIENT_NPI wskClientNpi;
	
	ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
	UNREFERENCED_PARAMETER(RegistryPath);

#ifndef NDEBUG
	//__debugbreak();
#endif

	status = STATUS_SUCCESS;

	wskClientNpi.ClientContext = NULL;
	wskClientNpi.Dispatch = &WskMinecraftClientDispatch;

	status = WskRegister(&wskClientNpi, &WskMinecraftRegistration);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = WskCaptureProviderNPI(&WskMinecraftRegistration, WSK_INFINITE_WAIT, &wskProviderNpi);

	if (!NT_SUCCESS(status)) {
		WskDeregister(&WskMinecraftRegistration);
		return status;
	}

	PKEVENT _evt;
	_evt = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), 'enim');
	if (!_evt) return STATUS_INSUFFICIENT_RESOURCES;
	KeInitializeEvent(_evt, SynchronizationEvent, FALSE);
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	WskMinecraftPrepareAwaitIRP(irp, _evt);

	WSK_EVENT_CALLBACK_CONTROL callbackControl;
	callbackControl.NpiId = (PNPIID)&NPI_WSK_INTERFACE_ID;
	callbackControl.EventMask = WSK_EVENT_ACCEPT;
	status = wskProviderNpi.Dispatch->WskControlClient(
		wskProviderNpi.Client,
		WSK_SET_STATIC_EVENT_CALLBACKS,
		sizeof(callbackControl),
		&callbackControl,
		0, NULL, NULL,
		NULL
	);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	// Fetch WskSocket
	wskProviderNpi.Dispatch->WskSocket(wskProviderNpi.Client,
		AF_INET6,
		SOCK_STREAM,
		IPPROTO_TCP,
		WSK_FLAG_LISTEN_SOCKET,
		NULL,
		&WskMinecraftClientListenDispatch,
		NULL, NULL, NULL,
		irp);
	
	status = WskMinecraftAwaitIRP(irp, _evt);
	
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WskMinecraftListeningSocket = (PWSK_SOCKET)irp->IoStatus.Information;

	// Set IPv6 Only to False
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, _evt);
	int zero = 0;

	((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskControlSocket(
		WskMinecraftListeningSocket,
		WskSetOption,
		IPV6_V6ONLY,
		IPPROTO_IPV6,
		sizeof(zero),
		&zero,
		0, NULL, NULL, irp
	);

	status = WskMinecraftAwaitIRP(irp, _evt);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	// Bind & Listen
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, _evt);
	
	status = ((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskBind(
		WskMinecraftListeningSocket, (PSOCKADDR)&ListenAddress, 0, irp
	);

	status = WskMinecraftAwaitIRP(irp, _evt);
	ASSERT(NT_SUCCESS(status));

	if (!NT_SUCCESS(status)) {
		return status;
	}

	HANDLE unused;
	PsCreateSystemThread(&unused, THREAD_ALL_ACCESS, NULL, NULL, NULL, WskMinecraftSocketBroker, NULL);

	IoFreeIrp(irp);
	ExFreePoolWithTag(_evt, 'enim');
	DriverObject->DriverUnload = UnloadHandler;
	return status;
}

VOID WskMinecraftSocketBroker(PVOID ctx) {
	UNREFERENCED_PARAMETER(ctx);
	HANDLE unused;
	PIRP irp = IoAllocateIrp(1, FALSE);
	
	WskMinecraftSocketBrokerEvent = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), 'enim');
	if (!WskMinecraftSocketBrokerEvent || !irp) return;
	KeInitializeEvent(WskMinecraftSocketBrokerEvent, SynchronizationEvent, FALSE);

	while (TRUE) {
		KeWaitForSingleObject(WskMinecraftSocketBrokerEvent, Executive, KernelMode, FALSE, NULL);
		if (WskMinecraftSocketBrokerSocket == NULL) break;
		else {
			PsCreateSystemThread(&unused, THREAD_ALL_ACCESS, NULL, NULL, NULL, PacketHandler, WskMinecraftSocketBrokerSocket);
		}
	}
}


VOID NTAPI UnloadHandler(_In_ PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);
	// TODO: Clean-up 

	//KEVENT evt;
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		return; STATUS_INSUFFICIENT_RESOURCES;
	}
	//WskMinecraftPrepareAwaitIRP(irp, &evt);

	((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskCloseSocket
	(WskMinecraftListeningSocket, irp);

	//WskMinecraftAwaitIRP(irp, &evt);
	WskDeregister(&WskMinecraftRegistration);

}

#pragma warning(disable: 4702)
NTSTATUS WSKAPI WskMinecraftAcceptEvent(
	_In_ PVOID SocketContext,
	_In_  ULONG         Flags,
	_In_  PSOCKADDR     LocalAddress,
	_In_  PSOCKADDR     RemoteAddress,
	_In_opt_  PWSK_SOCKET AcceptSocket,
	_Outptr_result_maybenull_ PVOID* AcceptSocketContext,
	_Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH** AcceptSocketDispatch
) {
	//KeBugCheckEx(0xC8C8C8C8, 0, 0, 0, 0);
	//return STATUS_UNSUCCESSFUL;
	SocketContext, Flags, LocalAddress, RemoteAddress, AcceptSocket, AcceptSocketContext, AcceptSocketDispatch;

	UNREFERENCED_PARAMETER(SocketContext); // Since we are passed it as NULL
	UNREFERENCED_PARAMETER(Flags), LocalAddress;

	if (!AcceptSocket) { 
		PIRP irp = IoAllocateIrp(1, FALSE);
		//((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)
		((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskCloseSocket
		(WskMinecraftListeningSocket, irp);
		IoFreeIrp(irp);
		return STATUS_REQUEST_NOT_ACCEPTED;
	}

	//PsCreateSystemThread(&threadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, PacketHandler, AcceptSocket);
	WskMinecraftSocketBrokerSocket = AcceptSocket;
	KeSetEvent(WskMinecraftSocketBrokerEvent, 2, FALSE);
	AcceptSocketContext = AcceptSocketDispatch = NULL;
	return STATUS_SUCCESS; // Nakdonggang river Duck Egg
}

NTSTATUS NTAPI WskMinecraftSendV(PWSK_SOCKET socket, PIOVEC iov, int iovcnt) {
	size_t mergedSize = 0;
	WSK_BUF actuallySends;
	unsigned char* buf_data;
	NTSTATUS status;
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) return STATUS_INSUFFICIENT_RESOURCES;
	PKEVENT _evt;
	_evt = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), 'enim');
	if (!_evt) return STATUS_INSUFFICIENT_RESOURCES;
	KeInitializeEvent(_evt, SynchronizationEvent, FALSE);

	for (int i = 0;i < iovcnt;i++)
		mergedSize += iov[i].iov_len;
	buf_data = ExAllocatePoolZero(NonPagedPool, mergedSize, 'pmet');
	if (!buf_data) return STATUS_INSUFFICIENT_RESOURCES;
	mergedSize = 0;
	for (int i = 0;i < iovcnt;i++) {
		memcpy(buf_data + mergedSize, iov[i].iov_base, iov[i].iov_len);
		mergedSize += iov[i].iov_len;
	}

	actuallySends.Offset = 0;
	actuallySends.Length = mergedSize;
	actuallySends.Mdl = IoAllocateMdl(buf_data, (ULONG)mergedSize, FALSE, FALSE, NULL);
	MmProbeAndLockPages(actuallySends.Mdl, KernelMode, IoWriteAccess);

	WskMinecraftPrepareAwaitIRP(irp, _evt);
	((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)->WskSend(
		socket,
		&actuallySends,
		0,
		irp
	);

	MmUnlockPages(actuallySends.Mdl);
	status = WskMinecraftAwaitIRP(irp, _evt);

	IoFreeMdl(actuallySends.Mdl);
	IoFreeIrp(irp);
	ExFreePoolWithTag(_evt, 'enim');
	ExFreePoolWithTag(buf_data, 'pmet');

	return -!!(NT_SUCCESS(status));
}

int NTAPI WskMinecraftRecv(PWSK_SOCKET socket, PVOID buf, size_t len, ULONG Flags) {
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) return STATUS_INSUFFICIENT_RESOURCES;
	WSK_BUF bufwrapper;
	PKEVENT _evt;
	int received_bytes = 0;
	_evt = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), 'enim');
	if (!_evt) return STATUS_INSUFFICIENT_RESOURCES;
	KeInitializeEvent(_evt, SynchronizationEvent, FALSE);
	NTSTATUS status;

	bufwrapper.Offset = 0;
	bufwrapper.Length = len;
	bufwrapper.Mdl = IoAllocateMdl(buf, (ULONG)len, FALSE, FALSE, NULL);
	MmProbeAndLockPages(bufwrapper.Mdl, KernelMode, IoWriteAccess);

	WskMinecraftPrepareAwaitIRP(irp, _evt);
	((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)->WskReceive(
		socket,
		&bufwrapper,
		Flags,
		irp
	);

	MmUnlockPages(bufwrapper.Mdl);
	status = WskMinecraftAwaitIRP(irp, _evt);

	if (NT_SUCCESS(status)) {
		received_bytes = (int)irp->IoStatus.Information;
	}

	IoFreeMdl(bufwrapper.Mdl);
	IoFreeIrp(irp);
	ExFreePoolWithTag(_evt, 'enim');

	return NT_SUCCESS(status) ? received_bytes : -1;
}

NTSTATUS NTAPI PacketHandler(PVOID ctx) {
	LARGE_INTEGER _1000;
	_1000.QuadPart = 1000;
	KeDelayExecutionThread(KernelMode, FALSE, &_1000);
	PWSK_SOCKET AcceptSocket = ctx;
	NTSTATUS status;
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) return STATUS_INSUFFICIENT_RESOURCES;
	PKEVENT _evt;
	_evt = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), 'enim');
	KeInitializeEvent(_evt, SynchronizationEvent, FALSE);
	if (!_evt) return STATUS_INSUFFICIENT_RESOURCES;

	/********************************/
	//__debugbreak();
	char m[1024];
	int nbytes = WskMinecraftRecv(AcceptSocket, m, 1024, 0);
	DbgPrint("%d",nbytes);

	char HelloWorld[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nHello, World!\r\n";
	IOVEC test[2];
	test[0].iov_len = sizeof(HelloWorld) - 1;
	test[0].iov_base = HelloWorld;

	status = WskMinecraftSendV(AcceptSocket, test, 1);

	/********************************/
	WskMinecraftPrepareAwaitIRP(irp, _evt);
	((PWSK_PROVIDER_CONNECTION_DISPATCH)AcceptSocket->Dispatch)->WskDisconnect(
		AcceptSocket,
		NULL,
		0,
		irp
	);
	status = WskMinecraftAwaitIRP(irp, _evt);

	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, _evt);

	((PWSK_PROVIDER_CONNECTION_DISPATCH)AcceptSocket->Dispatch)->WskCloseSocket(
		AcceptSocket,
		irp
	);
	status = WskMinecraftAwaitIRP(irp, _evt);

	IoFreeIrp(irp);
	ExFreePoolWithTag(_evt, 'enim');
	return status;
}