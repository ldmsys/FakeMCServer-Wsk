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

	KEVENT evt;
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	WskMinecraftPrepareAwaitIRP(irp, &evt);

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
	
	status = WskMinecraftAwaitIRP(irp, &evt);
	
	if (!NT_SUCCESS(status)) {
		WskDeregister(&WskMinecraftRegistration);
		return status;
	}

	WskMinecraftListeningSocket = (PWSK_SOCKET)irp->IoStatus.Information;

	// Set IPv6 Only to False
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, &evt);
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

	status = WskMinecraftAwaitIRP(irp, &evt);

	if (!NT_SUCCESS(status)) {
		WskDeregister(&WskMinecraftRegistration);
		return status;
	}

	// Bind & Listen
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, &evt);
	
	status = ((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskBind(
		WskMinecraftListeningSocket, (PSOCKADDR)&ListenAddress, 0, irp
	);

	status = WskMinecraftAwaitIRP(irp, &evt);
	ASSERT(NT_SUCCESS(status));

	if (!NT_SUCCESS(status)) {
		WskDeregister(&WskMinecraftRegistration);
		return status;
	}



	PacketHandler(WskMinecraftListeningSocket); // fixme: Should be async


	IoFreeIrp(irp);
	DriverObject->DriverUnload = UnloadHandler;
	return status;
}

NTSTATUS NTAPI PacketHandler(PWSK_SOCKET ListeningSocket) {
	PWSK_PROVIDER_LISTEN_DISPATCH ListenDispatch;
	SOCKADDR LocalAddress, RemoteAddress;
	KEVENT evt;
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		return STATUS_INSUFFICIENT_RESOURCES; // Better safe than sorry
	}
	WskMinecraftPrepareAwaitIRP(irp, &evt);
	ListenDispatch = (PWSK_PROVIDER_LISTEN_DISPATCH)(ListeningSocket->Dispatch);

	ListenDispatch->WskAccept(ListeningSocket, 0, NULL, NULL, &LocalAddress, &RemoteAddress, irp);
	NTSTATUS status = WskMinecraftAwaitIRP(irp, &evt);
	
	ASSERT(NT_SUCCESS(status));
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	PWSK_SOCKET AcceptSocket = (PWSK_SOCKET)irp->IoStatus.Information;

	__debugbreak();
	if (!AcceptSocket) return STATUS_REQUEST_NOT_ACCEPTED;

	char HelloWorld[] = "HTTP/1.1 200 OK\r\n\r\nHello, World!\r\n";

	status = STATUS_SUCCESS;
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WSK_BUF buftest;
	WskMinecraftPrepareAwaitIRP(irp, &evt);

	buftest.Offset = 0;
	buftest.Length = strlen(HelloWorld);
	buftest.Mdl = IoAllocateMdl(HelloWorld, (ULONG)buftest.Length, FALSE, FALSE, NULL); // Because status for IRP couldn't checked


	((PWSK_PROVIDER_CONNECTION_DISPATCH)AcceptSocket->Dispatch)->WskSend(
		AcceptSocket,
		&buftest,
		0,
		irp
	);

	status = WskMinecraftAwaitIRP(irp, &evt);

	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, &evt);

	((PWSK_PROVIDER_CONNECTION_DISPATCH)AcceptSocket->Dispatch)->WskCloseSocket(
		AcceptSocket,
		irp
	); WskMinecraftAwaitIRP(irp, &evt);
	// According to MS docs, WskCloseSocket never fail, so we won't trace irp.

	IoFreeIrp(irp);

	return STATUS_SUCCESS;
}

VOID NTAPI UnloadHandler(_In_ PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);
	// TODO: Clean-up 

	KEVENT evt;
	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		return; STATUS_INSUFFICIENT_RESOURCES;
	}
	WskMinecraftPrepareAwaitIRP(irp, &evt);

	((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskCloseSocket
	(WskMinecraftListeningSocket, irp);

	WskMinecraftAwaitIRP(irp, &evt);
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
	KeBugCheckEx(0xC8C8C8C8, 0, 0, 0, 0);
	return STATUS_UNSUCCESSFUL;
	SocketContext, Flags, LocalAddress, RemoteAddress, AcceptSocket, AcceptSocketContext, AcceptSocketDispatch;

	UNREFERENCED_PARAMETER(SocketContext); // Since we are passed it as NULL
	UNREFERENCED_PARAMETER(Flags), LocalAddress;

	__debugbreak();
	if (!AcceptSocket) return STATUS_REQUEST_NOT_ACCEPTED;

	char HelloWorld[] = "HTTP/1.1 200 OK\r\n\r\nHello, World!\r\n";

	NTSTATUS status = STATUS_SUCCESS;
	KEVENT evt;
	PIRP irp = IoAllocateIrp(1, FALSE);
	WSK_BUF buftest;
	if (!irp) {
		return STATUS_INSUFFICIENT_RESOURCES; // Better safe than sorry
	}
	WskMinecraftPrepareAwaitIRP(irp, &evt);

	buftest.Offset = 0;
	buftest.Length = strlen(HelloWorld);
	buftest.Mdl = IoAllocateMdl(HelloWorld, (ULONG)buftest.Length, FALSE, FALSE, NULL); // Because status for IRP couldn't checked

	
	((PWSK_PROVIDER_CONNECTION_DISPATCH)AcceptSocket->Dispatch)->WskSend(
		AcceptSocket,
		&buftest,
		0,
		irp
	);

	status = WskMinecraftAwaitIRP(irp, &evt);

	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, &evt);
	
	((PWSK_PROVIDER_CONNECTION_DISPATCH)AcceptSocket->Dispatch)->WskCloseSocket(
		AcceptSocket,
		irp
	); WskMinecraftAwaitIRP(irp, &evt);
	// According to MS docs, WskCloseSocket never fail, so we won't trace irp.

	IoFreeIrp(irp);
	AcceptSocketContext = AcceptSocketDispatch = NULL;
	return status;
}

//NTSTATUS NTAPI WskMinecraftSendV(PWSK_SOCKET sock, )