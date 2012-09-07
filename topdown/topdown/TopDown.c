//---------------------------------------------------------------------------
//
// Includes
//  
//---------------------------------------------------------------------------
//#include <ntddk.h>
#include <ntifs.h>
#include "TopDownIo.h"

//---------------------------------------------------------------------------
//
// Forward declaration
//  
//---------------------------------------------------------------------------
DRIVER_UNLOAD UnloadDriver;
DRIVER_DISPATCH DispatchCreateClose;
DRIVER_DISPATCH DispatchIoctl;

void UnloadDriver(
    PDRIVER_OBJECT DriverObject
    );
NTSTATUS DispatchCreateClose(
    IN PDEVICE_OBJECT DeviceObject, 
    IN PIRP Irp
    );
NTSTATUS DispatchIoctl(
    IN PDEVICE_OBJECT DeviceObject, 
    IN PIRP Irp
    );
//
// Process function callback
//  
VOID ProcessCallback(
    IN HANDLE  hParentId, 
    IN HANDLE  hProcessId, 
    IN BOOLEAN bCreate
    );


typedef struct _ImageFileEntry
{
    UNICODE_STRING Filename;
    LIST_ENTRY Entry;
} IMAGEFILE_ENTRY, *PIMAGEFILE_ENTRY;

IMAGEFILE_ENTRY g_ImageFileHead;
//
// Structure for process callback information
//
typedef struct _ProcessCallbackInfo
{
    HANDLE  hParentId;
    HANDLE  hProcessId;
    BOOLEAN bCreate;
} PROCESS_CALLBACK_INFO, *PPROCESS_CALLBACK_INFO;

