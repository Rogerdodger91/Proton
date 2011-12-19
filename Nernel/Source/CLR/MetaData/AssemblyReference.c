#include <stdio.h>
#include <stdlib.h>

#include <CLR/CLIFile.h>

const uint8_t* AssemblyReference_Initialize(CLIFile* pFile, const uint8_t* pTableData)
{
    if ((pFile->TablesHeader->PresentTables & (1ull << MetaData_Table_AssemblyReference)) != 0)
    {
        pFile->AssemblyReferenceCount = *(uint32_t*)pTableData; pTableData += 4;
        pFile->AssemblyReferences = (AssemblyReference*)calloc(pFile->AssemblyReferenceCount, sizeof(AssemblyReference));
    }
    return pTableData;
}

void AssemblyReference_Cleanup(CLIFile* pFile)
{
    if (pFile->AssemblyReferences)
    {
        for (uint32_t index = 0; index < pFile->AssemblyReferenceCount; ++index)
        {
        }
        free(pFile->AssemblyReferences);
        pFile->AssemblyReferences = NULL;
    }
}

const uint8_t* AssemblyReference_Load(CLIFile* pFile, const uint8_t* pTableData)
{
    for (uint32_t index = 0, heapIndex = 0; index < pFile->AssemblyReferenceCount; ++index)
    {
        pFile->AssemblyReferences[index].MajorVersion = *(uint16_t* )pTableData; pTableData += 2;
        pFile->AssemblyReferences[index].MinorVersion = *(uint16_t* )pTableData; pTableData += 2;
        pFile->AssemblyReferences[index].Build = *(uint16_t* )pTableData; pTableData += 2;
        pFile->AssemblyReferences[index].Revision = *(uint16_t* )pTableData; pTableData += 2;
        pFile->AssemblyReferences[index].Flags = *(uint32_t* )pTableData; pTableData += 4;
        if ((pFile->TablesHeader->HeapOffsetSizes & MetaDataTablesHeader_HeapOffsetSizes_Blobs32Bit) != 0) { heapIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { heapIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->AssemblyReferences[index].PublicKeyOrToken = MetaData_GetCompressedUnsigned(pFile->BlobsHeap + heapIndex, &pFile->AssemblyReferences[index].PublicKeyOrTokenLength);
        if ((pFile->TablesHeader->HeapOffsetSizes & MetaDataTablesHeader_HeapOffsetSizes_Strings32Bit) != 0) { heapIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { heapIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->AssemblyReferences[index].Name = (const char*)(pFile->StringsHeap + heapIndex);
        if ((pFile->TablesHeader->HeapOffsetSizes & MetaDataTablesHeader_HeapOffsetSizes_Strings32Bit) != 0) { heapIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { heapIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->AssemblyReferences[index].Culture = (const char*)(pFile->StringsHeap + heapIndex);
        if ((pFile->TablesHeader->HeapOffsetSizes & MetaDataTablesHeader_HeapOffsetSizes_Blobs32Bit) != 0) { heapIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { heapIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->AssemblyReferences[index].HashValue = MetaData_GetCompressedUnsigned(pFile->BlobsHeap + heapIndex, &pFile->AssemblyReferences[index].HashValueLength);
    }
    return pTableData;
}