#include <windows.h>
#include <stdio.h>
#include "DirectReadDisk.h"

// Tell user how to use the program.
int Usage(wchar_t* ProgramName)
{
	printf("\nusage: %ls -f srcfile dstfile \n", ProgramName);
	return -1;
}

int wmain(int argc, wchar_t* argv[])
{
	if (argc != 4)
	{
		Usage(argv[0]);
		return 0;
	}
	
	if (wcscmp(argv[1], L"-f") == 0)
	{
		DirectReadDisk rawRead;
		rawRead.ReadFromClusterStream(argv[2], argv[3]);
	}
	else
	{
		Usage(argv[0]);
	}

	return 0;
}