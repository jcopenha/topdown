#include "stdafx.h"
#include <Windows.h>
#include <cstdio>

// This maps all of the 'W' postfixed symbols to their
// non-'W' equivalent, which means this will fail on DbgHelp.dll < 6.3
// and not work at all if compied without UNICODE defined.

#define DBGHELP_TRANSLATE_TCHAR
#include <dbghelp.h>

typedef BOOL    (WINAPI *PFNSYMINITIALIZEW)(HANDLE hProcess, PCTSTR UserSearchPath, BOOL fInvadeProcess);
typedef DWORD   (WINAPI *PFNSYMSETOPTIONS)(DWORD SymOptions);
typedef DWORD64 (WINAPI *PFNSYMLOADMODULEXW)(HANDLE hProcess, HANDLE hFile, LPCTSTR ImageName, LPCTSTR ModuleName, DWORD64 BaseOfDll, DWORD DllSize, PMODLOAD_DATA Data, DWORD Flags);
typedef BOOL    (WINAPI *PFNSYMGETTYPEFROMNAMEW)(HANDLE hProcess, ULONG64 BaseOfDll, PCTSTR Name, PSYMBOL_INFO Symbol);
typedef BOOL    (WINAPI *PFNSYMGETTYPEINFO)(HANDLE hProcess, DWORD64 ModBase, ULONG TypeId, IMAGEHLP_SYMBOL_TYPE_INFO GetType, PVOID pInfo);


bool GetSymbolInfo(ULONG& FlagOffset, ULONG& VmTopDownBitPosition)
{
    HANDLE hProcess;
    HMODULE hDbgHelp = 0;
    hDbgHelp = LoadLibrary(L"DbgHelp.dll");
    if (hDbgHelp == 0)
    {
        printf("Failed to load DbgHelp.dll - %d\n", GetLastError());
        return false;
    }

    PFNSYMINITIALIZEW pfnSymInitialize = (PFNSYMINITIALIZEW)GetProcAddress(hDbgHelp, "SymInitializeW");
    PFNSYMSETOPTIONS pfnSymSetOptions = (PFNSYMSETOPTIONS)GetProcAddress(hDbgHelp, "SymSetOptions");
    PFNSYMLOADMODULEXW pfnSymLoadModuleEx = (PFNSYMLOADMODULEXW)GetProcAddress(hDbgHelp, "SymLoadModuleExW");
    PFNSYMGETTYPEFROMNAMEW pfnSymGetTypeFromName = (PFNSYMGETTYPEFROMNAMEW)GetProcAddress(hDbgHelp, "SymGetTypeFromNameW");
    PFNSYMGETTYPEINFO pfnSymGetTypeInfo = (PFNSYMGETTYPEINFO)GetProcAddress(hDbgHelp, "SymGetTypeInfo");

    (pfnSymSetOptions)(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS| SYMOPT_DEBUG);

    hProcess = GetCurrentProcess();

    if (!(pfnSymInitialize)(hProcess, NULL, FALSE))
    {
        printf("SymInitialize returned error : %d\n", GetLastError());
        return false;
    }

    TCHAR  szImageName[MAX_PATH] = TEXT("c:\\windows\\system32\\ntoskrnl.exe");
    DWORD64 dwBaseAddr = 0;
    DWORD64 BaseOfDll = 0;
    ULONG64 buffer[(sizeof(SYMBOL_INFO) +
            MAX_SYM_NAME * sizeof(TCHAR) +
            sizeof(ULONG64) - 1) /
            sizeof(ULONG64)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;

    // this call can take awhile if the user hasn't downloaded the symbols
    BaseOfDll = (pfnSymLoadModuleEx)(hProcess,    // target process 
                                     NULL,        // handle to image - not used
                                     szImageName, // name of image file
                                     NULL,        // name of module - not required
                                     dwBaseAddr,  // base address - not required
                                     0,           // size of image - not required
                                     NULL,        // MODLOAD_DATA used for special cases 
                                     0);          // flags - not required
    if ( BaseOfDll == 0 )
    {
        printf("SymLoadModuleEx returned error : %d\n", GetLastError());
        return false;
    }

    if(!(pfnSymGetTypeFromName)(hProcess, BaseOfDll, TEXT("_EPROCESS"), pSymbol))
    {
        // SymEnumSymbols failed
        printf("SymEnumSymbols failed: %d\n", GetLastError());
        return false;
    }

    DWORD Index = pSymbol->TypeIndex; // use this to get children count
    DWORD ChildCount = 0; 

    if( !(*pfnSymGetTypeInfo)( hProcess, BaseOfDll, Index, TI_GET_CHILDRENCOUNT, &ChildCount ) ) 
    {
      printf("SymGetTypeInfo(TI_GET_CHIDRENCOUNT) failed %d.", GetLastError()); 
      return false; 
    }

    if( ChildCount == 0 ) 
    {
      // No children 
      printf("No Children for symbol _EPROCESS\n");
      return false; 
    }

    // Get the children 

    int FindChildrenSize = sizeof(TI_FINDCHILDREN_PARAMS) + ChildCount*sizeof(ULONG); 

    TI_FINDCHILDREN_PARAMS* pFC = (TI_FINDCHILDREN_PARAMS*)calloc( FindChildrenSize, 1 ); 

    pFC->Count = ChildCount; 

    if( !(*pfnSymGetTypeInfo)( hProcess, BaseOfDll, Index, TI_FINDCHILDREN, pFC ) ) 
    {
      printf( "SymGetTypeInfo(TI_FINDCHILDREN) failed. %d", GetLastError() ); 
      return false; 
    }

    for( DWORD i = 0; i < ChildCount; i++ ) 
    {
        WCHAR* pC;
        (*pfnSymGetTypeInfo)( hProcess, BaseOfDll, pFC->ChildId[i], TI_GET_SYMNAME, &pC );
        if( _wcsicmp(pC ,L"VmTopDown") == 0)
        {
            DWORD FlagsIndex = pFC->ChildId[i];
            DWORD Offset = 0;
            DWORD BitPos = 0;
            (*pfnSymGetTypeInfo)( hProcess, BaseOfDll, FlagsIndex, TI_GET_OFFSET, &Offset );
            (*pfnSymGetTypeInfo)( hProcess, BaseOfDll, FlagsIndex, TI_GET_BITPOSITION, &BitPos );
			FlagOffset = Offset;
			VmTopDownBitPosition = BitPos;
            //printf("Child of _EPCORESS %d is %S @ 0x%X:%d\n", FlagsIndex, pC, FlagsOffset, FlagsType);
            LocalFree(pC);
            break;
        }
        LocalFree(pC);
    }

	return true;
}