//
// Private storage for process retreiving 
//
typedef struct _DEVICE_EXTENSION 
{
    PDEVICE_OBJECT DeviceObject;
    //
    // Shared section
    //
    HANDLE  hProcessId;
    //
    // Process section data
    //
    HANDLE  hParentId;
    BOOLEAN bCreate;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

//
// Global variables
//
PDEVICE_OBJECT g_pDeviceObject;
TOPDOWN_ACTIVATE_INFO  g_ActivateInfo;

typedef NTSTATUS (*QUERY_INFO_PROCESS) (
    __in HANDLE ProcessHandle,
    __in PROCESSINFOCLASS ProcessInformationClass,
    __out_bcount(ProcessInformationLength) PVOID ProcessInformation,
    __in ULONG ProcessInformationLength,
    __out_opt PULONG ReturnLength
    );

// implementation from
// http://www.osronline.com/article.cfm?article=472
QUERY_INFO_PROCESS ZwQueryInformationProcess;

NTSTATUS GetProcessImageName(HANDLE hProcess, PUNICODE_STRING ProcessImageName)
{
    NTSTATUS status;
    ULONG returnedLength;
    ULONG bufferLength;
    PVOID buffer;
    PUNICODE_STRING imageName;
   
    PAGED_CODE(); // this eliminates the possibility of the IDLE Thread/Process

    if (NULL == ZwQueryInformationProcess) {
        UNICODE_STRING routineName;
        RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");
        ZwQueryInformationProcess =
               (QUERY_INFO_PROCESS) MmGetSystemRoutineAddress(&routineName);

        if (NULL == ZwQueryInformationProcess) {
            DbgPrint("Cannot resolve ZwQueryInformationProcess\n");
        }
    }
    //
    // Step one - get the size we need
    //
    status = ZwQueryInformationProcess( hProcess,
                                        ProcessImageFileName,
                                        NULL, // buffer
                                        0, // buffer size
                                        &returnedLength);

    if (STATUS_INFO_LENGTH_MISMATCH != status) {
        return status;
    }

    //
    // Is the passed-in buffer going to be big enough for us? 
    // This function returns a single contguous buffer model...
    //
    bufferLength = returnedLength - sizeof(UNICODE_STRING);
   
    if (ProcessImageName->MaximumLength < bufferLength) {
        ProcessImageName->Length = (USHORT) bufferLength;
        return STATUS_BUFFER_OVERFLOW;
    }

    //
    // If we get here, the buffer IS going to be big enough for us, so
    // let's allocate some storage.
    //
    buffer = ExAllocatePoolWithTag(PagedPool, returnedLength, 'dPoT');

    if (NULL == buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Now lets go get the data
    //
    status = ZwQueryInformationProcess( hProcess,
                                        ProcessImageFileName,
                                        buffer,
                                        returnedLength,
                                        &returnedLength);

    if (NT_SUCCESS(status)) {
        //
        // Ah, we got what we needed
        //
        imageName = (PUNICODE_STRING) buffer;
        RtlCopyUnicodeString(ProcessImageName, imageName);
    }

    //
    // free our buffer
    //
    ExFreePool(buffer);

    //
    // And tell the caller what happened.
    //   
    return status;
}

//
// The main entry point of the driver module
//
NTSTATUS DriverEntry(
    IN PDRIVER_OBJECT DriverObject, 
    IN PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS                  ntStatus;
    UNICODE_STRING            uszDriverString;
    UNICODE_STRING            uszDeviceString;
    PDEVICE_OBJECT            pDeviceObject;
    PDEVICE_EXTENSION         extension;

    UNREFERENCED_PARAMETER(RegistryPath);
    //    
    // Point uszDriverString at the driver name
    //
    RtlInitUnicodeString(&uszDriverString, L"\\Device\\TopDown");
    //
    // Create and initialize device object
    //
    ntStatus = IoCreateDevice(
        DriverObject,
        sizeof(DEVICE_EXTENSION),
        &uszDriverString,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &pDeviceObject
        );
    if(ntStatus != STATUS_SUCCESS)
        return ntStatus;
    //
    // Assign extension variable
    //
    extension = (PDEVICE_EXTENSION) pDeviceObject->DeviceExtension;
    //
    // Point uszDeviceString at the device name
    //
    RtlInitUnicodeString(&uszDeviceString, L"\\DosDevices\\TopDown");
    //
    // Create symbolic link to the user-visible name
    //
    ntStatus = IoCreateSymbolicLink(&uszDeviceString, &uszDriverString);

    if(ntStatus != STATUS_SUCCESS)
    {
        //
        // Delete device object if not successful
        //
        IoDeleteDevice(pDeviceObject);
        return ntStatus;
    }
    //
    // Assign global pointer to the device object for use by the callback functions
    //
    g_pDeviceObject = pDeviceObject;
    //
    // Setup initial state
    //
    g_ActivateInfo.bActivate = FALSE;
    g_ActivateInfo.FlagsOffset = 0x440;
    g_ActivateInfo.VmTopDownBitPosition = 21;
    //
    // Load structure to point to IRP handlers
    //
    DriverObject->DriverUnload                         = UnloadDriver;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;
    

    InitializeListHead(&g_ImageFileHead.Entry);
    RtlInitUnicodeString(&g_ImageFileHead.Filename, L"");
    //
    // Return success
    //
    return ntStatus;
}

//
// Create and close routine
//
NTSTATUS DispatchCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

//
// Process function callback
//
VOID ProcessCallback(
    IN HANDLE  hParentId, 
    IN HANDLE  hProcessId, 
    IN BOOLEAN bCreate
    )
{
    PEPROCESS pEprocess = NULL;
    PLIST_ENTRY pCur = &g_ImageFileHead.Entry;
    UNICODE_STRING CurrentFileName; 
    UNICODE_STRING JustFileName;
    HANDLE hProcess = NULL;
    PWCH pCh = 0;
    USHORT x = 0;

    UNREFERENCED_PARAMETER(hParentId);

    if(!bCreate)
        return;

    if ( PsLookupProcessByProcessId(hProcessId, &pEprocess) != STATUS_SUCCESS )
        return;

    if ( ObOpenObjectByPointer(pEprocess,0, NULL, 0,0,KernelMode,&hProcess) != STATUS_SUCCESS )
        return;
    
    ObDereferenceObject(pEprocess); 
    
    CurrentFileName.MaximumLength = 0;
    GetProcessImageName(hProcess, &CurrentFileName);
    CurrentFileName.Buffer = (PWCH)ExAllocatePoolWithTag(PagedPool, CurrentFileName.Length, 'dPoT');
	
    if ( CurrentFileName.Buffer == NULL )
        return;

    CurrentFileName.MaximumLength = CurrentFileName.Length;
    GetProcessImageName(hProcess, &CurrentFileName);

    for(x = (CurrentFileName.Length/sizeof(wchar_t)) - 1; x > 0; x--)
    {
        if(CurrentFileName.Buffer[x] == L'\\')
        {
            pCh = &CurrentFileName.Buffer[x+1];
            x++;
            break;
        }
    }

    if ( x == 0 )
        return;

    x *= sizeof(wchar_t);
    JustFileName.Length = CurrentFileName.Length - x;
    JustFileName.MaximumLength = CurrentFileName.Length - x;
    JustFileName.Buffer = pCh;
        
    while(pCur != NULL)
    {
        PIMAGEFILE_ENTRY pEntry = CONTAINING_RECORD(pCur, IMAGEFILE_ENTRY, Entry);

        if(RtlCompareUnicodeString(&pEntry->Filename, &JustFileName, TRUE) == 0)
        {
            *((unsigned int*)(((unsigned char*)pEprocess)+g_ActivateInfo.FlagsOffset)) |= (1 << g_ActivateInfo.VmTopDownBitPosition);
            break;
        }
    
        pCur = pEntry->Entry.Flink;
        if ( pCur == &g_ImageFileHead.Entry )
            break;
    }

    RtlFreeUnicodeString(&CurrentFileName);
    ZwClose(hProcess);
}

NTSTATUS AddImageFileName(
    IN PIRP           Irp
    )
{
    NTSTATUS               ntStatus = STATUS_UNSUCCESSFUL;
    PIO_STACK_LOCATION     irpStack  = IoGetCurrentIrpStackLocation(Irp);
    PTOPDOWN_FILENAME_INFO pImageFileInfo;
    PIMAGEFILE_ENTRY       pIFE;
    
    if (irpStack->Parameters.DeviceIoControl.InputBufferLength >= 
       sizeof(TOPDOWN_ACTIVATE_INFO))		
    {
        pImageFileInfo = (PTOPDOWN_FILENAME_INFO)Irp->AssociatedIrp.SystemBuffer;
        pIFE = (PIMAGEFILE_ENTRY)ExAllocatePoolWithTag(PagedPool, sizeof(IMAGEFILE_ENTRY), 'dPoT');
        if ( pIFE == 0 )
            return ntStatus;

        // TODO : Mem check if ExAllocate Failes
        pIFE->Filename.Buffer = (PWCH) ExAllocatePoolWithTag(PagedPool, pImageFileInfo->Length + sizeof(wchar_t), 'dPoT');
        if ( pIFE->Filename.Buffer == NULL )
            return STATUS_MEMORY_NOT_ALLOCATED;

        pIFE->Filename.MaximumLength = (USHORT)pImageFileInfo->Length;
        pIFE->Filename.Length = pIFE->Filename.MaximumLength;
        RtlCopyMemory(pIFE->Filename.Buffer, &pImageFileInfo->Buffer, pIFE->Filename.Length);

        InsertHeadList(&g_ImageFileHead.Entry, &pIFE->Entry);
        ntStatus = STATUS_SUCCESS;
    }

    return ntStatus;
}

NTSTATUS RemoveImageFileName(
    IN PIRP           Irp
    )
{
    NTSTATUS               ntStatus = STATUS_UNSUCCESSFUL;
    PIO_STACK_LOCATION     irpStack  = IoGetCurrentIrpStackLocation(Irp);
    PTOPDOWN_FILENAME_INFO pImageFileInfo;
    
    if (irpStack->Parameters.DeviceIoControl.InputBufferLength >= 
       sizeof(TOPDOWN_ACTIVATE_INFO))		
    {
        UNICODE_STRING CurrentFileName;
        PLIST_ENTRY pCur = &g_ImageFileHead.Entry;
        pImageFileInfo = (PTOPDOWN_FILENAME_INFO)Irp->AssociatedIrp.SystemBuffer;
        
        CurrentFileName.Buffer = pImageFileInfo->Buffer;
        CurrentFileName.Length = (USHORT)pImageFileInfo->Length;
        CurrentFileName.MaximumLength = CurrentFileName.Length;
        
        while(pCur != NULL)
        {
            PIMAGEFILE_ENTRY pEntry = CONTAINING_RECORD(pCur, IMAGEFILE_ENTRY, Entry);
        
            if(RtlCompareUnicodeString(&pEntry->Filename, &CurrentFileName, FALSE) == 0)
            {
                RtlFreeUnicodeString(&pEntry->Filename);
                RemoveEntryList(&pEntry->Entry);
                ExFreePoolWithTag(pEntry, 'dPoT');
                ntStatus = STATUS_SUCCESS;
                break;
            }
            pCur = pEntry->Entry.Flink;
            if ( pCur == &g_ImageFileHead.Entry )
                break;
        }
    }

    return ntStatus;
}
//
// IOCTL handler for setting the callback
//
NTSTATUS ActivateMonitoringHanlder(
    IN PIRP           Irp
    )
{
    NTSTATUS               ntStatus = STATUS_UNSUCCESSFUL;
    PIO_STACK_LOCATION     irpStack  = IoGetCurrentIrpStackLocation(Irp);
    PTOPDOWN_ACTIVATE_INFO pActivateInfo;
    
    if (irpStack->Parameters.DeviceIoControl.InputBufferLength >= 
       sizeof(TOPDOWN_ACTIVATE_INFO))		
    {
        pActivateInfo = (PTOPDOWN_ACTIVATE_INFO)Irp->AssociatedIrp.SystemBuffer;
        if (g_ActivateInfo.bActivate != pActivateInfo->bActivate)
        {
            if (pActivateInfo->bActivate) 
            {
                //
                // Set up callback routines
                //
                ntStatus = PsSetCreateProcessNotifyRoutine(ProcessCallback, FALSE);
                if (ntStatus != STATUS_SUCCESS)
                {
                    return ntStatus;
                }
                //
                // Setup the global data structure
                //
                g_ActivateInfo.bActivate = pActivateInfo->bActivate; 
            } // if
            else
            {
                //
                // restore the call back routine, thus givinig chance to the 
                // user mode application to unload dynamically the driver
                //
                ntStatus = PsSetCreateProcessNotifyRoutine(ProcessCallback, TRUE);
                if (ntStatus != STATUS_SUCCESS)
                    return ntStatus;
                else
                    g_ActivateInfo.bActivate= FALSE;
            }
            ntStatus = STATUS_SUCCESS;
        } // if
        g_ActivateInfo.FlagsOffset = pActivateInfo->FlagsOffset;
        g_ActivateInfo.VmTopDownBitPosition = pActivateInfo->VmTopDownBitPosition;
    } // if

    return ntStatus;
}

//
// The dispatch routine
//
NTSTATUS DispatchIoctl(
    IN PDEVICE_OBJECT DeviceObject, 
    IN PIRP           Irp
    )
{
    NTSTATUS               ntStatus = STATUS_UNSUCCESSFUL;
    PIO_STACK_LOCATION     irpStack  = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);
    //PDEVICE_EXTENSION      extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    //
    // These IOCTL handlers are the set and get interfaces between
    // the driver and the user mode app
    //
    switch(irpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_TOPDOWN_ACTIVATE_MONITORING:
            {
                ntStatus = ActivateMonitoringHanlder( Irp );
                break;
            }
        case IOCTL_TOPDOWN_ADD_FILENAME:
            {
                ntStatus = AddImageFileName( Irp );
                break;
            }
        case IOCTL_TOPDOWN_REMOVE_FILENAME:
            {
                ntStatus = RemoveImageFileName( Irp );
                break;
            }

        default:
            break;
    }

    Irp->IoStatus.Status = ntStatus;
    //
    // Set number of bytes to copy back to user-mode
    //
    if(ntStatus == STATUS_SUCCESS)
        Irp->IoStatus.Information = 
            irpStack->Parameters.DeviceIoControl.OutputBufferLength;
    else
        Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return ntStatus;
}

//
// Driver unload routine
//
void UnloadDriver(
    IN PDRIVER_OBJECT DriverObject
    )
{
    PLIST_ENTRY pCur = &g_ImageFileHead.Entry;
    UNICODE_STRING  uszDeviceString;
    //
    //  By default the I/O device is configured incorrectly or the 
    // configuration parameters to the driver are incorrect.
    //
    NTSTATUS        ntStatus = STATUS_DEVICE_CONFIGURATION_ERROR;

    
    // since we are always turning it on, always turn it off as well.
    //PsSetCreateProcessNotifyRoutine(ProcessCallback, TRUE);

    if (g_ActivateInfo.bActivate)
        //
        // restore the call back routine, thus givinig chance to the 
        // user mode application to unload dynamically the driver
        //
        ntStatus = PsSetCreateProcessNotifyRoutine(ProcessCallback, TRUE);

    IoDeleteDevice(DriverObject->DeviceObject);

    RtlInitUnicodeString(&uszDeviceString, L"\\DosDevices\\TopDown");
    IoDeleteSymbolicLink(&uszDeviceString);

    // TODO : Need to delete g_ImageHead list and all elements otherwise leaking memory..
    while(pCur != NULL)
    {
        PIMAGEFILE_ENTRY pEntry = CONTAINING_RECORD(pCur, IMAGEFILE_ENTRY, Entry);

        pCur = pEntry->Entry.Flink;
        RemoveEntryList(&pEntry->Entry);
        if ( pEntry != NULL )
            ExFreePoolWithTag(pEntry, 'dPoT');
        
        if ( pCur == &g_ImageFileHead.Entry )
            break;
    }
}

//----------------------------End of the file -------------------------------
