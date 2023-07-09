#pragma once
#include<Windows.h>

class DirectReadDisk {
public:
    DirectReadDisk();
    /**
    * @brief Initialization phase of reading a file from a cluster stream
    * @param pSrcFileName source file to be read
    * @param pDstFileName dst file to be write
    * @param fileSize the size of source file
    * @param dwClusterSize  the size of cluster
    * @return bool Whether the initialization is successful
   */
    bool ReadFromClusterStreamInit(wchar_t* pSrcFileName,
        wchar_t* pDstFileName,
        LARGE_INTEGER& fileSize,
        DWORD& dwClusterSize);
    /**
     * @brief reading a file from a cluster stream
     * @param pSrcFileName source file to be read
     * @param pDstFileName dst file to be write
     * @return bool 
    */
    bool ReadFromClusterStream(wchar_t* pSrcFileName, wchar_t* pDstFileName);
    bool ReadFromClusterStreamUnInit();
    /**
     * @brief copy the cluster stream from source file to dst file
     * @param fileSize 
     * @param dwClusterSize 
     * @return 
    */
    bool CopyClusterStream(LARGE_INTEGER& fileSize, DWORD& dwClusterSize);
public:
    // source file handle
    HANDLE hSrcFile;
    // dst file handle
    HANDLE hDstFile;
    // the disk handle where the source file is located
    HANDLE hDiskDriver;
    // Asynchronously read event
    HANDLE hEvent;
    // store clusters info
    PRETRIEVAL_POINTERS_BUFFER pOutputBuffer;
};