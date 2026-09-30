
#include <ntddk.h>
#include <ntdddisk.h>
#include <scsi.h>
#include <intrin.h>
#include "defs.h"

PDRIVER_DISPATCH g_original_device_control;

void spoof_serial(char* serial, bool is_smart);
unsigned long long g_startup_time;

struct REQUEST_STRUCT
{
	PIO_COMPLETION_ROUTINE OldRoutine;
	PVOID OldContext;
	ULONG OutputBufferLength;
	PVOID SystemBuffer;
};

NTSTATUS completed_storage_query(
	PDEVICE_OBJECT device_object,
	PIRP irp,
	PVOID context
)
{
	if(!context)
	{
		KdPrint(("%s %d : Context was nullptr\n", __FUNCTION__, __LINE__));
		return STATUS_SUCCESS;
	}

	const auto request = (REQUEST_STRUCT*)context;
	const auto buffer_length = request->OutputBufferLength;
	const auto buffer = (PSTORAGE_DEVICE_DESCRIPTOR)request->SystemBuffer;
	const auto old_routine = request->OldRoutine;
	const auto old_context = request->OldContext;
	ExFreePool(context);

	do
	{

		if(buffer_length < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties))
			break;	// They cannot think here stright 

		if(buffer->SerialNumberOffset == 0)
		{
			KdPrint(("%s %d : Device doesn't have unique ID\n", __FUNCTION__, __LINE__));
			break;
		}

		if(buffer_length < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) + buffer->RawPropertiesLength
			|| buffer->SerialNumberOffset < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties)
			|| buffer->SerialNumberOffset >= buffer_length
			)
		{
			KdPrint(("%s %d : Malformed buffer (should never happen) size: %d\n", __FUNCTION__, __LINE__, buffer_length));
		}
		else
		{
			const auto serial = (char*)buffer + buffer->SerialNumberOffset;
			KdPrint(("%s %d : Serial0: %s\n", __FUNCTION__, __LINE__, serial));
			spoof_serial(serial, false);
			KdPrint(("%s %d : Serial1: %s\n", __FUNCTION__, __LINE__, serial));
		}
	} while(false);

	// Call next completion routine (if any)
	if(irp->StackCount > 1ul && old_routine)
		return old_routine(device_object, irp, old_context);

	return STATUS_SUCCESS;
}

NTSTATUS completed_smart(
	PDEVICE_OBJECT device_object,
	PIRP irp,
	PVOID context
)
{
	UNREFERENCED_PARAMETER(device_object);

	if(!context)
	{
		KdPrint(("%s %d : Context was nullptr\n", __FUNCTION__, __LINE__));
		return STATUS_SUCCESS;
	}

	const auto request = (REQUEST_STRUCT*)context;
	const auto buffer_length = request->OutputBufferLength;
	const auto buffer = (SENDCMDOUTPARAMS*)request->SystemBuffer;
	ExFreePool(context);

	if(buffer_length < FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer)
		|| FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer) + buffer->cBufferSize > buffer_length
		|| buffer->cBufferSize < sizeof(IDINFO)
		)
	{
		KdPrint(("%s %d : Malformed buffer (should never happen) size: %d\n", __FUNCTION__, __LINE__, buffer_length));
	}
	else
	{
		const auto info = (IDINFO*)buffer->bBuffer;
		const auto serial = info->sSerialNumber;
		KdPrint(("%s %d : Serial0: %s\n", __FUNCTION__, __LINE__, serial));
		spoof_serial(serial, true);
		KdPrint(("%s %d : Serial1: %s\n", __FUNCTION__, __LINE__, serial));
	}

	return irp->IoStatus.Status;
}

void do_completion_hook(PIRP irp, PIO_STACK_LOCATION ioc, PIO_COMPLETION_ROUTINE routine)
{
	// [REMOVIDO PARA ESTUDO]
	// Dica: uma completion routine roda quando o IRP volta subindo a pilha.
	// Aqui é preciso:
	//   1. setar ioc->Control com SL_INVOKE_ON_SUCCESS;
	//   2. salvar a CompletionRoutine/Context originais;
	//   3. guardar OutputBufferLength e irp->AssociatedIrp.SystemBuffer
	//      (o IRP é METHOD_BUFFERED, o buffer vive no SystemBuffer);
	//   4. trocar ioc->CompletionRoutine pela rotina passada em `routine`,
	//      passando os dados salvos via ioc->Context.
	KdPrint(("do_completion_hook: exercício deixado para o leitor\n"));
}

NTSTATUS hooked_device_control(PDEVICE_OBJECT device_object, PIRP irp)
{
	const auto ioc = IoGetCurrentIrpStackLocation(irp);

	// [REMOVIDO PARA ESTUDO]
	// Dica: inspecione ioc->Parameters.DeviceIoControl.IoControlCode.
	// Para IOCTL_STORAGE_QUERY_PROPERTY com query->PropertyId ==
	// StorageDeviceProperty (o query é o irp->AssociatedIrp.SystemBuffer,
	// castado para PSTORAGE_PROPERTY_QUERY), registre completed_storage_query.
	// Para SMART_RCV_DRIVE_DATA, registre completed_smart.
	// Use do_completion_hook e depois encadeie para o handler original.
	KdPrint(("hooked_device_control: exercício deixado para o leitor\n"));

	return g_original_device_control(device_object, irp);
}

void apply_hook()
{
	// [REMOVIDO PARA ESTUDO]
	// Dica: use ObReferenceObjectByName (função não documentada do kernel)
	// para obter o DRIVER_OBJECT de L"\\Driver\\Disk". Guarde a entrada
	// MajorFunction[IRP_MJ_DEVICE_CONTROL] em g_original_device_control e
	// substitua-a por hooked_device_control. Lembre do ObDereferenceObject.
	KdPrint(("apply_hook: exercício deixado para o leitor\n"));
}


extern "C"
NTSTATUS EntryPoint(
	_DRIVER_OBJECT *DriverObject,
	_UNICODE_STRING *RegistryPath
)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	KeQuerySystemTime(&g_startup_time);
	apply_hook();
	return STATUS_SUCCESS;
}
