#include "DirectReadDisk.h"
#include<stdio.h>

DirectReadDisk::DirectReadDisk() {
	hSrcFile = NULL;
	hDstFile = NULL;
	hDiskDriver = NULL;
	pOutputBuffer = NULL;
	hEvent = NULL;
}

bool DirectReadDisk::ReadFromClusterStreamUnInit() {
	// uninit
	if (pOutputBuffer) {
		VirtualFreeEx(INVALID_HANDLE_VALUE, pOutputBuffer, 0, MEM_RELEASE);
		pOutputBuffer = NULL;
	}
	
	if (hSrcFile) {
		CloseHandle(hSrcFile);
		hSrcFile = NULL;
	}

	if (hDiskDriver) {
		CloseHandle(hDiskDriver);
		hDiskDriver = NULL;
	}

	if (hDstFile) {
		CloseHandle(hDstFile);
		hDstFile = NULL;
	}

	if (hEvent) {
		CloseHandle(hEvent);
		hEvent = NULL;
	}

	return true;
}

/**
 * @brief Initialization phase of reading a file from a cluster stream
 * @param pSrcFileName source file to be read
 * @param pDstFileName dst file to be write
 * @param fileSize the size of source file
 * @param dwClusterSize  the size of cluster
 * @return bool Whether the initialization is successful
*/
bool DirectReadDisk::ReadFromClusterStreamInit(wchar_t* pSrcFileName,
	wchar_t* pDstFileName,
	LARGE_INTEGER &fileSize,
	DWORD &dwClusterSize) {
	wchar_t driverPath[3] = { pSrcFileName[0],L':',0 };
	wchar_t driverSymbolicPath[7] = { L'\\',L'\\',L'.',L'\\',pSrcFileName[0],L':',0 };
	DWORD dwSectorsPerCluster, dwBytesPerSector = 0;
	bool bSuccess = FALSE;
	DWORD dwOutputBufSize = 0;

	do {
		// Get a cluster size
		if (!GetDiskFreeSpaceW(driverPath, &dwSectorsPerCluster, &dwBytesPerSector, NULL, NULL)) {
			printf("ReadFromClusterStreamInit: GetDiskFreeSpaceA failed, error code:%d\n", GetLastError());
			break;
		}
		dwClusterSize = dwSectorsPerCluster * dwBytesPerSector;

		// Determine whether the source file is accessible
		hSrcFile = CreateFileW(pSrcFileName,
			FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			0,
			0);
		if (hSrcFile == INVALID_HANDLE_VALUE)
		{
			printf("ReadFromClusterStreamInit: CreateFile %ls failed,error code:%d\n", pSrcFileName, GetLastError());
			break;
		}

		// Open a symbolic link to a local disk	
		hDiskDriver = CreateFileW(driverSymbolicPath,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_READONLY | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
			0);
		if (hDiskDriver == INVALID_HANDLE_VALUE) {
			printf("ReadFromClusterStreamInit: CreateFile symbolic link failed,error code:%d\n", GetLastError());
			break;
		}

		// create writable file
		hDstFile = CreateFileW(pDstFileName,
			GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
			0);
		if (hDstFile == INVALID_HANDLE_VALUE)
		{
			printf("ReadFromClusterStreamInit:  CreateFile %ls failed,errror code = %d\n", pDstFileName, GetLastError());
			break;
		}

		hEvent = CreateEventW(0, 0, 0, 0);
		if (!hEvent) {
			printf("ReadFromClusterStreamInit: CreateEventW Failed, error code:%d\n", GetLastError());
			break;
		}

		if (!GetFileSizeEx(hSrcFile, &fileSize)) {
			printf("ReadFromClusterStreamInit: GetFileSizeEx Failed, error code:%d\n", GetLastError());
			break;
		}

		// Apply for memory for file cluster flow information
		dwOutputBufSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + (fileSize.QuadPart / dwClusterSize) * sizeof(pOutputBuffer->Extents);
		pOutputBuffer = (PRETRIEVAL_POINTERS_BUFFER)VirtualAllocEx(INVALID_HANDLE_VALUE,
			NULL,
			dwOutputBufSize,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE);
		if (!pOutputBuffer) {
			printf("ReadFromClusterStreamInit: VirtualAllocEx Failed, error code:%d\n", GetLastError());
			break;
		}


		if (SetFilePointer(hDstFile,
			fileSize.LowPart,
			&fileSize.HighPart,
			FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			printf("ReadFromClusterStreamInit: SetFilePointer Failed, error code:%d\n", GetLastError());
			break;
		}

		if (!SetEndOfFile(hDstFile)) {
			printf("ReadFromClusterStreamInit: SetEndOfFile Failed, error code:%d\n", GetLastError());
			break;
		}
		
		
		LARGE_INTEGER integer = { 0,0 };
		if (SetFilePointer(hDstFile,
			integer.LowPart,
			&integer.HighPart,
			FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			printf("ReadFromClusterStreamInit: SetFilePointer Failed, error code:%d\n", GetLastError());
			break;
		}
		
		bSuccess = TRUE;
	} while (false);

	return bSuccess;
}

/**
 * @brief copy the cluster stream from source file to dst file
 * @param fileSize
 * @param dwClusterSize
 * @return bool
*/
bool DirectReadDisk::CopyClusterStream(LARGE_INTEGER& fileSize,DWORD &dwClusterSize) {
	DWORD dwByteReturned = 0;
	STARTING_VCN_INPUT_BUFFER inVcvBuffer;
	DWORD dwOutputBufSize = 0;
	DWORD dwClusterCapacity = 0;
	bool bDeviceIoResult = false;
	bool bSuccess = true;

	dwOutputBufSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + (fileSize.QuadPart / dwClusterSize) * sizeof(pOutputBuffer->Extents);
	do {
		inVcvBuffer.StartingVcn.QuadPart = 0;
		// Get cluster flow list information
		bDeviceIoResult = DeviceIoControl(hSrcFile,
			FSCTL_GET_RETRIEVAL_POINTERS,
			&inVcvBuffer,
			sizeof(inVcvBuffer),
			pOutputBuffer,
			dwOutputBufSize,
			&dwByteReturned,
			NULL);
		if (!bDeviceIoResult)
		{
			printf("CopyClusterStream: DeviceIocontrol failed, error code:%d\n", GetLastError());
			bSuccess = false;
			break;
		}

		// Read cluster-by-cluster and write to new file
		LARGE_INTEGER preVcn = pOutputBuffer->StartingVcn;
		LARGE_INTEGER curVcn = { 0,0 };
		LARGE_INTEGER clusterStreamSize = { 0,0 };
		PVOID pClusterStream = NULL;
		LARGE_INTEGER readPos = { 0,0 };
		DWORD dwByteRead = 0;
		bool bReadComplete = false;
		DWORD dwErrorCode = 0;
		OVERLAPPED stOverlapped = { 0, };

		// Start traversing the cluster flow
		for (int i = 0; i < pOutputBuffer->ExtentCount; i++) {
			curVcn = pOutputBuffer->Extents[i].NextVcn;
			clusterStreamSize.QuadPart = dwClusterSize * (curVcn.QuadPart - preVcn.QuadPart);
			pClusterStream = (PVOID)VirtualAllocEx(INVALID_HANDLE_VALUE,
				NULL,
				clusterStreamSize.QuadPart,
				MEM_COMMIT | MEM_RESERVE,
				PAGE_READWRITE);
			if (!pClusterStream) {
				printf("CopyClusterStream: VirtualAllocEx Failed, error code:%d\n", GetLastError());
				bSuccess = false;
				break;
			}

			// Get the location to read disk data
			LARGE_INTEGER curLcn = pOutputBuffer->Extents[i].Lcn;
			readPos.QuadPart = curLcn.QuadPart * dwClusterSize;
			stOverlapped.Offset = readPos.LowPart;
			stOverlapped.OffsetHigh = readPos.HighPart;
			stOverlapped.hEvent = hEvent;
			stOverlapped.Internal = 0;
			stOverlapped.InternalHigh = 0;

			// Asynchronous I/O read file
			bReadComplete = ReadFile(hDiskDriver, pClusterStream, clusterStreamSize.QuadPart, NULL, &stOverlapped);
			dwErrorCode = GetLastError();
			if (bReadComplete || dwErrorCode == ERROR_IO_PENDING) {
				if (!GetOverlappedResult(hDiskDriver, &stOverlapped, &dwByteRead, true)) {
					printf("CopyClusterStream: GetOverlappedResult Failed, error code:%d\n", GetLastError());
					bSuccess = false;
					break;
				}

				if (!WriteFile(hDstFile, pClusterStream, dwByteRead, &dwByteRead, NULL)) {
					printf("CopyClusterStream: WriteFile Failed, error code:%d\n", GetLastError());
					bSuccess = false;
					break;
				}

				if (!VirtualFreeEx(INVALID_HANDLE_VALUE, pClusterStream, 0, MEM_RELEASE)) {
					printf("CopyClusterStream: VirtualFreeEx Failed, error code:%d\n", GetLastError());
					bSuccess = false;
					break;
				}
				pClusterStream = NULL;
				preVcn = pOutputBuffer->Extents[i].NextVcn;
			}
		}

		if (pClusterStream) {
			VirtualFreeEx(INVALID_HANDLE_VALUE,
				pClusterStream,
				0,
				MEM_RELEASE);
		}

		if (SetFilePointer(hDstFile,
			fileSize.LowPart,
			&fileSize.HighPart,
			FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			printf("CopyClusterStream: SetFilePointer Failed, error code:%d\n", GetLastError());
		}
		if (!SetEndOfFile(hDstFile)) {
			printf("CopyClusterStream: SetEndOfFile Failed, error code:%d\n", GetLastError());
		}
	} while (false);

	return bSuccess;
}

/**
 * @brief reading a file from a cluster stream
 * @param pSrcFileName source file to be read
 * @param pDstFileName dst file to be write
 * @return bool
*/
bool DirectReadDisk::ReadFromClusterStream(wchar_t* pSrcFileName, wchar_t* pDstFileName) {
	LARGE_INTEGER srcFileSize = { 0,0 };
	DWORD dwClusterSize = 0;
	bool bSuccess = false;

	do {
		// try to open all files and disk devices
		if (!ReadFromClusterStreamInit(pSrcFileName,
			pDstFileName,
			srcFileSize,
			dwClusterSize)) {
			printf("ReadFromClusterStreamInit failed.\n");
			break;
		}

		// Obtain the list of cluster streams where the file is located,
		// and process each group of cluster streams separately instead of collectively
		if (!CopyClusterStream(srcFileSize, dwClusterSize)) {
			printf("CopyClusterStream failed.\n");
			break;
		}

		bSuccess = true;
	} while (false);	

	// uninit
	if (!ReadFromClusterStreamUnInit()) {
		printf("ReadFromClusterStreamUnInit failed.\n");
		return false;
	}
	return bSuccess;
}