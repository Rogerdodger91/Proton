#include <stdio.h>
#include <stdlib.h>

#include <CLR/CLIFile.h>

const uint8_t* CustomAttribute_Initialize(CLIFile* pFile, const uint8_t* pTableData)
{
    if ((pFile->TablesHeader->PresentTables & (1ull << MetaData_Table_CustomAttribute)) != 0)
    {
        pFile->CustomAttributeCount = *(uint32_t*)pTableData; pTableData += 4;
        pFile->CustomAttributes = (CustomAttribute*)calloc(pFile->CustomAttributeCount + 1, sizeof(CustomAttribute));
    }
    return pTableData;
}

void CustomAttribute_Cleanup(CLIFile* pFile)
{
    if (pFile->CustomAttributes)
    {
        for (uint32_t index = 1; index <= pFile->CustomAttributeCount; ++index)
        {
        }
        free(pFile->CustomAttributes);
        pFile->CustomAttributes = NULL;
    }
}

const uint8_t* CustomAttribute_Load(CLIFile* pFile, const uint8_t* pTableData)
{
    uint32_t parentIndex = 0;
    uint32_t parentRow = 0;
    uint32_t typeIndex = 0;
    uint32_t typeRow = 0;
    for (uint32_t index = 1, heapIndex = 0; index <= pFile->CustomAttributeCount; ++index)
    {
        pFile->CustomAttributes[index].TableIndex = index;
        if (pFile->MethodDefinitionCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->FieldCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->TypeReferenceCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->TypeDefinitionCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->ParameterCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->InterfaceImplementationCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->MemberReferenceCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->ModuleDefinitionCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->DeclSecurityCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->PropertyCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->EventCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->StandAloneSignatureCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->ModuleReferenceCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->TypeSpecificationCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->AssemblyDefinitionCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->AssemblyReferenceCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->FileCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->ExportedTypeCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->ManifestResourceCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->GenericParameterCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->GenericParameterConstraintCount > HasCustomAttribute_Type_MaxRows16Bit ||
            pFile->MethodSpecificationCount > HasCustomAttribute_Type_MaxRows16Bit) { parentIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { parentIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->CustomAttributes[index].TypeOfParent = parentIndex & HasCustomAttribute_Type_Mask;
        parentRow = parentIndex >> HasCustomAttribute_Type_Bits;
        switch (pFile->CustomAttributes[index].TypeOfParent)
        {
        case HasCustomAttribute_Type_MethodDefinition:
            if (parentRow == 0 || parentRow > pFile->MethodDefinitionCount) Panic("CustomAttribute_Load MethodDefinition");
            pFile->CustomAttributes[index].Parent.MethodDefinition = &pFile->MethodDefinitions[parentRow];
            break;
        case HasCustomAttribute_Type_Field:
            if (parentRow == 0 || parentRow > pFile->FieldCount) Panic("CustomAttribute_Load Field");
            pFile->CustomAttributes[index].Parent.Field = &pFile->Fields[parentRow];
            break;
        case HasCustomAttribute_Type_TypeReference:
            if (parentRow == 0 || parentRow > pFile->TypeReferenceCount) Panic("CustomAttribute_Load TypeReference");
            pFile->CustomAttributes[index].Parent.TypeReference = &pFile->TypeReferences[parentRow];
            break;
        case HasCustomAttribute_Type_TypeDefinition:
            if (parentRow == 0 || parentRow > pFile->TypeDefinitionCount) Panic("CustomAttribute_Load TypeDefinition");
            pFile->CustomAttributes[index].Parent.TypeDefinition = &pFile->TypeDefinitions[parentRow];
            break;
        case HasCustomAttribute_Type_Parameter:
            if (parentRow == 0 || parentRow > pFile->ParameterCount) Panic("CustomAttribute_Load Parameter");
            pFile->CustomAttributes[index].Parent.Parameter = &pFile->Parameters[parentRow];
            break;
        case HasCustomAttribute_Type_InterfaceImplementation:
            if (parentRow == 0 || parentRow > pFile->InterfaceImplementationCount) Panic("CustomAttribute_Load InterfaceImplementation");
            pFile->CustomAttributes[index].Parent.InterfaceImplementation = &pFile->InterfaceImplementations[parentRow];
            break;
        case HasCustomAttribute_Type_MemberReference:
            if (parentRow == 0 || parentRow > pFile->MemberReferenceCount) Panic("CustomAttribute_Load MemberReference");
            pFile->CustomAttributes[index].Parent.MemberReference = &pFile->MemberReferences[parentRow];
            break;
        case HasCustomAttribute_Type_ModuleDefinition:
            if (parentRow == 0 || parentRow > pFile->ModuleDefinitionCount) Panic("CustomAttribute_Load ModuleDefinition");
            pFile->CustomAttributes[index].Parent.ModuleDefinition = &pFile->ModuleDefinitions[parentRow];
            break;
        case HasCustomAttribute_Type_DeclSecurity:
            if (parentRow == 0 || parentRow > pFile->DeclSecurityCount) Panic("CustomAttribute_Load DeclSecurity");
            pFile->CustomAttributes[index].Parent.DeclSecurity = &pFile->DeclSecurities[parentRow];
            break;
        case HasCustomAttribute_Type_Property:
            if (parentRow == 0 || parentRow > pFile->PropertyCount) Panic("CustomAttribute_Load Property");
            pFile->CustomAttributes[index].Parent.Property = &pFile->Properties[parentRow];
            break;
        case HasCustomAttribute_Type_Event:
            if (parentRow == 0 || parentRow > pFile->EventCount) Panic("CustomAttribute_Load Event");
            pFile->CustomAttributes[index].Parent.Event = &pFile->Events[parentRow];
            break;
        case HasCustomAttribute_Type_StandAloneSignature:
            if (parentRow == 0 || parentRow > pFile->StandAloneSignatureCount) Panic("CustomAttribute_Load StandAloneSignature");
            pFile->CustomAttributes[index].Parent.StandAloneSignature = &pFile->StandAloneSignatures[parentRow];
            break;
        case HasCustomAttribute_Type_ModuleReference:
            if (parentRow == 0 || parentRow > pFile->ModuleReferenceCount) Panic("CustomAttribute_Load ModuleReference");
            pFile->CustomAttributes[index].Parent.ModuleReference = &pFile->ModuleReferences[parentRow];
            break;
        case HasCustomAttribute_Type_TypeSpecification:
            if (parentRow == 0 || parentRow > pFile->TypeSpecificationCount) Panic("CustomAttribute_Load TypeSpecification");
            pFile->CustomAttributes[index].Parent.TypeSpecification = &pFile->TypeSpecifications[parentRow];
            break;
        case HasCustomAttribute_Type_AssemblyDefinition:
            if (parentRow == 0 || parentRow > pFile->AssemblyDefinitionCount) Panic("CustomAttribute_Load AssemblyDefinition");
            pFile->CustomAttributes[index].Parent.AssemblyDefinition = &pFile->AssemblyDefinitions[parentRow];
            break;
        case HasCustomAttribute_Type_AssemblyReference:
            if (parentRow == 0 || parentRow > pFile->AssemblyReferenceCount) Panic("CustomAttribute_Load AssemblyReference");
            pFile->CustomAttributes[index].Parent.AssemblyReference = &pFile->AssemblyReferences[parentRow];
            break;
        case HasCustomAttribute_Type_File:
            if (parentRow == 0 || parentRow > pFile->FileCount) Panic("CustomAttribute_Load File");
            pFile->CustomAttributes[index].Parent.File = &pFile->Files[parentRow];
            break;
        case HasCustomAttribute_Type_ExportedType:
            if (parentRow == 0 || parentRow > pFile->ExportedTypeCount) Panic("CustomAttribute_Load ExportedType");
            pFile->CustomAttributes[index].Parent.ExportedType = &pFile->ExportedTypes[parentRow];
            break;
        case HasCustomAttribute_Type_ManifestResource:
            if (parentRow == 0 || parentRow > pFile->ManifestResourceCount) Panic("CustomAttribute_Load ManifestResource");
            pFile->CustomAttributes[index].Parent.ManifestResource = &pFile->ManifestResources[parentRow];
            break;
        case HasCustomAttribute_Type_GenericParameter:
            if (parentRow == 0 || parentRow > pFile->GenericParameterCount) Panic("CustomAttribute_Load GenericParameter");
            pFile->CustomAttributes[index].Parent.GenericParameter = &pFile->GenericParameters[parentRow];
            break;
        case HasCustomAttribute_Type_GenericParameterConstraint:
            if (parentRow == 0 || parentRow > pFile->GenericParameterConstraintCount) Panic("CustomAttribute_Load GenericParameterConstraint");
            pFile->CustomAttributes[index].Parent.GenericParameterConstraint = &pFile->GenericParameterConstraints[parentRow];
            break;
        case HasCustomAttribute_Type_MethodSpecification:
            if (parentRow == 0 || parentRow > pFile->MethodSpecificationCount) Panic("CustomAttribute_Load MethodSpecification");
            pFile->CustomAttributes[index].Parent.MethodSpecification = &pFile->MethodSpecifications[parentRow];
            break;
        default: break;
        }
        if (pFile->MethodDefinitionCount > CustomAttributeType_Type_MaxRows16Bit ||
            pFile->MemberReferenceCount > CustomAttributeType_Type_MaxRows16Bit) { typeIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { typeIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->CustomAttributes[index].TypeOfType = typeIndex & CustomAttributeType_Type_Mask;
        typeRow = typeIndex >> CustomAttributeType_Type_Bits;
        switch (pFile->CustomAttributes[index].TypeOfType)
        {
        case CustomAttributeType_Type_MethodDefinition:
            if (typeRow == 0 || typeRow > pFile->MethodDefinitionCount) Panic("CustomAttribute_Load Type MethodDefinition");
            pFile->CustomAttributes[index].Type.MethodDefinition = &pFile->MethodDefinitions[typeRow];
            break;
        case CustomAttributeType_Type_MemberReference:
            if (typeRow == 0 || typeRow > pFile->MemberReferenceCount) Panic("CustomAttribute_Load Type MemberReference");
            pFile->CustomAttributes[index].Type.MemberReference = &pFile->MemberReferences[typeRow];
            break;
        default: break;
        }
        if ((pFile->TablesHeader->HeapOffsetSizes & MetaDataTablesHeader_HeapOffsetSizes_Blobs32Bit) != 0) { heapIndex = *(uint32_t*)pTableData; pTableData += 4; }
        else { heapIndex = *(uint16_t*)pTableData; pTableData += 2; }
        pFile->CustomAttributes[index].Value = MetaData_GetCompressedUnsigned(pFile->BlobsHeap + heapIndex, &pFile->CustomAttributes[index].ValueLength);
    }
    return pTableData;
}

void CustomAttribute_Link(CLIFile* pFile)
{
}
