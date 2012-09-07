// ShowMemory.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>


int _tmain(int argc, _TCHAR* argv[])
{
	void* pointers[4];
	unsigned int temp[4];

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

	pointers[0] = HeapAlloc(GetProcessHeap(), 0, 1024);
	pointers[1] = malloc(1024);
	pointers[2] = VirtualAlloc(0, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	pointers[3] = new byte[1024];
	printf("HeapAlloc    = %p\n", pointers[0]);
	printf("malloc       = %p\n", pointers[1]);
	printf("VirtualAlloc = %p\n", pointers[2]);
	printf("new          = %p\n", pointers[3]);

	for(int x = 0; x < 4; x++)
		temp[x] = (unsigned int)pointers[x];

	for(int x = 0; x < 4; x++)
		pointers[x] = (void*)temp[x];

	printf("After truncation..\n");
	printf("HeapAlloc    = %p\n", pointers[0]);
	printf("malloc       = %p\n", pointers[1]);
	printf("VirtualAlloc = %p\n", pointers[2]);
	printf("new          = %p\n", pointers[3]);

	return 0;
}

