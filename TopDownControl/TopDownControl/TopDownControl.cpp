// TopDownControl.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <tchar.h>
#include <algorithm>
#include <string>

#include "TopDownIo.h"

//#define NO_DEVICE (1)

bool GetSymbolInfo(ULONG& FlagOffset, ULONG& VmTopDownBitPosition);

void Usage()
{
    printf("-start       Start listenting to process starts.\n");
    printf("-stop        Stop listenting to process starts.\n");
    printf("-add         Add \"str1\" to the intercept list.\n");
    printf("-remove      Remove \"str1\" from the intercept list.\n");
    printf("-usesymbols  Use public symbols to get VmTopDown Offset (default true).\n");
    printf("-?           Display this message.\n");
}


wchar_t* GetCommandOption(wchar_t ** begin, wchar_t ** end, const std::wstring & option)
{
    wchar_t ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end)
    {
        return *itr;
    }
    return 0;
}

bool CommandOptionExists(wchar_t** begin, wchar_t** end, const std::wstring& option)
{
    return std::find(begin, end, option) != end;
}

#ifdef NO_DEVICE
HANDLE OpenTopDownDevice()
{
    return (HANDLE)1;
}
int ActivateDevice(HANDLE hDriverFile, BOOL activate, ULONG FlagsOffset, ULONG VmTopDownBitPosition)
{
    if(activate)
        printf("Turning on Process Checking..\n");
    else
        printf("Turning off Process Checking..\n");

    printf("ActivateDevice returned %d\n", 1);
    return 0;
}

int AddDeleteName(HANDLE hDriverFile, BOOL add, wchar_t* ImageFileName)
{
    printf("%s %S\n", add ? "Adding" : "Removing", ImageFileName);
    printf("AddDeleteName returned %d\n", 1);
    return 0;
}

#else
HANDLE OpenTopDownDevice()
{
    HANDLE hDriverFile = hDriverFile = ::CreateFile(
            TEXT("\\\\.\\TopDown"),
            GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            0,                     // Default security
            OPEN_EXISTING,
            0,                     // Perform synchronous I/O
            0);                    // No template

    return hDriverFile;
}

int ActivateDevice(HANDLE hDriverFile, BOOLEAN Activate, ULONG FlagsOffset, ULONG VmTopDownBitPosition)
{
    TOPDOWN_ACTIVATE_INFO ActivateInfo;
        
    ActivateInfo.bActivate = Activate;
    ActivateInfo.FlagsOffset = FlagsOffset;
    ActivateInfo.VmTopDownBitPosition = VmTopDownBitPosition;
    DWORD dwBytesReturned = 0;
    
    if(Activate)
        printf("Turning on Process Checking..\n");
    else
        printf("Turning off Process Checking..\n");

    BOOL bReturnCode = ::DeviceIoControl(
                hDriverFile,
                IOCTL_TOPDOWN_ACTIVATE_MONITORING,
                &ActivateInfo, 
                sizeof(ActivateInfo),
                NULL, 
                0,
                &dwBytesReturned,
                NULL
                );
    printf("ActivateDevice returned %d\n", bReturnCode);
    return 0;
}

// Always takes a NULL terminated UNICODE string..
int AddDeleteName(HANDLE hDriverFile, BOOL Add, wchar_t* Name)
{
    BOOL bReturnCode = 0;
    DWORD dwBytesReturned = 0;

    PTOPDOWN_FILENAME_INFO FilenameInfo = NULL;
    // +1 for NULL terminator
    FilenameInfo = (PTOPDOWN_FILENAME_INFO)calloc(1, sizeof(TOPDOWN_FILENAME_INFO) + sizeof(wchar_t) * wcslen(Name) + sizeof(wchar_t));
    FilenameInfo->Length = (USHORT)(wcslen(Name) * sizeof(wchar_t));
    memcpy(&FilenameInfo->Buffer, Name, sizeof(wchar_t) * wcslen(Name));
    
    printf("%s %S\n", Add ? "Adding" : "Removing", Name);
    bReturnCode = ::DeviceIoControl(
                hDriverFile,
                Add ? IOCTL_TOPDOWN_ADD_FILENAME : IOCTL_TOPDOWN_REMOVE_FILENAME,
                FilenameInfo, 
                sizeof(TOPDOWN_FILENAME_INFO) + FilenameInfo->Length,
                NULL, 
                0,
                &dwBytesReturned,
                NULL
                );
    
    free(FilenameInfo);

    printf("AddDeleteName returned %d\n", bReturnCode);
    return 0;
}

#endif

int _tmain(int argc, _TCHAR* argv[])
{
    // defaults for Windows 7 x64
    ULONG FlagsOffset = 0x440;
    ULONG VmTopDownBitPosition = 21;
	bool Start = false, Stop = false, UseSymbols = false;
    bool HasOption = false;


	if ( CommandOptionExists(argv, argv+argc, _T("-?")) )
	{
		Usage();
		return 0;
	}

	if ( CommandOptionExists(argv, argv+argc, _T("-start")) )
	{
		HasOption = Start = true;
	}

	if ( CommandOptionExists(argv, argv+argc, _T("-stop")) )
	{
		HasOption = Stop = true;
	}

	if ( CommandOptionExists(argv, argv+argc, _T("-usesymbols")) )
	{
		UseSymbols = true;
	}

	wchar_t * AddFilename = GetCommandOption(argv, argv + argc, _T("-add"));
	wchar_t * RemoveFilename = GetCommandOption(argv, argv + argc, _T("-remove"));

    if ( AddFilename )
        HasOption = true;

    if ( RemoveFilename )
        HasOption = true;
    
    if ( !HasOption )
    {
        Usage();
        return 0;
    }

	if ( Start && Stop )
    {
        printf("Can't have -start+ and -stop+\n");
        return -1;
    }

    HANDLE hDriverFile = OpenTopDownDevice();
    
    if (INVALID_HANDLE_VALUE == hDriverFile)
    {
        printf("Failed to open \\\\.\\TopDown\n");
        return -1;
    }

	if ( RemoveFilename )
    {
        AddDeleteName(hDriverFile, FALSE, RemoveFilename);
    }

	if ( AddFilename )
    {
        AddDeleteName(hDriverFile, TRUE, AddFilename);
    }

    if ( UseSymbols )
    {
        // get the symbol stuff using DbgHelp.dll
        // Set that value as the offset/bitposition for VmTopDown
		if ( !GetSymbolInfo(FlagsOffset, VmTopDownBitPosition) )
        {
            printf("Failed to Get symbol info.\n");
			return -1;
        }
    }
	printf("FlagsOffset = 0x%x VmTopDownBitPosition = %d\n", FlagsOffset, VmTopDownBitPosition);

    if ( Start )
    {
        ActivateDevice(hDriverFile, TRUE, FlagsOffset, VmTopDownBitPosition);
    }
    
    if ( Stop )
    {
        ActivateDevice(hDriverFile, FALSE, FlagsOffset, VmTopDownBitPosition);
    } 

    CloseHandle(hDriverFile);

    return 0;
}

