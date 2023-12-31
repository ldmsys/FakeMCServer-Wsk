#include "main.h"
//#include "main.tmh"
// Reference: https://github.com/microsoft/Windows-driver-samples/blob/main/network/wsk/echosrv/wsksmple.c
// Reference: https://github.com/wbenny/KSOCKET

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

	WskMinecraftRegistration = ExAllocatePoolZero(NonPagedPool, sizeof(WSK_REGISTRATION), 'enim');
	if (!WskMinecraftRegistration) return STATUS_INSUFFICIENT_RESOURCES;
	status = WskRegister(&wskClientNpi, WskMinecraftRegistration);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = WskCaptureProviderNPI(WskMinecraftRegistration, WSK_INFINITE_WAIT, &wskProviderNpi);

	if (!NT_SUCCESS(status)) {
		WskDeregister(WskMinecraftRegistration);
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

	// Set TCP_NODELAY to disable nagle algorithm
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, _evt);
	int yes = 1;

	((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskControlSocket(
		WskMinecraftListeningSocket,
		WskSetOption,
		TCP_NODELAY,
		IPPROTO_TCP,
		sizeof(yes),
		&yes,
		0, NULL, NULL, irp
	);

	status = WskMinecraftAwaitIRP(irp, _evt);
	if (!NT_SUCCESS(status)) {
		return status;
	}/**/


	// Fetch Registry Information
	PHANDLE RegHandle = NULL;
	PKEY_VALUE_PARTIAL_INFORMATION ReturnData;
	UNICODE_STRING ValueName;
	ReturnData = ExAllocatePoolZero(NonPagedPool, 32767 + sizeof(KEY_VALUE_PARTIAL_INFORMATION), 'enim');
	
	//__debugbreak();
	do {
		if (!RegistryPath || !ReturnData) break;

		OBJECT_ATTRIBUTES RegOpenAttributes;
		InitializeObjectAttributes(&RegOpenAttributes, RegistryPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

		RegHandle = ExAllocatePoolZero(NonPagedPool, sizeof(HANDLE), 'enim');
		if (!RegHandle) break;

		status = ZwOpenKey(RegHandle, KEY_READ, &RegOpenAttributes);
		if (!NT_SUCCESS(status)) break;

		ULONG ResultLen;

		RtlInitUnicodeString(&ValueName, L"Port");
		status = ZwQueryValueKey(*RegHandle, &ValueName, KeyValuePartialInformation, ReturnData, 32767 + sizeof(KEY_VALUE_PARTIAL_INFORMATION), &ResultLen);
		if (!NT_SUCCESS(status)) break;

		if (ResultLen > 0 && ReturnData->Type == REG_DWORD && ReturnData->DataLength == sizeof(ULONG)) {
			PORT = *((unsigned short*)(void*)ReturnData->Data);
		}
		RtlFillMemory(ReturnData, 32767 + sizeof(KEY_VALUE_PARTIAL_INFORMATION), 0);

		RtlInitUnicodeString(&ValueName, L"MotdJSON");
		status = ZwQueryValueKey(*RegHandle, &ValueName, KeyValuePartialInformation, ReturnData, 32767 + sizeof(KEY_VALUE_PARTIAL_INFORMATION), &ResultLen);
		if (!NT_SUCCESS(status)) break;

		if (ResultLen > 0 && ReturnData->Type == REG_BINARY && ReturnData->DataLength <= 32767) {
			// No NULL-terminated string expected
			motdJSON = ExAllocatePoolZero(NonPagedPool, ReturnData->DataLength + 1, 'enim');
			memcpy(motdJSON, ReturnData->Data, ReturnData->DataLength);
			motdJSON[ReturnData->DataLength] = '\0';
		}

		RtlInitUnicodeString(&ValueName, L"KickJSON");
		status = ZwQueryValueKey(*RegHandle, &ValueName, KeyValuePartialInformation, ReturnData, 32767 + sizeof(KEY_VALUE_PARTIAL_INFORMATION), &ResultLen);
		if (!NT_SUCCESS(status)) break;

		if (ResultLen > 0 && ReturnData->Type == REG_BINARY && ReturnData->DataLength <= 32767) {
			// No NULL-terminated string expected
			kickJSON = ExAllocatePoolZero(NonPagedPool, ReturnData->DataLength + 1, 'enim');
			memcpy(kickJSON, ReturnData->Data, ReturnData->DataLength);
			kickJSON[ReturnData->DataLength] = '\0';
		}

	} while (0);

	if (RegHandle) {
		ZwClose(RegHandle);
		ExFreePoolWithTag(RegHandle, 'enim');
	}

	if (!motdJSON) motdJSON = defaultMotdJSON;
	if (!kickJSON) kickJSON = defaultKickJSON;

	status = STATUS_SUCCESS; // Since there is fallback, failing to fetch Registry isn't critical, huh?

	// TODO: Create Ioctl Device
	UNICODE_STRING IoctlDeviceName, IoctlAliasName;
	RtlInitUnicodeString(&IoctlDeviceName, L"\\Device\\MCServer");
	RtlInitUnicodeString(&IoctlAliasName, L"\\DosDevices\\MCServer");

	status = IoCreateDevice(DriverObject, 0, &IoctlDeviceName, FILE_DEVICE_UNKNOWN, 0, TRUE, &IoctlDeviceObject);
	if (!NT_SUCCESS(status) || !IoctlDeviceObject) {
		return status;
	}

	status = IoCreateSymbolicLink(&IoctlAliasName, &IoctlDeviceName);
	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(IoctlDeviceObject);
		return status;
	}

	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IoctlHandler;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = IoctlCreateCloseHandler;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = IoctlCreateCloseHandler;

	// Bind & Listen
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);
	WskMinecraftPrepareAwaitIRP(irp, _evt);
	
	if (!PORT) PORT = 25565;
	SOCKADDR_IN6 ListenAddress = { AF_INET6,
							  htons(PORT),
							  0, IN6ADDR_ANY_INIT, 0 };

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

NTSTATUS NTAPI IoctlCreateCloseHandler(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP irp) {
#ifndef NDEBUG
	__debugbreak();
#endif
	UNREFERENCED_PARAMETER(DeviceObject);
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI IoctlHandler(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP irp) {
#ifndef NDEBUG
	__debugbreak();
#endif
	NTSTATUS returnStatus = STATUS_SUCCESS;
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION stackLoc;
	ULONG IoctlCode;

	if (!irp) return STATUS_INVALID_PARAMETER;

	stackLoc = IoGetCurrentIrpStackLocation(irp);
	if (!stackLoc || stackLoc->MajorFunction != IRP_MJ_DEVICE_CONTROL) {
		irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_UNSUCCESSFUL;
	}

	IoctlCode = stackLoc->Parameters.DeviceIoControl.IoControlCode;

	switch (IoctlCode) {
	case IOCTL_FAKEMCSERVER_UPDATE_PORT:
		returnStatus = STATUS_NOT_IMPLEMENTED;
		break;
	case IOCTL_FAKEMCSERVER_UPDATE_MOTD:
		if (stackLoc->Parameters.DeviceIoControl.InputBufferLength < 32767) {
			if (motdJSON != defaultMotdJSON) ExFreePoolWithTag(motdJSON, 'enim');

			motdJSON = ExAllocatePoolZero(NonPagedPool, stackLoc->Parameters.DeviceIoControl.InputBufferLength + 1, 'enim');
			memcpy(motdJSON, irp->UserBuffer, stackLoc->Parameters.DeviceIoControl.InputBufferLength);
			motdJSON[stackLoc->Parameters.DeviceIoControl.InputBufferLength] = '\0';

			irp->IoStatus.Information = (ULONG_PTR)motdJSON; // debugging purposes only
		}
		break;
	case IOCTL_FAKEMCSERVER_UPDATE_KICK:
		if (stackLoc->Parameters.DeviceIoControl.InputBufferLength < 32767) {
			if (kickJSON != defaultKickJSON) ExFreePoolWithTag(kickJSON, 'enim');

			kickJSON = ExAllocatePoolZero(NonPagedPool, stackLoc->Parameters.DeviceIoControl.InputBufferLength + 1, 'enim');
			memcpy(kickJSON, irp->UserBuffer, stackLoc->Parameters.DeviceIoControl.InputBufferLength);
			kickJSON[stackLoc->Parameters.DeviceIoControl.InputBufferLength] = '\0';

			irp->IoStatus.Information = (ULONG_PTR)kickJSON; // debugging purposes only
		}
		break;
	default:
		returnStatus = STATUS_INVALID_PARAMETER;
	}

	irp->IoStatus.Status = returnStatus;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI UnloadHandler(_In_ PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);
	// TODO: Clean-up 

	//__debugbreak();
	PKEVENT _evt;
	_evt = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), 'enim');
	if (!_evt) return STATUS_INSUFFICIENT_RESOURCES;
	KeInitializeEvent(_evt, SynchronizationEvent, FALSE);

	PIRP irp = IoAllocateIrp(1, FALSE);
	if (!irp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	WskMinecraftPrepareAwaitIRP(irp, _evt);

	WskMinecraftSocketBrokerSocket = NULL;
	KeSetEvent(WskMinecraftSocketBrokerEvent, 2, FALSE);

	((PWSK_PROVIDER_LISTEN_DISPATCH)WskMinecraftListeningSocket->Dispatch)->WskCloseSocket
	(WskMinecraftListeningSocket, irp);

	UNICODE_STRING IoctlAliasName;
	RtlInitUnicodeString(&IoctlAliasName, L"\\DosDevices\\MCServer");
	IoDeleteSymbolicLink(&IoctlAliasName);
	IoDeleteDevice(IoctlDeviceObject);

	WskMinecraftAwaitIRP(irp, _evt);
	WskReleaseProviderNPI(WskMinecraftRegistration);
	WskDeregister(WskMinecraftRegistration);
	IoFreeIrp(irp);
	if (motdJSON != defaultMotdJSON) ExFreePoolWithTag(motdJSON, 'enim');
	if (kickJSON != defaultKickJSON) ExFreePoolWithTag(kickJSON, 'enim');
	ExFreePoolWithTag(WskMinecraftRegistration, 'enim');
	ExFreePoolWithTag(_evt, 'enim');

	return STATUS_SUCCESS;
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
	if (ConnectionCount > MAXIMUM_CLIENT)
		return STATUS_REQUEST_NOT_ACCEPTED;

	//PsCreateSystemThread(&threadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, PacketHandler, AcceptSocket);
	WskMinecraftSocketBrokerSocket = AcceptSocket;
	KeSetEvent(WskMinecraftSocketBrokerEvent, 2, FALSE);
	AcceptSocketContext = AcceptSocketDispatch = NULL;
	return STATUS_SUCCESS; // Nakdonggang river Duck Egg
}

NTSTATUS NTAPI WskMinecraftWriteV(PWSK_SOCKET socket, PIOVEC iov, int iovcnt) {
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
	if (!_evt) return STATUS_INSUFFICIENT_RESOURCES;
	KeInitializeEvent(_evt, SynchronizationEvent, FALSE);
	

	ConnectionCount++;
	int yes = 1;

	WskMinecraftPrepareAwaitIRP(irp, _evt);
	((PWSK_PROVIDER_LISTEN_DISPATCH)AcceptSocket->Dispatch)->WskControlSocket(
		AcceptSocket,
		WskSetOption,
		TCP_NODELAY,
		IPPROTO_TCP,
		sizeof(yes),
		&yes,
		0, NULL, NULL, irp
	);

	status = WskMinecraftAwaitIRP(irp, _evt);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

	/********************************/
	//__debugbreak();
	/*char m[1024];
	int nbytes = WskMinecraftRecv(AcceptSocket, m, 1024, 0);
	DbgPrint("%d",nbytes);

	char HelloWorld[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nHello, World!\r\n";
	IOVEC test[2];
	test[0].iov_len = sizeof(HelloWorld) - 1;
	test[0].iov_base = HelloWorld;

	status = WskMinecraftWriteV(AcceptSocket, test, 1);*/

	unsigned char buf[MTU], returnbuf[MTU], yebibuf[MTU];
	int n, dn, mode, expected_packet_size, keepalivecounter, yebiopt;
	n = dn = mode = expected_packet_size = keepalivecounter = yebiopt = 0;
	IOVEC vec[10];
	while (TRUE) {
		//__debugbreak();
		if (yebiopt) {
			PRINTF_DEBUG("Restoring from yebi! %d\n", yebiopt);
			n = yebiopt;
			memset(&buf, 0, MTU);
			memcpy(buf, yebibuf, yebiopt);
			yebiopt = 0;
		}
		else {
			PRINTF_DEBUG("Reading... %d\n", mode);
			dn = WskMinecraftRecv(AcceptSocket, buf + n, 1500 - n, 0);
			if (dn <= 0) break;
			n += dn;
			PRINTF_DEBUG("Read! +%d=%d\n", dn, n);
		}

		if (expected_packet_size == 0) {
			expected_packet_size = varintToint(buf);
			PRINTF_DEBUG("Expected packet size: %d\n", expected_packet_size);
		}
		if (expected_packet_size == 0) continue;
	
		if (n < expected_packet_size + varintSize(buf)) {
			PRINTF_DEBUG("Johnbeo! %d < %d\n", n, expected_packet_size + varintSize(buf));
			continue;
		}
		else if (n > expected_packet_size + varintSize(buf)) {
			PRINTF_DEBUG("Moving! %d > %d\n", n, expected_packet_size + varintSize(buf));
			memset(yebibuf, 0, MTU);
			memcpy(yebibuf, buf + expected_packet_size + varintSize(buf), n - expected_packet_size);
			yebiopt = n - expected_packet_size - (int)varintSize(buf);
			n = expected_packet_size + (int)varintSize(buf);
		}

		memset(returnbuf, 0, MTU);
		unsigned char packetID = buf[varintSize(buf)];
		PRINTF_DEBUG("Packet done! %x %x %x %x %x (%d, packetID %d, mode %d)\n", buf[0], buf[1], buf[2], buf[3], buf[4], varintSize(buf), packetID, mode);
		if (mode == 0) { // pre-handshaking
			//int protocol = varintToint(buf + varintSize(buf) + 1);
			mode = buf[expected_packet_size]; // new mode
			PRINTF_DEBUG("Handshaking! %d\n", mode);
			if (packetID != 0 || mode > 2 || mode == 0) {
				// Undefined behavior
				PRINTF_DEBUG("Undefined behavior! %d %d\n", packetID, mode);
				break;
			}

		}
		else if (mode == 1) { // Ping Mode
			PRINTF_DEBUG("Ping Mode!\n");
			if (packetID == 0) { // MOTD Request
				//char* packedJSON = (char*)malloc(strlen(json) + 6);
				char* packedJSON = ExAllocatePoolZero(NonPagedPool, strlen(motdJSON) + 6, 'enim');
				if (!packedJSON) break;
				unsigned char _packetID = 0x00;

				vec[1].iov_base = &_packetID;
				vec[1].iov_len = 1;
				vec[2].iov_base = packedJSON;
				vec[2].iov_len = appendLengthvarint(motdJSON, strlen(motdJSON), packedJSON);

				size_t payloadsize = 0;
				for (int i = 1;i <= 2;i++) {
					payloadsize += vec[i].iov_len;
				}
				unsigned char payloadsize_varint[4];
				vec[0].iov_base = payloadsize_varint;
				vec[0].iov_len = intTovarint((int)payloadsize, payloadsize_varint);

				WskMinecraftWriteV(AcceptSocket, vec, 3);
				ExFreePoolWithTag(packedJSON, 'enim');
			}
			else if (packetID == 1) { // Ping Request
				vec[0].iov_base = buf;
				vec[0].iov_len = expected_packet_size + varintSize(buf);

				WskMinecraftWriteV(AcceptSocket, vec, 1); // Send back the full packet
			}
		}
		else if (mode == 2) {
			if (!packetID == 0) {
				PRINTF_DEBUG("Undefined behavior! %d %d\n", packetID, mode);
				break;
			}
			//char* packedJSON = (char*)malloc(strlen(json) + 6);
			char* packedJSON = ExAllocatePoolZero(NonPagedPool, strlen(kickJSON) + 6, 'enim');
			if (!packedJSON) break;
			unsigned char _packetID = 0x00;

			vec[1].iov_base = &_packetID;
			vec[1].iov_len = 1;
			vec[2].iov_base = packedJSON;
			vec[2].iov_len = appendLengthvarint(kickJSON, strlen(kickJSON), packedJSON);

			size_t payloadsize = 0;
			for (int i = 1;i < 3;i++) {
				payloadsize += vec[i].iov_len;
			}
			unsigned char payloadsize_varint[4];
			vec[0].iov_base = payloadsize_varint;
			vec[0].iov_len = intTovarint((int)payloadsize, payloadsize_varint);

			WskMinecraftWriteV(AcceptSocket, vec, 3);
			ExFreePoolWithTag(packedJSON, 'enim');
		}
		n = expected_packet_size = 0;
		memset(&buf, 0, MTU);
	}
	PRINTF_DEBUG("Disconnecting!\n");

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
	ConnectionCount--;
	return status;
}