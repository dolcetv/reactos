/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Device utility functions
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include "precomp.h"
#include "devutils.h"

/* FUNCTIONS *****************************************************************/

/**
 * @brief
 * Open an existing device given by its NT-style path, which is assumed to be
 * for a disk device or a partition. The open is for synchronous I/O access.
 *
 * @param[in]   DevicePath
 * Supplies the NT-style path to the device to open.
 *
 * @param[out]  DeviceHandle
 * If successful, receives the NT handle of the opened device.
 * Once the handle is no longer in use, call NtClose() to close it.
 *
 * @param[in]   DesiredAccess
 * An ACCESS_MASK value combination that determines the requested access
 * to the device. Because the open is for synchronous access, SYNCHRONIZE
 * is automatically added to the access mask.
 *
 * @param[in]   ShareAccess
 * Specifies the type of share access for the device.
 *
 * @return  An NTSTATUS code indicating success or failure.
 **/
NTSTATUS
pOpenDeviceEx(
    _In_ PCWSTR DevicePath,
    _Out_ PHANDLE DeviceHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    RtlInitUnicodeString(&Name, DevicePath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    return NtOpenFile(DeviceHandle,
                      DesiredAccess | SYNCHRONIZE,
                      &ObjectAttributes,
                      &IoStatusBlock,
                      ShareAccess,
                      /* FILE_NON_DIRECTORY_FILE | */
                      FILE_SYNCHRONOUS_IO_NONALERT);
}

/**
 * @brief
 * Open an existing device given by its NT-style path, which is assumed to be
 * for a disk device or a partition. The open is share read/write/delete, for
 * synchronous I/O and read access.
 *
 * @param[in]   DevicePath
 * @param[out]  DeviceHandle
 * See the DevicePath and DeviceHandle parameters of pOpenDeviceEx().
 *
 * @return  An NTSTATUS code indicating success or failure.
 *
 * @see pOpenDeviceEx()
 **/
NTSTATUS
pOpenDevice(
    _In_ PCWSTR DevicePath,
    _Out_ PHANDLE DeviceHandle)
{
    return pOpenDeviceEx(DevicePath,
                         DeviceHandle,
                         FILE_READ_DATA | FILE_READ_ATTRIBUTES,
                         FILE_SHARE_VALID_FLAGS // FILE_SHARE_READ,WRITE,DELETE
                         );
}


/* PnP ENUMERATION SUPPORT HELPERS *******************************************/

#define _CFGMGR32_
#include <cfgmgr32.h>

/**
 * @brief
 * Enumerates devices using PnP support.
 * The type of devices to be enumerated is specified by an interface
 * class GUID. A user-provided callback is invoked for each device found.
 *
 * @param[in]   InterfaceClassGuid
 * The interface class GUID designating the devices to enumerate.
 *
 * @param[in]   Callback
 * A user-provided callback function of type PENUM_DEVICES_PROC.
 *
 * @param[in]   Context
 * An optional context for the callback function.
 *
 * @note
 * This function uses the lower-level user-mode CM_* PnP API,
 * that are more widely available than the more common Win32
 * SetupDi* functions.
 * See
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/install/porting-from-setupapi-to-cfgmgr32#get-a-list-of-interfaces-get-the-device-exposing-each-interface-and-get-a-property-from-the-device
 * for more details.
 **/
NTSTATUS
pNtEnumDevicesPnP(
    _In_ const GUID* InterfaceClassGuid,
    _In_ PENUM_DEVICES_PROC Callback,
    _In_opt_ PVOID Context)
{
    NTSTATUS Status;
    CONFIGRET cr;
    ULONG DevIFaceListLength = 0;
    PWSTR DevIFaceList = NULL;
    PWSTR CurrentIFace;

    /*
     * Retrieve a list of device interface instances belonging to the given interface class.
     * Equivalent to:
     * hDevInfo = SetupDiGetClassDevs(pGuid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
     * and SetupDiEnumDeviceInterfaces(hDevInfo, NULL, pGuid, i, &DevIFaceData);
     */
    do
    {
        cr = CM_Get_Device_Interface_List_SizeW(&DevIFaceListLength,
                                                (GUID*)InterfaceClassGuid,
                                                NULL,
                                                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr != CR_SUCCESS)
            break;

        if (DevIFaceList) RtlFreeHeap(ProcessHeap, 0, DevIFaceList);
        DevIFaceList = RtlAllocateHeap(ProcessHeap, HEAP_ZERO_MEMORY,
                                       DevIFaceListLength * sizeof(WCHAR));
        if (!DevIFaceList)
        {
            cr = CR_OUT_OF_MEMORY;
            break;
        }

        cr = CM_Get_Device_Interface_ListW((GUID*)InterfaceClassGuid,
                                           NULL,
                                           DevIFaceList,
                                           DevIFaceListLength,
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    } while (cr == CR_BUFFER_SMALL);

    if (cr != CR_SUCCESS)
    {
        if (DevIFaceList) RtlFreeHeap(ProcessHeap, 0, DevIFaceList);
        return STATUS_UNSUCCESSFUL;
    }

    /* Enumerate each device for the given interface class.
     * NOTE: This gives the proper interface names with the correct casing,
     * contrary to SetupDiGetDeviceInterfaceDetailW(...) that gives them
     * in all lower-case letters. */
    for (CurrentIFace = DevIFaceList;
         *CurrentIFace;
         CurrentIFace += wcslen(CurrentIFace) + 1)
    {
        UNICODE_STRING Name;
        HANDLE DeviceHandle;

// TESTING
#if 0
        WCHAR DevInstPath[MAX_DEVICE_ID_LEN];
        PWSTR buffer = NULL;
        ULONG buffersize = 0;

        cr = CM_Locate_DevNodeW(&DevInst,
                                CurrentDevice, // ????
                                CM_LOCATE_DEVNODE_NORMAL);
        if (cr != CR_SUCCESS)
            break;

        cr = CM_Get_Device_IDW(DevInst,
                               DevInstPath,
                               _countof(DevInstPath),
                               0);

        for (;;)
        {
            // SetupDiGetDeviceRegistryPropertyW(..., SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, ...);
            cr = CM_Get_DevNode_Registry_PropertyW(DevInst,
                                                   CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME,
                                                   NULL, // PULONG pulRegDataType
                                                   (PBYTE)buffer,
                                                   &buffersize,
                                                   0);
            if (cr != CR_BUFFER_SMALL)
                break;

            if (buffer) RtlFreeHeap(ProcessHeap, 0, buffer);
            buffer = RtlAllocateHeap(ProcessHeap, HEAP_ZERO_MEMORY, buffersize);
            if (!buffer)
            {
                cr = CR_OUT_OF_MEMORY;
                break;
            }
        }
        if (cr != CR_SUCCESS)
            continue;
#endif

        RtlInitUnicodeString(&Name, CurrentIFace);

        /* Normalize the interface path in case it is of Win32-style */
        if (Name.Length > 4 * sizeof(WCHAR) &&
            Name.Buffer[0] == '\\' && Name.Buffer[1] == '\\' &&
            Name.Buffer[2] == '?'  && Name.Buffer[3] == '\\')
        {
            Name.Buffer[1] = '?';
        }

        Status = pOpenDevice(/*Name*/CurrentIFace, &DeviceHandle);
        if (NT_SUCCESS(Status))
        {
            /* Do the callback */
            if (Callback)
            {
                (void)Callback(InterfaceClassGuid,
                               CurrentIFace, DeviceHandle, Context);
            }

            NtClose(DeviceHandle);
        }

#if 0
        if (buffer) RtlFreeHeap(ProcessHeap, 0, buffer);
#endif
    }

    if (DevIFaceList) RtlFreeHeap(ProcessHeap, 0, DevIFaceList);

    return STATUS_SUCCESS;
}

/* EOF */
