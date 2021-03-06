#include <CLR/ILReader.h>
#include <CLR/OpCodes_IL.h>
#include <CLR/OpCodes_IR.h>
#include <CLR/SyntheticStack.h>
#include <CLR/IROptimizer.h>
#include <CLR/IRMethod_BranchLinker.h>
#include <Core/Multiboot.h>
#include <CLR/Log.h>
#include <String_Format.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <CLR/ILReader_Defines.h>
#include <Inline.h>


uint8_t ReadUInt8(uint8_t** dat);
uint16_t ReadUInt16(uint8_t** dat);
uint32_t ReadUInt32(uint8_t** dat);
uint64_t ReadUInt64(uint8_t** dat);
void Link(IRAssembly* asmb);

IRField** TypeDefinition_GetLayedOutFields(TypeDefinition* tdef, CLIFile* fil, uint* destLength, AppDomain* dom);
IRMethod** TypeDefinition_GetLayedOutMethods(TypeDefinition* tdef, CLIFile* fil, uint* destLength, AppDomain* dom);
IRType* GenerateType(TypeDefinition* def, CLIFile* fil, IRAssembly* asmb, AppDomain* dom);
IRField* GenerateField(Field* def, CLIFile* fil, IRAssembly* asmb, AppDomain* dom);
IRMethod* GenerateMethod(MethodDefinition* methodDef, CLIFile* fil, IRAssembly* asmb, AppDomain* dom);


void EmptyStack(SyntheticStack* stack);

void CheckBinaryNumericOperandTypesAndSetResult(ElementType* OperandA, ElementType* OperandB, int BinaryNumericOp, StackObject* ResultObject);
void GetElementTypeFromTypeDef(TypeDefinition* tdef, AppDomain* dom, ElementType* dst);
void GetElementTypeOfStackObject(ElementType* dest, StackObject* stkObj);
void SetObjectTypeFromElementType(StackObject* obj, ElementType elemType);
void SetTypeOfStackObjectFromSigElementType(StackObject* obj, SignatureType* TypeSig, CLIFile* fil, AppDomain* dom);
IRType* GetIRTypeOfSignatureType(AppDomain* dom, CLIFile* fil, IRAssembly* asmbly, SignatureType* tp);

bool_t IsStruct(TypeDefinition* tdef, AppDomain* dom);



void ReadIL(uint8_t** dat, uint32_t len, MethodDefinition* methodDef, CLIFile* fil, AppDomain* dom, IRAssembly* asmbly, IRMethod* m);

IRAssembly* ILReader_CreateAssembly(CLIFile* fil, AppDomain* dom)
{
	if (fil->AssemblyReferenceCount > 0)
	{
		char buf[FILENAME_MAX];
		char buf2[FILENAME_MAX];
		for (uint32_t i = 1; i <= fil->AssemblyReferenceCount; i++)
		{
			AssemblyReference* r = &fil->AssemblyReferences[i];
			Log_WriteLine(LogFlags_AssemblyReferenceResolution, "Attempting to resolve %s", r->Name);
			bool_t FoundAssembly = FALSE;
			for (uint32_t i2 = 0; i2 < dom->IRAssemblyCount; i2++)
			{
				if (!strcmp(dom->IRAssemblies[i2]->ParentFile->AssemblyDefinitions[1].Name, r->Name))
				{
					Log_WriteLine(LogFlags_AssemblyReferenceResolution, "Found assembly with matching name");
					FoundAssembly = TRUE;
					break;
				}
			}
			if (FoundAssembly)
			{
				Log_WriteLine(LogFlags_AssemblyReferenceResolution, "Resolved %s by finding it in the app domain.", r->Name);
			}
			else
			{
				snprintf(buf, FILENAME_MAX, "/gac/%s.dll", r->Name);
				snprintf(buf2, FILENAME_MAX, "%s.dll", r->Name);
				MultiBoot_LoadedModule* mod = MultiBoot_GetLoadedModuleByFileName(buf);
				//if (!mod)
				//{
				//	snprintf(buf, FILENAME_MAX, "/gac/%s.exe", r->Name);
				//	mod = MultiBoot_GetLoadedModuleByFileName(buf);
				//}
				if (!mod)
				{
					snprintf(buf, FILENAME_MAX, "/gac/proton/%s.dll", r->Name);
					mod = MultiBoot_GetLoadedModuleByFileName(buf);
				}
				//if (!mod)
				//{
				//	snprintf(buf, FILENAME_MAX, "/gac/proton/%s.exe", r->Name);
				//	mod = MultiBoot_GetLoadedModuleByFileName(buf);
				//}
				if (!mod)
				{
					Panic("Unable to resolve dependancy!");
				}
				Log_WriteLine(LogFlags_AssemblyReferenceResolution, "Resolved %s by loading it from a module.", r->Name);
				CLIFile* clFil = CLIFile_Create(PEFile_Create((uint8_t*)mod->Address, mod->Length), buf2);
				IRAssembly* asmb = ILReader_CreateAssembly(clFil, dom);
				AppDomain_AddAssembly(dom, asmb);
			}
		}
		AppDomain_ResolveMetaReferences(fil, dom);
	}
	IRAssembly* asmbly = IRAssembly_Create(dom);
	asmbly->ParentFile = fil;
	fil->Assembly = asmbly;
	
	for (uint32_t i = 1; i <= fil->TypeDefinitionCount; i++)
	{
		IRAssembly_AddType(asmbly, GenerateType(&fil->TypeDefinitions[i], fil, asmbly, dom));
	}
	printf("Loaded Types\n");
	
	for (uint32_t i = 1; i <= fil->FieldCount; i++)
	{
		IRAssembly_AddField(asmbly, GenerateField(&fil->Fields[i], fil, asmbly, dom));
	}
	printf("Loaded Fields\n");

    for (uint32_t i = 1; i <= fil->MethodDefinitionCount; i++)
    {
        Log_WriteLine(LogFlags_ILReading, "Reading Method %s.%s.%s", fil->MethodDefinitions[i].TypeDefinition->Namespace, fil->MethodDefinitions[i].TypeDefinition->Name, fil->MethodDefinitions[i].Name);
		Log_WriteLine(LogFlags_ILReading, "Method index: %i", (int)i);
        IRMethod* irMeth = GenerateMethod(&fil->MethodDefinitions[i], fil, asmbly, dom);
		irMeth->MethodDefinition = &fil->MethodDefinitions[i];
        IRAssembly_AddMethod(asmbly, irMeth);
    }
	printf("Loaded Methods\n");
	Link(asmbly);
	
	if (fil->CLIHeader->EntryPointToken)
	{
		MetaDataToken* tok = CLIFile_ResolveToken(fil, fil->CLIHeader->EntryPointToken);
		if (tok->Table != MetaData_Table_MethodDefinition)
			Panic("Unknown entry point Table!");
		asmbly->EntryPoint = asmbly->Methods[((MethodDefinition*)tok->Data)->TableIndex - 1];
		free(tok);
	}
	return asmbly;
}

void DecomposeMethod(IRMethod* mth)
{
	uint8_t* ilLoc = (uint8_t*)mth->MethodDefinition->Body.Code;
	ReadIL(&ilLoc, mth->MethodDefinition->Body.CodeSize, mth->MethodDefinition, mth->ParentAssembly->ParentFile, mth->ParentAssembly->ParentDomain, mth->ParentAssembly, mth);
    IRMethod_BranchLinker_LinkMethod(mth);
}

IRField* GenerateField(Field* def, CLIFile* fil, IRAssembly* asmb, AppDomain* dom)
{
	IRField* fld = IRField_Create();
	if (def->Flags & FieldAttributes_Static)
		fld->IsStatic = TRUE;
	fld->ParentAssembly = asmb;
	fld->FieldDef = def;
	fld->ParentType = (IRType*)def->TypeDefinition;
	FieldSignature* sig = FieldSignature_Expand(def->Signature, fil);
	fld->FieldType = GetIRTypeOfSignatureType(dom, fil, asmb, sig->Type);
	//switch(sig->Type->ElementType)
	//{
	//	case Signature_ElementType_Boolean:
	//		fld->FieldType = dom->CachedType___System_Boolean->File->Assembly->Types[dom->CachedType___System_Boolean->TableIndex - 1];
	//		break;
	//	case Signature_ElementType_Char:
	//		fld->FieldType = dom->CachedType___System_Char->File->Assembly->Types[dom->CachedType___System_Char->TableIndex - 1];
	//		break;
	//	case Signature_ElementType_I1:
	//		fld->FieldType = dom->CachedType___System_SByte->File->Assembly->Types[dom->CachedType___System_SByte->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_SByte;
	//		break;
	//	case Signature_ElementType_I2:
	//		fld->FieldType = dom->CachedType___System_Int16->File->Assembly->Types[dom->CachedType___System_Int16->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Int16;
	//		break;
	//	case Signature_ElementType_I4:
	//		fld->FieldType = dom->CachedType___System_Int32->File->Assembly->Types[dom->CachedType___System_Int32->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Int32;
	//		break;
	//	case Signature_ElementType_I8:
	//		fld->FieldType = dom->CachedType___System_Int64->File->Assembly->Types[dom->CachedType___System_Int64->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Int64;
	//		break;
	//	case Signature_ElementType_IPointer:
	//		fld->FieldType = dom->CachedType___System_IntPtr->File->Assembly->Types[dom->CachedType___System_IntPtr->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_IntPtr;
	//		break;
	//	case Signature_ElementType_U1:
	//		fld->FieldType = dom->CachedType___System_Byte->File->Assembly->Types[dom->CachedType___System_Byte->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Byte;
	//		break;
	//	case Signature_ElementType_U2:
	//		fld->FieldType = dom->CachedType___System_UInt16->File->Assembly->Types[dom->CachedType___System_UInt16->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_UInt16;
	//		break;
	//	case Signature_ElementType_U4:
	//		fld->FieldType = dom->CachedType___System_UInt32->File->Assembly->Types[dom->CachedType___System_UInt32->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_UInt32;
	//		break;
	//	case Signature_ElementType_U8:
	//		fld->FieldType = dom->CachedType___System_UInt64->File->Assembly->Types[dom->CachedType___System_UInt64->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_UInt64;
	//		break;
	//	case Signature_ElementType_UPointer:
	//		fld->FieldType = dom->CachedType___System_UIntPtr->File->Assembly->Types[dom->CachedType___System_UIntPtr->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_UIntPtr;
	//		break;
	//	case Signature_ElementType_R4:
	//		fld->FieldType = dom->CachedType___System_Single->File->Assembly->Types[dom->CachedType___System_Single->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Single;
	//		break;
	//	case Signature_ElementType_R8:
	//		fld->FieldType = dom->CachedType___System_Double->File->Assembly->Types[dom->CachedType___System_Double->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Double;
	//		break;

	//	case Signature_ElementType_String:
	//		fld->FieldType = dom->CachedType___System_String->File->Assembly->Types[dom->CachedType___System_String->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_String;
	//		break;
	//	case Signature_ElementType_Type:
	//		fld->FieldType = dom->CachedType___System_Type->File->Assembly->Types[dom->CachedType___System_Type->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Type;
	//		break;
	//	case Signature_ElementType_Object:
	//		fld->FieldType = dom->CachedType___System_Object->File->Assembly->Types[dom->CachedType___System_Object->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Object;
	//		break;
	//	case Signature_ElementType_Void:
	//		fld->FieldType = dom->CachedType___System_Void->File->Assembly->Types[dom->CachedType___System_Void->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Void;
	//		break;
	//	case Signature_ElementType_TypedByReference:
	//		fld->FieldType = dom->CachedType___System_TypedReference->File->Assembly->Types[dom->CachedType___System_TypedReference->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_TypedReference;
	//		break;
	//	case Signature_ElementType_Array:
	//		fld->FieldType = dom->CachedType___System_Array->File->Assembly->Types[dom->CachedType___System_Array->TableIndex - 1];
	//		fld->FieldType = (IRType*)dom->CachedType___System_Array;
	//		break;
	//	case Signature_ElementType_ValueType:
	//	case Signature_ElementType_Class:
	//		{
	//			MetaDataToken* tok = CLIFile_ResolveTypeDefOrRefOrSpecToken(fil, sig->Type->ClassTypeDefOrRefOrSpecToken);
	//			switch (tok->Table)
	//			{
	//				case MetaData_Table_TypeDefinition:
	//					{
	//						fld->FieldType = (IRType*)((TypeDefinition*)tok->Data);
	//						break;
	//					}
	//				case MetaData_Table_TypeReference:
	//					{
	//						fld->FieldType = (IRType*)(((TypeReference*)tok->Data)->ResolvedType);
	//						break;
	//					}
	//				default:
	//					Panic("Unknown table for class lookup");
	//					break;
	//			}
	//			free(tok);
	//			break;
	//		}
	//		
	//	case Signature_ElementType_SingleDimensionArray:
	//	{
	//		fld->FieldType = (IRType*)dom->CachedType___System_Array;
	//		//IRType* sysArrayType = dom->IRAssemblies[0]->Types[dom->CachedType___System_Array->TableIndex - 1];
	//		//IRType* elementType = GetIRTypeOfSignatureType(dom, fil, asmbly, tp->SZArrayType);
	//		//IRType* arrayType = NULL;
	//		//IRArrayType* lookupType = NULL;
	//		//HASH_FIND(HashHandle, asmbly->ArrayTypesHashTable, (void*)&elementType, sizeof(void*), lookupType);
	//		//if (!lookupType)
	//		//{
	//		//	arrayType = IRType_Create();
	//		//	*arrayType = *sysArrayType; // Shallow copy everything first
	//		//	arrayType->ArrayType = IRArrayType_Create(elementType, arrayType);
	//		//	HASH_ADD(HashHandle, asmbly->ArrayTypesHashTable, ArrayElementType, sizeof(void*), arrayType->ArrayType);
	//		//}
	//		//else arrayType = lookupType->ArrayType;
	//		//return arrayType;
	//	}

	//	default:
	//		printf("Not setting the type of %s.%s!\n", def->TypeDefinition->Name, def->Name);
	//		break;
	//}
	FieldSignature_Destroy(sig);
	return fld;
}


IRType* GenerateType(TypeDefinition* def, CLIFile* fil, IRAssembly* asmb, AppDomain* dom)
{
	Log_WriteLine(LogFlags_ILReading_MethodLayout, "Generating %s.%s", def->Namespace, def->Name);
	IRType* tp = IRType_Create();
	tp->TypeDef = def;
	
	// Check if it's an interface.
	if (def->Flags & TypeAttributes_Interface)
	{
		tp->IsInterface = TRUE;
	}

	// Layout the methods.
	uint mthCount = 0;
	tp->Methods = TypeDefinition_GetLayedOutMethods(def, fil, &mthCount, dom);
	tp->MethodCount = mthCount;

	// Layout the fields
	uint fldCount = 0;
	tp->Fields = TypeDefinition_GetLayedOutFields(def, fil, &fldCount, dom);
	tp->FieldCount = fldCount;


	// Check if it's a generic type.
	if (def->GenericParameterCount > 0)
	{
		tp->IsGeneric = TRUE;
		tp->IsFixedSize = FALSE;
	}
	else
	{
		tp->IsGeneric = FALSE;
		tp->IsFixedSize = TRUE;
	}

	if (IsStruct(def, dom))
		tp->IsValueType = TRUE;
	else
		tp->IsReferenceType = TRUE;
	
	// Find the static constructor if it exists.
	for (uint32_t i = 0; i < def->MethodDefinitionListCount; i++)
	{
		if ((def->MethodDefinitionList[i].Flags & MethodAttributes_Static) != 0)
		{
			if (!strcmp(def->MethodDefinitionList[i].Name, ".cctor"))
			{
				tp->HasStaticConstructor = TRUE;
				// This gets resolved in the Link method.
				tp->StaticConstructor = (IRMethod*)def->MethodDefinitionList[i].TableIndex;
			}
		}
	}
	
	return tp;
}

IRField** TypeDefinition_GetLayedOutFields(TypeDefinition* tdef, CLIFile* fil, uint* destLength, AppDomain* dom)
{
	Log_WriteLine(LogFlags_ILReading_FieldLayout, "Laying out the fields of %s.%s, TypeOfExtends = %u", tdef->Namespace, tdef->Name, (unsigned int)tdef->TypeOfExtends);
	IRField** flds = NULL;
	uint fldsCount = 0;
	if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeDefinition)
	{
		if (tdef->Extends.TypeDefinition != NULL)
		{
			flds = TypeDefinition_GetLayedOutFields(tdef->Extends.TypeDefinition, fil, &fldsCount, dom);
		}
	}
	else if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeReference)
	{
		if (tdef->Extends.TypeReference != NULL)
		{
			flds = TypeDefinition_GetLayedOutFields(tdef->Extends.TypeReference->ResolvedType, tdef->Extends.TypeReference->ResolvedType->File, &fldsCount, dom);
		}
		else 
			Panic("TypeRef Extends NULL!");
	}
	else if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeSpecification)
	{
		*destLength = 0;
		return NULL;
	}

	uint32_t newFieldsCount = 0;
	for (uint32_t i = 0; i < tdef->FieldListCount; i++)
	{
		if (!(tdef->FieldList[i].Flags & FieldAttributes_Static))
		{
			newFieldsCount++;
		}
	}
	Log_WriteLine(LogFlags_ILReading_FieldLayout, "Field count: %i NewFieldCount: %i", (int)fldsCount, (int)newFieldsCount);

	IRField** fnlFields = (IRField**)calloc(fldsCount + newFieldsCount, sizeof(IRField*));
	for (uint32_t i = 0; i < fldsCount; i++)
	{
		fnlFields[i] = flds[i];
	}

	uint32_t fldIndex = 0;
	for (uint32_t i = 0; i < tdef->FieldListCount; i++)
	{
		if (!(tdef->FieldList[i].Flags & FieldAttributes_Static))
		{
			fnlFields[fldIndex + fldsCount] = (IRField*)&tdef->FieldList[i];
			Log_WriteLine(LogFlags_ILReading_FieldLayout, "Adding Field %s.%s.%s from table index %i at %i", tdef->Namespace, tdef->Name, tdef->FieldList[i].Name, (int)tdef->FieldList[i].TableIndex, fldIndex + fldsCount);
			fldIndex++;
		}
	}

	if (flds != NULL)
	{
		free(flds);
	}

	*destLength = fldsCount + newFieldsCount;
	return fnlFields;
}

IRMethod** TypeDefinition_GetLayedOutMethods(TypeDefinition* tdef, CLIFile* fil, uint* destLength, AppDomain* dom)
{
	Log_WriteLine(LogFlags_ILReading_MethodLayout, "Laying out the methods of %s.%s, TypeOfExtends = %u", tdef->Namespace, tdef->Name, (unsigned int)tdef->TypeOfExtends);
	IRMethod** mths = NULL;
	uint mthsCount = 0;
	if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeDefinition)
	{
		if (tdef->Extends.TypeDefinition != NULL)
		{
			mths = TypeDefinition_GetLayedOutMethods(tdef->Extends.TypeDefinition, fil, &mthsCount, dom);
		}	
	}
	else if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeReference)
	{
		if (tdef->Extends.TypeReference != NULL)
		{
			mths = TypeDefinition_GetLayedOutMethods(tdef->Extends.TypeReference->ResolvedType, tdef->Extends.TypeReference->ResolvedType->File, &mthsCount, dom);
		}
		else
			Panic("TypeRef Extends NULL!");
	}
	else if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeSpecification)
	{
		*destLength = 0;
		return NULL;
		//Panic("Type Spec!");
	}

	uint32_t newMethodsCount = 0;
	for (uint32_t i = 0; i < tdef->MethodDefinitionListCount; i++)
	{
		if (tdef->MethodDefinitionList[i].Flags & MethodAttributes_Virtual)
		{
			if (tdef->MethodDefinitionList[i].Flags & MethodAttributes_NewSlot)
			{
				newMethodsCount++;
			}
		}
		else
		{
			if (!(tdef->MethodDefinitionList[i].Flags & MethodAttributes_Static) && 
				!(tdef->MethodDefinitionList[i].Flags & MethodAttributes_RTSpecialName)
				)
			{
				newMethodsCount++;
			}
		}
	}
	Log_WriteLine(LogFlags_ILReading_MethodLayout, "Method count: %i NewMethodCount: %i", (int)mthsCount, (int)newMethodsCount);

	IRMethod** fnlMethods = (IRMethod**)calloc(mthsCount + newMethodsCount, sizeof(IRMethod*));
	for (uint32_t i = 0; i < mthsCount; i++)
	{
		fnlMethods[i] = mths[i];
	}

	uint32_t mthIndex = 0;
	for (uint32_t i = 0; i < tdef->MethodDefinitionListCount; i++)
	{
		if (tdef->MethodDefinitionList[i].Flags & MethodAttributes_Virtual)
		{
			if (tdef->MethodDefinitionList[i].Flags & MethodAttributes_NewSlot)
			{
				fnlMethods[mthIndex + mthsCount] = (IRMethod*)&tdef->MethodDefinitionList[i];
				Log_WriteLine(LogFlags_ILReading_MethodLayout, "Adding method %s.%s.%s from table index %i at %i", tdef->Namespace, tdef->Name, tdef->MethodDefinitionList[i].Name, (int)tdef->MethodDefinitionList[i].TableIndex, mthIndex + mthsCount);
				mthIndex++;
			}
			else
			{
				bool_t Found = FALSE;
				Log_WriteLine(LogFlags_ILReading_MethodLayout, "Looking for base method of %s.%s.%s (Table Index %i)", tdef->Namespace, tdef->Name, tdef->MethodDefinitionList[i].Name, (int)(tdef->MethodDefinitionList[i].TableIndex));
				for (uint32_t i2 = 0; i2 < mthsCount; i2++)
				{
					MethodDefinition* mthDef = (MethodDefinition*)mths[i2];
					if (mthDef->TableIndex)
					{
						Log_WriteLine(LogFlags_ILReading_MethodLayout, "Checking Method %s.%s.%s from table index %i", mthDef->TypeDefinition->Namespace, mthDef->TypeDefinition->Name, mthDef->Name, (int)mthDef->TableIndex);
						Log_WriteLine(LogFlags_ILReading_MethodLayout, "i2: %i", (int)i2);
					}
					else
					{
						Log_WriteLine(LogFlags_ILReading_MethodLayout, "Method Index 0! This shouldn't happen!");
					}

					if (!strcmp(tdef->MethodDefinitionList[i].Name, mthDef->Name))
					{
						Log_WriteLine(LogFlags_ILReading_MethodLayout, "Name is the same");
						if (Signature_Equals(tdef->MethodDefinitionList[i].Signature, tdef->MethodDefinitionList[i].SignatureLength, mthDef->Signature, mthDef->SignatureLength))
						{
							Log_WriteLine(LogFlags_ILReading_MethodLayout, "Found Match!");
							fnlMethods[i2] = (IRMethod*)&tdef->MethodDefinitionList[i]; 
							Log_WriteLine(LogFlags_ILReading_MethodLayout, "Overloading method %s.%s.%s from table index %i at %i", tdef->Namespace, tdef->Name, tdef->MethodDefinitionList[i].Name, (int)tdef->MethodDefinitionList[i].TableIndex, i2);
							Found = TRUE;
							break;
						}
					}
				}
				if (!Found)
				{
					Panic("Unable to resolve base method of override!");
				}
			}
		}
		else
		{
			if (!(tdef->MethodDefinitionList[i].Flags & MethodAttributes_Static) && 
				!(tdef->MethodDefinitionList[i].Flags & MethodAttributes_RTSpecialName)
				)
			{
				fnlMethods[mthIndex + mthsCount] = (IRMethod*)&tdef->MethodDefinitionList[i];
				Log_WriteLine(LogFlags_ILReading_MethodLayout, "Adding method %s.%s.%s from table index %i at %i", tdef->Namespace, tdef->Name, tdef->MethodDefinitionList[i].Name, (int)tdef->MethodDefinitionList[i].TableIndex, mthIndex + mthsCount);
				mthIndex++;
			}
			else
			{
				Log_WriteLine(LogFlags_ILReading_MethodLayout, "Ignoring method %s.%s.%s at table index %i", tdef->Namespace, tdef->Name, tdef->MethodDefinitionList[i].Name, (int)tdef->MethodDefinitionList[i].TableIndex);
			}
		}
	}

	if (mths != NULL)
	{
		free(mths);
	}

	*destLength = mthsCount + newMethodsCount;
	return fnlMethods;
}


void Link(IRAssembly* asmb)
{
	// Resolve field types.
	for (uint32_t i = 0; i < asmb->FieldCount; i++)
	{
		IRField* fld = asmb->Fields[i];
		//fld->FieldType = ((TypeDefinition*)fld->FieldType)->File->Assembly->Types[((TypeDefinition*)fld->FieldType)->TableIndex - 1];
		fld->ParentType = ((TypeDefinition*)fld->ParentType)->File->Assembly->Types[((TypeDefinition*)fld->ParentType)->TableIndex - 1];
	}
		
	// Resolve the static constructors 
	// for types.
	for (uint32_t i = 0; i < asmb->TypeCount; i++)
	{
		IRType* tp = asmb->Types[i];
		if (tp->HasStaticConstructor)
		{
			tp->StaticConstructor = asmb->Methods[(uint32_t)tp->StaticConstructor - 1];
		}
	}

	// Resolve the methods in the method
	// tables.
	for (uint32_t i = 0; i < asmb->TypeCount; i++)
	{
		IRType* tp = asmb->Types[i];
		MethodDefinition* methodDef = NULL;
		for (uint32_t i2 = 0; i2 < tp->MethodCount; i2++)
		{
			methodDef = (MethodDefinition*)tp->Methods[i2];
			tp->Methods[i2] = methodDef->File->Assembly->Methods[methodDef->TableIndex - 1];
		}
	}

	// Resolve the fields in the field
	// tables.
	for (uint32_t i = 0; i < asmb->TypeCount; i++)
	{
		IRType* tp = asmb->Types[i];
		Field* field = NULL;
		for (uint32_t i2 = 0; i2 < tp->FieldCount; i2++)
		{
			field = (Field*)tp->Fields[i2];
			tp->Fields[i2] = field->File->Assembly->Fields[field->TableIndex - 1];
		}
	}


	for (uint32_t i = 0; i < asmb->TypeCount; i++)
	{
		IRType* tp = asmb->Types[i];
		for (uint32_t i2 = 0; i2 < tp->TypeDef->InterfaceImplementationCount; i2++)
		{
			InterfaceImplementation* interfaceImpl = tp->TypeDef->InterfaceImplementations[i2];
			IRType* intfcTp = NULL;
			switch(interfaceImpl->TypeOfInterface)
			{
				case TypeDefOrRef_Type_TypeDefinition:
					intfcTp = asmb->Types[interfaceImpl->Interface.TypeDefinition->TableIndex - 1];
					break;
				case TypeDefOrRef_Type_TypeReference:
					intfcTp = interfaceImpl->Interface.TypeReference->ResolvedType->File->Assembly->Types[interfaceImpl->Interface.TypeReference->ResolvedType->TableIndex - 1];
					break;
				case TypeDefOrRef_Type_TypeSpecification:
					printf("Generic interface implementations aren't supported yet!\n");
					continue;
				default:
					Panic("Unknown type of extends for the type of an interface!");
					break;
			}
			IRInterfaceImpl* intrfc = (IRInterfaceImpl*)calloc(1, sizeof(IRInterfaceImpl));
			intrfc->InterfaceType = intfcTp;
			intrfc->MethodCount = intfcTp->MethodCount;
			intrfc->MethodIndexes = (uint32_t*)calloc(1, sizeof(uint32_t) * intfcTp->MethodCount);

			for (uint32_t i3 = 0; i3 < tp->TypeDef->MethodImplementationCount; i3++)
			{
				MethodImplementation* methodImpl = tp->TypeDef->MethodImplementations[i3];
				IRMethod* intfcMth = NULL;
				switch(methodImpl->TypeOfMethodDeclaration)
				{
					case MethodDefOrRef_Type_MethodDefinition:
						intfcMth = asmb->Methods[methodImpl->MethodDeclaration.MethodDefinition->TableIndex - 1];
						break;
					case MethodDefOrRef_Type_MemberReference:
						intfcMth = methodImpl->MethodDeclaration.MemberReference->Resolved.MethodDefinition->File->Assembly->Methods[methodImpl->MethodDeclaration.MemberReference->Resolved.MethodDefinition->TableIndex - 1];
						continue;
					default:
						Panic("Unknown type of extends for the type of an interface!");
						break;
				}
				// Make sure this is a method impl for the current interface.
				if (intfcMth->MethodDefinition->TypeDefinition == intfcTp->TypeDef)
				{
					for (uint32_t i4 = 0; i4 < intfcTp->MethodCount; i4++)
					{
						// Now find the method in the interface that this maps to.
						if (intfcMth == intfcTp->Methods[i4])
						{
							IRMethod* tdefMth = NULL;
							switch(methodImpl->TypeOfMethodBody)
							{
								case MethodDefOrRef_Type_MethodDefinition:
									tdefMth = asmb->Methods[methodImpl->MethodBody.MethodDefinition->TableIndex - 1];
									break;
								case MethodDefOrRef_Type_MemberReference:
									tdefMth = methodImpl->MethodBody.MemberReference->Resolved.MethodDefinition->File->Assembly->Methods[methodImpl->MethodBody.MemberReference->Resolved.MethodDefinition->TableIndex - 1];
									break;
								default:
									Panic("Unknown type of extends for the type of an interface!");
									break;
							}
							for (uint32_t i5 = 0; i5 < tp->MethodCount; i5++)
							{
								if (tp->Methods[i5] == tdefMth)
								{
									// We mow have the index in the interface  the index in the
									// typedef's VMT's.
									intrfc->MethodIndexes[i4] = i5;
									break;
								}
							}

							break;
						}
					}
				}
			}

			for (uint32_t i3 = 0; i3 < intrfc->MethodCount; i3++)
			{
				if (!intrfc->MethodIndexes[i3])
				{
					// Either it's trying to force you to implement System.Object.Finalize,
					// or it's not an explicitly layed out interface method.
					MethodDefinition* mthDef = intfcTp->Methods[i3]->MethodDefinition;
					bool_t FoundMethod = FALSE;
					for (uint32_t i4 = 0; i4 < tp->MethodCount; i4++)
					{
						MethodDefinition* mthDef2 = tp->Methods[i4]->MethodDefinition;
						if (mthDef2->TableIndex)
						{
							Log_WriteLine(LogFlags_ILReading, "Checking Method %s.%s.%s from table index %i", mthDef2->TypeDefinition->Namespace, mthDef2->TypeDefinition->Name, mthDef2->Name, (int)mthDef2->TableIndex);
							Log_WriteLine(LogFlags_ILReading, "i: %i", (int)i4);
						}
						else
						{
							Log_WriteLine(LogFlags_ILReading, "Method Index 0! This shouldn't happen!");
						}							
						if (!strcmp(mthDef->Name, mthDef2->Name))
						{
							if (mthDef->Flags & MethodAttributes_HideBySignature)
							{
								if (Signature_Equals(mthDef->Signature, mthDef->SignatureLength, mthDef2->Signature, mthDef2->SignatureLength))
								{
									intrfc->MethodIndexes[i3] = i4;
									FoundMethod = TRUE;
									break;
								}
							}
							else
							{
								intrfc->MethodIndexes[i3] = i4;
								FoundMethod = TRUE;
								break;
							}
						}
					}
					if (!FoundMethod)
					{
						printf("WARNING: Unable to resolve method for interface %s @ 0x%x implemented on %s\n", intfcTp->TypeDef->Name, (unsigned int)intfcTp->TypeDef, tp->TypeDef->Name);
					}
				}
			}

			HASH_ADD(HashHandle, tp->InterfaceTable, InterfaceType, sizeof(void*), intrfc);
		}
	}



}

IRMethod* GenerateMethod(MethodDefinition* methodDef, CLIFile* fil, IRAssembly* asmbly, AppDomain* dom)
{
	IRMethod* m = IRMethod_Create();
	m->ParentAssembly = asmbly;

	{   // Setup Parameters
		MethodSignature* sig = MethodSignature_Expand(methodDef->Signature, fil);
		if (!sig->ReturnType->Void)
		{
			m->Returns = TRUE;
			m->ReturnType = GetIRTypeOfSignatureType(dom, fil, asmbly, sig->ReturnType->Type);
		}

		if (sig->HasThis && !sig->ExplicitThis)
		{
			IRParameter* p = IRParameter_Create();
			if (IsStruct(methodDef->TypeDefinition, dom))
			{
				p->Type = asmbly->Types[dom->CachedType___System_IntPtr->TableIndex - 1];
			}
			else
			{
				p->Type = asmbly->Types[methodDef->TypeDefinition->TableIndex - 1];
			}
			IRMethod_AddParameter(m, p);
		}

		for (uint32_t i = 0; i < sig->ParameterCount; i++)
		{
			IRParameter* p = IRParameter_Create();
			p->Type = GetIRTypeOfSignatureType(dom, fil, asmbly, sig->Parameters[i]->Type);
			IRMethod_AddParameter(m, p);
		}

		MethodSignature_Destroy(sig);
	}

	
	{   // Setup internal call stuff.
		if (methodDef->ImplFlags & MethodImplAttributes_InternalCall)
		{
			m->AssembledMethod = (void(*)())methodDef->InternalCall;
		}
	}

	return m;
}

void ReadIL(uint8_t** dat, uint32_t len, MethodDefinition* methodDef, CLIFile* fil, AppDomain* dom, IRAssembly* asmbly, IRMethod* m)
{
	uint32_t CatchHandlerIndex = 0;
	uint32_t TryHandlerIndex = 0;
	uint32_t ExceptionHandlerCount = methodDef->ExceptionCount;
	uint32_t* ExceptionCatchStatementOffsets = (uint32_t*)calloc(ExceptionHandlerCount, sizeof(uint32_t));
	uint32_t* ExceptionCatchStatementLengths = (uint32_t*)calloc(ExceptionHandlerCount, sizeof(uint32_t));
	uint32_t* ExceptionTryStatementOffsets = (uint32_t*)calloc(ExceptionHandlerCount, sizeof(uint32_t));
	uint32_t* ExceptionTryStatementLengths = (uint32_t*)calloc(ExceptionHandlerCount, sizeof(uint32_t));
	for (uint32_t i = 0; i < ExceptionHandlerCount; i++)
	{
		Log_WriteLine(LogFlags_ILReading_ExceptionBlocks, "Catch handler %i (Length: 0x%x) at offset 0x%x", (int)i, (unsigned int)methodDef->Exceptions[i].HandlerLength, (unsigned int)methodDef->Exceptions[i].HandlerOffset);
		ExceptionCatchStatementOffsets[i] = methodDef->Exceptions[i].HandlerOffset;
		ExceptionCatchStatementLengths[i] = methodDef->Exceptions[i].HandlerLength;
		Log_WriteLine(LogFlags_ILReading_ExceptionBlocks, "Try statement %i (Length: 0x%x) at offset 0x%x", (int)i, (unsigned int)methodDef->Exceptions[i].TryLength, (unsigned int)methodDef->Exceptions[i].TryOffset);
		ExceptionTryStatementOffsets[i] = methodDef->Exceptions[i].TryOffset;
		ExceptionTryStatementOffsets[i] = methodDef->Exceptions[i].TryLength;
	}

    SyntheticStack* stack = SyntheticStack_Create();
    bool_t Constrained = FALSE;
	uint32_t ConstrainedToken = 0;
    bool_t No = FALSE;
    bool_t ReadOnly = FALSE;
    bool_t Tail = FALSE;
    bool_t UnAligned = FALSE;
    bool_t Volatile = FALSE;
    BranchCondition branch_Condition = (BranchCondition)0;
    uint32_t branch_Target;
	StackObject* branch_Arg1 = NULL;
	StackObject* branch_Arg2 = NULL;
	size_t orig = (size_t)(*dat);
    size_t CurInstructionBase;
    uint8_t b;
	
	{   // Setup Locals
		if (methodDef->Body.LocalVariableSignatureToken)
		{
			MetaDataToken* tok = CLIFile_ResolveToken(fil, methodDef->Body.LocalVariableSignatureToken);
			LocalsSignature* sig = LocalsSignature_Expand(((StandAloneSignature*)tok->Data)->Signature, fil);
			
			m->LocalVariableCount = sig->LocalVariableCount;
			m->LocalVariables = (IRLocalVariable**)calloc(m->LocalVariableCount, sizeof(IRLocalVariable*));
			for (uint32_t i = 0; i < m->LocalVariableCount; i++)
			{
				m->LocalVariables[i] = (IRLocalVariable*)calloc(1, sizeof(IRLocalVariable));
				m->LocalVariables[i]->VariableType = GetIRTypeOfSignatureType(dom, fil, asmbly, sig->LocalVariables[i]->Type);
				m->LocalVariables[i]->LocalVariableIndex = i;
			}

			LocalsSignature_Destroy(sig);
			free(tok);
		}
	}

    /*Log_WriteLine(LogFlags_ILReading, "Hex value of the method: ");
    while ((size_t)(*dat) - orig < len)
    {
        Log_WriteLine(LogFlags_ILReading, "%0.8x", (unsigned int)ReadUInt32(dat));
	}

	*dat = (uint8_t*)orig;*/
    while ((size_t)(*dat) - orig < len)
    {
        CurInstructionBase = ((size_t)*dat);
        Log_WriteLine(LogFlags_ILReading, "CurInstructionBase: %i", (int)CurInstructionBase);
		if (CatchHandlerIndex < ExceptionHandlerCount)
		{
			uint32_t curOffset = CurInstructionBase - orig;
			if (ExceptionCatchStatementOffsets[CatchHandlerIndex] == curOffset)
			{
				Log_WriteLine(LogFlags_ILReading_ExceptionBlocks, "Entered Catch Block %i (Length: 0x%x) at offset 0x%x", (int)(CatchHandlerIndex), (unsigned int)ExceptionCatchStatementLengths[CatchHandlerIndex], (unsigned int)ExceptionCatchStatementOffsets[CatchHandlerIndex]);
				// We're at the start of a catch handler,
				// so we need to push an exception object
				// to the top of the stack.
				StackObject* obj = StackObjectPool_Allocate();
				obj->Type = StackObjectType_ReferenceType;
				obj->NumericType = StackObjectNumericType_Ref;
				SyntheticStack_Push(stack, obj);

				CatchHandlerIndex++;
			}
		}
		if (TryHandlerIndex < ExceptionHandlerCount)
		{
			uint32_t curOffset = CurInstructionBase - orig;
			if (ExceptionTryStatementOffsets[TryHandlerIndex] == curOffset)
			{
				Log_WriteLine(LogFlags_ILReading_ExceptionBlocks, "Entered Try Block %i (Length: 0x%x) at offset 0x%x", (int)(TryHandlerIndex), (unsigned int)ExceptionTryStatementLengths[TryHandlerIndex], (unsigned int)ExceptionTryStatementOffsets[TryHandlerIndex]);
				TryHandlerIndex++;
			}
		}


        b = ReadUInt8(dat);
        switch (b)
        {
            case ILOpCode_Nop:				// 0x00
                Log_WriteLine(LogFlags_ILReading, "Read Nop");
                EMIT_IR(IROpCode_Nop);
                ClearFlags();
                break;
            case ILOpCode_Break:			// 0x01
                Log_WriteLine(LogFlags_ILReading, "Read Break");
                EMIT_IR(IROpCode_BreakForDebugger);
                ClearFlags();
                break;


            case ILOpCode_LdArg_0:			// 0x02
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdArg.0");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)0;
                    EMIT_IR_1ARG(IROpCode_Load_Parameter, dt);
					
                    MethodSignature* mthSig = MethodSignature_Expand(methodDef->Signature, fil);
					StackObject* obj = StackObjectPool_Allocate();
					if (mthSig->HasThis && !(mthSig->ExplicitThis))
					{
						if (methodDef->TypeDefinition->TypeOfExtends == TypeDefOrRef_Type_TypeDefinition &&
							methodDef->TypeDefinition->Extends.TypeDefinition == dom->CachedType___System_ValueType)
						{
							obj->Type = StackObjectType_ManagedPointer;
							obj->NumericType = StackObjectNumericType_ManagedPointer;
						}
						else if (methodDef->TypeDefinition->TypeOfExtends == TypeDefOrRef_Type_TypeReference &&
								 methodDef->TypeDefinition->Extends.TypeReference->ResolvedType == dom->CachedType___System_ValueType)
						{
							obj->Type = StackObjectType_ManagedPointer;
							obj->NumericType = StackObjectNumericType_ManagedPointer;
						}
						else
						{
							obj->Type = StackObjectType_ReferenceType;
							obj->NumericType = StackObjectNumericType_Ref;
						}
					}
					else
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[0]->Type, fil, dom);
					}
					MethodSignature_Destroy(mthSig);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdArg_1:			// 0x03
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdArg.1");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)1;
                    EMIT_IR_1ARG(IROpCode_Load_Parameter, dt);
					
                    MethodSignature* mthSig = MethodSignature_Expand(methodDef->Signature, fil);
					StackObject* obj = StackObjectPool_Allocate();
					if (mthSig->HasThis && !(mthSig->ExplicitThis))
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[0]->Type, fil, dom);
					}
					else
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[1]->Type, fil, dom);
					}
					MethodSignature_Destroy(mthSig);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdArg_2:			// 0x04
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdArg.2");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)2;
                    EMIT_IR_1ARG(IROpCode_Load_Parameter, dt);
					
                    MethodSignature* mthSig = MethodSignature_Expand(methodDef->Signature, fil);
					StackObject* obj = StackObjectPool_Allocate();
					if (mthSig->HasThis && !(mthSig->ExplicitThis))
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[1]->Type, fil, dom);
					}
					else
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[2]->Type, fil, dom);
					}
					MethodSignature_Destroy(mthSig);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdArg_3:			// 0x05
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdArg.3");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)3;
                    EMIT_IR_1ARG(IROpCode_Load_Parameter, dt);
					
                    MethodSignature* mthSig = MethodSignature_Expand(methodDef->Signature, fil);
					StackObject* obj = StackObjectPool_Allocate();
					if (mthSig->HasThis && !(mthSig->ExplicitThis))
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[2]->Type, fil, dom);
					}
					else
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[3]->Type, fil, dom);
					}
					MethodSignature_Destroy(mthSig);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdArg_S:			// 0x0E
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdArg.S");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt8(dat);
                    EMIT_IR_1ARG(IROpCode_Load_Parameter, dt);
					
                    MethodSignature* mthSig = MethodSignature_Expand(methodDef->Signature, fil);
					StackObject* obj = StackObjectPool_Allocate();
					if (mthSig->HasThis && !(mthSig->ExplicitThis))
					{
						if (*dt == 0)
						{
							if (methodDef->TypeDefinition->TypeOfExtends == TypeDefOrRef_Type_TypeDefinition &&
								methodDef->TypeDefinition->Extends.TypeDefinition == dom->CachedType___System_ValueType)
							{
								obj->Type = StackObjectType_ManagedPointer;
								obj->NumericType = StackObjectNumericType_ManagedPointer;
							}
							else if (methodDef->TypeDefinition->TypeOfExtends == TypeDefOrRef_Type_TypeReference &&
									 methodDef->TypeDefinition->Extends.TypeReference->ResolvedType == dom->CachedType___System_ValueType)
							{
								obj->Type = StackObjectType_ManagedPointer;
								obj->NumericType = StackObjectNumericType_ManagedPointer;
							}
							else
							{
								obj->Type = StackObjectType_ReferenceType;
								obj->NumericType = StackObjectNumericType_Ref;
							}
						}
						else
						{
							SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[(*dt) - 1]->Type, fil, dom);
						}
					}
					else
					{
						SetTypeOfStackObjectFromSigElementType(obj, mthSig->Parameters[*dt]->Type, fil, dom);
					}
					MethodSignature_Destroy(mthSig);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdArgA_S:			// 0x0F
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdArg.S");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt8(dat);
                    EMIT_IR_1ARG(IROpCode_Load_Parameter_Address, dt);
					
					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_ManagedPointer;
					obj->NumericType = StackObjectNumericType_ManagedPointer;
					SyntheticStack_Push(stack, obj);
				}
                ClearFlags();
                break;
                

            case ILOpCode_StArg_S:			// 0x10
				{
					Log_WriteLine(LogFlags_ILReading, "Read StArg.S");
					uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt8(dat);
                    EMIT_IR_1ARG(IROpCode_Store_Parameter, dt);

                    StackObjectPool_Release(SyntheticStack_Pop(stack));
				}
                ClearFlags();
                break;


            case ILOpCode_LdLoc_0:			// 0x06
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdLoc.0");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)0;
                    EMIT_IR_1ARG(IROpCode_Load_LocalVar, dt);
					

					ElementType mTp = (ElementType)NULL;
					GetElementTypeFromTypeDef(m->LocalVariables[0]->VariableType->TypeDef, dom, &mTp);
					StackObject* obj = StackObjectPool_Allocate();
					SetObjectTypeFromElementType(obj, mTp);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdLoc_1:			// 0x07
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdLoc.1");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)1;
                    EMIT_IR_1ARG(IROpCode_Load_LocalVar, dt);
					

					ElementType mTp = (ElementType)NULL;
					GetElementTypeFromTypeDef(m->LocalVariables[1]->VariableType->TypeDef, dom, &mTp);
					StackObject* obj = StackObjectPool_Allocate();
					SetObjectTypeFromElementType(obj, mTp);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdLoc_2:			// 0x08
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdLoc.2");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)2;
                    EMIT_IR_1ARG(IROpCode_Load_LocalVar, dt);
					

					ElementType mTp = (ElementType)NULL;
					GetElementTypeFromTypeDef(m->LocalVariables[2]->VariableType->TypeDef, dom, &mTp);
					StackObject* obj = StackObjectPool_Allocate();
					SetObjectTypeFromElementType(obj, mTp);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdLoc_3:			// 0x09
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdLoc.3");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)3;
                    EMIT_IR_1ARG(IROpCode_Load_LocalVar, dt);

					ElementType mTp = (ElementType)NULL;
					GetElementTypeFromTypeDef(m->LocalVariables[3]->VariableType->TypeDef, dom, &mTp);
					StackObject* obj = StackObjectPool_Allocate();
					SetObjectTypeFromElementType(obj, mTp);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;
            case ILOpCode_LdLoc_S:			// 0x11
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdLoc.S");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt8(dat);
                    EMIT_IR_1ARG(IROpCode_Load_LocalVar, dt);
					

					ElementType mTp = (ElementType)NULL;
					GetElementTypeFromTypeDef(m->LocalVariables[*dt]->VariableType->TypeDef, dom, &mTp);
					StackObject* obj = StackObjectPool_Allocate();
					SetObjectTypeFromElementType(obj, mTp);
					SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;


            case ILOpCode_LdLocA_S:			// 0x12
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdLocA.S");

                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt8(dat);
                    EMIT_IR_1ARG(IROpCode_Load_LocalVar_Address, dt);

                    StackObject* obj = StackObjectPool_Allocate();
                    obj->Type = StackObjectType_ManagedPointer;
                    obj->NumericType = StackObjectNumericType_ManagedPointer;
                    SyntheticStack_Push(stack, obj);
                }
                ClearFlags();
                break;


            case ILOpCode_StLoc_0:			// 0x0A
                DefineStLoc(0);
            case ILOpCode_StLoc_1:			// 0x0B
                DefineStLoc(1);
            case ILOpCode_StLoc_2:			// 0x0C
                DefineStLoc(2);
            case ILOpCode_StLoc_3:			// 0x0D
                DefineStLoc(3);
            case ILOpCode_StLoc_S:			// 0x13
                {
                    Log_WriteLine(LogFlags_ILReading, "Read StLoc.S");
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt8(dat);
                    EMIT_IR_1ARG(IROpCode_Store_LocalVar, dt);

                    StackObjectPool_Release(SyntheticStack_Pop(stack));
                }
                ClearFlags();
                break;


            case ILOpCode_LdNull:			// 0x14
				{
                    Log_WriteLine(LogFlags_ILReading, "Read LdNull");
					EMIT_IR(IROpCode_LoadNull);
					StackObject* obj = StackObjectPool_Allocate();
					obj->NumericType = StackObjectNumericType_Ref;
					obj->Type = StackObjectType_ReferenceType;
					SyntheticStack_Push(stack, obj);
				}
                ClearFlags();
                break;
            case ILOpCode_LdStr:			// 0x72
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdStr");
                    MetaDataToken* tkn = CLIFile_ResolveToken(fil, ReadUInt32(dat));
                    if (!tkn->IsUserString)
                        Panic("Invalid token after LdStr!");
					
					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_ReferenceType;
					obj->NumericType = StackObjectNumericType_Ref;
					SyntheticStack_Push(stack, obj);

					uint32_t* strLen = (uint32_t*)malloc(sizeof(uint32_t));
					uint8_t* str = (uint8_t*)MetaData_GetCompressedUnsigned((uint8_t*)tkn->Data, strLen);
					*strLen -= 1; // Remove the null terminator

					EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Load_String, strLen, str);
                }
                ClearFlags();
                break;

            case ILOpCode_Ldc_I4_M1:		// 0x15
                {
                    Log_WriteLine(LogFlags_ILReading, "Read Ldc.I4.M1");

                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)-1;
                    EMIT_IR_1ARG(IROpCode_LoadInt32_Val, dt);

                    StackObject* s = StackObjectPool_Allocate();
                    s->Type = StackObjectType_Int32;
                    s->NumericType = StackObjectNumericType_Int32;
                    SyntheticStack_Push(stack, s);
                }
                ClearFlags();
                break;
            case ILOpCode_Ldc_I4_0:			// 0x16
                DefineLdcI4(0);
            case ILOpCode_Ldc_I4_1:			// 0x17
                DefineLdcI4(1);
            case ILOpCode_Ldc_I4_2:			// 0x18
                DefineLdcI4(2);
            case ILOpCode_Ldc_I4_3:			// 0x19
                DefineLdcI4(3);
            case ILOpCode_Ldc_I4_4:			// 0x1A
                DefineLdcI4(4);
            case ILOpCode_Ldc_I4_5:			// 0x1B
                DefineLdcI4(5);
            case ILOpCode_Ldc_I4_6:			// 0x1C
                DefineLdcI4(6);
            case ILOpCode_Ldc_I4_7:			// 0x1D
                DefineLdcI4(7);
            case ILOpCode_Ldc_I4_8:			// 0x1E
                DefineLdcI4(8);
            case ILOpCode_Ldc_I4_S:			// 0x1F
                {
                    Log_WriteLine(LogFlags_ILReading, "Read Ldc.I4.S");
                    
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)((int32_t)((int8_t)ReadUInt8(dat)));
                    EMIT_IR_1ARG(IROpCode_LoadInt32_Val, dt);

                    StackObject* s = StackObjectPool_Allocate();
                    s->Type = StackObjectType_Int32;
                    s->NumericType = StackObjectNumericType_Int32;
                    SyntheticStack_Push(stack, s);
                }
                ClearFlags();
                break;
            case ILOpCode_Ldc_I4:			// 0x20
                {
                    Log_WriteLine(LogFlags_ILReading, "Read Ldc.I4");
                    
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt32(dat);
                    EMIT_IR_1ARG(IROpCode_LoadInt32_Val, dt);

                    StackObject* s = StackObjectPool_Allocate();
                    s->Type = StackObjectType_Int32;
                    s->NumericType = StackObjectNumericType_Int32;
                    SyntheticStack_Push(stack, s);
                }
                ClearFlags();
                break;
            case ILOpCode_Ldc_I8:			// 0x21
                {
                    Log_WriteLine(LogFlags_ILReading, "Read Ldc.I8");
                    
                    uint64_t* dt = (uint64_t*)malloc(sizeof(uint64_t));
                    *dt = (uint64_t)ReadUInt64(dat);
                    EMIT_IR_1ARG(IROpCode_LoadInt64_Val, dt);

                    StackObject* s = StackObjectPool_Allocate();
                    s->Type = StackObjectType_Int64;
                    s->NumericType = StackObjectNumericType_Int64;
                    SyntheticStack_Push(stack, s);
                }
                ClearFlags();
                break;
            case ILOpCode_Ldc_R4:			// 0x22
                {
                    Log_WriteLine(LogFlags_ILReading, "Read Ldc.R4");
                    
                    uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                    *dt = (uint32_t)ReadUInt32(dat);
                    EMIT_IR_1ARG(IROpCode_LoadFloat32_Val, dt);

                    StackObject* s = StackObjectPool_Allocate();
                    s->Type = StackObjectType_Float;
                    s->NumericType = StackObjectNumericType_Float32;
                    SyntheticStack_Push(stack, s);
                }
                ClearFlags();
                break;
            case ILOpCode_Ldc_R8:			// 0x23
                {
                    Log_WriteLine(LogFlags_ILReading, "Read Ldc.R8");
                    
                    uint64_t* dt = (uint64_t*)malloc(sizeof(uint64_t));
                    *dt = (uint64_t)ReadUInt64(dat);
                    EMIT_IR_1ARG(IROpCode_LoadFloat64_Val, dt);

                    StackObject* s = StackObjectPool_Allocate();
                    s->Type = StackObjectType_Float;
                    s->NumericType = StackObjectNumericType_Float64;
                    SyntheticStack_Push(stack, s);
                }
                ClearFlags();
                break;


            // 0x24 Doesn't exist
            case ILOpCode_Dup:				// 0x25
				{
					Log_WriteLine(LogFlags_ILReading, "Read Dup");
                
					StackObject* obj1 = SyntheticStack_Peek(stack);
					StackObject* obj2 = StackObjectPool_Allocate();
					obj2->NumericType = obj1->NumericType;
					obj2->Type = obj1->Type;
					SyntheticStack_Push(stack, obj2);

					ElementType* mType = (ElementType*)malloc(sizeof(ElementType));
					GetElementTypeOfStackObject(mType, obj2);
					EMIT_IR_1ARG(IROpCode_Dup, mType);
				}
                ClearFlags();
                break;
            case ILOpCode_Pop:				// 0x26
				{
					Log_WriteLine(LogFlags_ILReading, "Read Pop");
					ElementType* elemType = (ElementType*)malloc(sizeof(ElementType));
					StackObject* obj = SyntheticStack_Pop(stack);
					GetElementTypeOfStackObject(elemType, obj);
					if (*elemType == ElementType_DataType)
					{
						Panic("We don't currently track the exact type of stack objects, and as such, we can't do this yet!");
					}
					EMIT_IR_1ARG(IROpCode_Pop, elemType);
					StackObjectPool_Release(obj);
				}
                ClearFlags();
                break;


            case ILOpCode_Call:				// 0x28
				{
					Log_WriteLine(LogFlags_ILReading, "Read NFI-Call");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					MethodDefinition* mthDef = NULL;
					IRAssembly* parAssmbly = NULL;
					MethodSignature* sig = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							sig = MethodSignature_Expand(((MethodDefinition*)tok->Data)->Signature, fil);
							mthDef = (MethodDefinition*)tok->Data;
							parAssmbly = asmbly;
							break;

						case MetaData_Table_MemberReference:
							mthDef = ((MemberReference*)tok->Data)->Resolved.MethodDefinition;
							sig = MethodSignature_Expand(((MemberReference*)tok->Data)->Signature, fil);
							parAssmbly = mthDef->File->Assembly;
							break;
							
						case MetaData_Table_MethodSpecification:
							printf("Don't deal with this yet! Call with Method Spec!\n");
							if (((MethodSpecification*)tok->Data)->TypeOfMethod == MethodDefOrRef_Type_MethodDefinition)
							{
								sig = MethodSignature_Expand(((MethodSpecification*)tok->Data)->Method.MethodDefinition->Signature, fil);
							}
							else if (((MethodSpecification*)tok->Data)->TypeOfMethod == MethodDefOrRef_Type_MemberReference)
							{
								sig = MethodSignature_Expand(((MethodSpecification*)tok->Data)->Method.MemberReference->Signature, fil);
							}
							else
							{
								Panic("GO AWAY!");
							}
							break;

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Unknown Table for Call!");
							break;
					}
					if (sig->HasThis)
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					for (uint32_t i = 0; i < sig->ParameterCount; i++)
					{
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					}
					if (!(sig->ReturnType->Void))
					{
						StackObject* obj = StackObjectPool_Allocate();
						SetTypeOfStackObjectFromSigElementType(obj, sig->ReturnType->Type, fil, dom);
						SyntheticStack_Push(stack, obj);
					}
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							MethodSignature_Destroy(sig);
							break;
							
						case MetaData_Table_MethodSpecification:
							MethodSignature_Destroy(sig);
							break;

						case MetaData_Table_MemberReference:
							MethodSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for Call!");
							break;
					}
					free(tok);
					if (mthDef)
					{
						if (mthDef->InternalCall)
						{
							EMIT_IR_2ARG_NO_DISPOSE(IROpCode_Call_Internal, mthDef->InternalCall, parAssmbly->Methods[mthDef->TableIndex - 1]);
						}
						else// if ((mthDef->Flags & MethodAttributes_Static) || ((mthDef->Flags & MethodAttributes_Virtual) == 0) || (mthDef->Flags & MethodAttributes_RTSpecialName) || (mthDef->TypeDefinition->Flags & TypeAttributes_Sealed) || IsStruct(mthDef->TypeDefinition, dom))
						{
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Call_Absolute, parAssmbly->Methods[mthDef->TableIndex - 1]);
						}
						/*else
						{
							IRMethodSpec* mthSpec = IRMethodSpec_Create();
							Log_WriteLine(LogFlags_ILReading, "Looking for Method %s.%s.%s from table index %i", mthDef->TypeDefinition->Namespace, mthDef->TypeDefinition->Name, mthDef->Name, (int)mthDef->TableIndex);
							mthSpec->ParentType = asmbly->Types[mthDef->TypeDefinition->TableIndex - 1];
							bool_t FoundMethod = FALSE;
							for (uint32_t i = 0; i < mthSpec->ParentType->MethodCount; i++)
							{
								MethodDefinition* mthDef2 = mthSpec->ParentType->Methods[i]->MethodDefinition;
								if (mthDef2->TableIndex)
								{
									Log_WriteLine(LogFlags_ILReading, "Checking Method %s.%s.%s from table index %i", mthDef2->TypeDefinition->Namespace, mthDef2->TypeDefinition->Name, mthDef2->Name, (int)mthDef2->TableIndex);
									Log_WriteLine(LogFlags_ILReading, "i: %i", (int)i);
								}
								else
								{
									Log_WriteLine(LogFlags_ILReading, "Method Index 0! This shouldn't happen!");
								}							
								if (!strcmp(mthDef->Name, mthDef2->Name))
								{
									if (mthDef->Flags & MethodAttributes_HideBySignature)
									{
										if (Signature_Equals(mthDef->Signature, mthDef->SignatureLength, mthDef2->Signature, mthDef2->SignatureLength))
										{
											mthSpec->MethodIndex = i;
											FoundMethod = TRUE;
											break;
										}
									}
									else
									{
										mthSpec->MethodIndex = i;
										FoundMethod = TRUE;
										break;
									}
								}
							}
							if (!FoundMethod)
							{
								Panic("Unable to resolve method to call!");
							}
							EMIT_IR_1ARG(IROpCode_Call, mthSpec);
						}*/
					}
					else
					{
						// This is just a temporary thing
						// so that this can be the target
						// of a branch (although it shouldn't be).
						EMIT_IR(IROpCode_Nop);
					}
				}
                ClearFlags();
                break;
            case ILOpCode_CallI:			// 0x29
				{
					Log_WriteLine(LogFlags_ILReading, "Read NI-CallI");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					MethodSignature* sig = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							sig = MethodSignature_Expand(((MethodDefinition*)tok->Data)->Signature, fil);
							break;
							
						case MetaData_Table_MemberReference:
							sig = MethodSignature_Expand(((MemberReference*)tok->Data)->Signature, fil);
							break;

						case MetaData_Table_MethodSpecification:
							//printf("Don't deal with this yet! Call with table index: 0x%x\n", (unsigned int)tok->Table);
							if (((MethodSpecification*)tok->Data)->TypeOfMethod == MethodDefOrRef_Type_MethodDefinition)
							{
								sig = MethodSignature_Expand(((MethodSpecification*)tok->Data)->Method.MethodDefinition->Signature, fil);
							}
							else if (((MethodSpecification*)tok->Data)->TypeOfMethod == MethodDefOrRef_Type_MemberReference)
							{
								sig = MethodSignature_Expand(((MethodSpecification*)tok->Data)->Method.MemberReference->Signature, fil);
							}
							else
							{
								Panic("GO AWAY!");
							}
							break;

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Unknown Table for CallI!");
							break;
					}
					if (sig->HasThis)
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					for (uint32_t i = 0; i < sig->ParameterCount; i++)
					{
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					}
					if (!(sig->ReturnType->Void))
					{
						StackObject* obj = StackObjectPool_Allocate();
						SetTypeOfStackObjectFromSigElementType(obj, sig->ReturnType->Type, fil, dom);
						SyntheticStack_Push(stack, obj);
					}
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							MethodSignature_Destroy(sig);
							break;
							
						case MetaData_Table_MethodSpecification:
							MethodSignature_Destroy(sig);
							break;

						case MetaData_Table_MemberReference:
							MethodSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for CallI!");
							break;
					}
					free(tok);
				}
                
                ClearFlags();
                break;
            case ILOpCode_CallVirt:			// 0x6F
				{
					Log_WriteLine(LogFlags_ILReading, "Read NI-CallVirt");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					MethodSignature* sig = NULL;
					IRAssembly* parAssmbly = NULL;
					MethodDefinition* mthDef = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							sig = MethodSignature_Expand(((MethodDefinition*)tok->Data)->Signature, fil);
							mthDef = (MethodDefinition*)tok->Data;
							parAssmbly = asmbly;
							break;

						case MetaData_Table_MemberReference:
							mthDef = ((MemberReference*)tok->Data)->Resolved.MethodDefinition;
							sig = MethodSignature_Expand(((MemberReference*)tok->Data)->Signature, fil);
							parAssmbly = mthDef->File->Assembly;
							break;

						case MetaData_Table_MethodSpecification:
							//printf("Don't deal with this yet! Call with table index: 0x%x\n", (unsigned int)tok->Table);
							if (((MethodSpecification*)tok->Data)->TypeOfMethod == MethodDefOrRef_Type_MethodDefinition)
							{
								sig = MethodSignature_Expand(((MethodSpecification*)tok->Data)->Method.MethodDefinition->Signature, fil);
							}
							else if (((MethodSpecification*)tok->Data)->TypeOfMethod == MethodDefOrRef_Type_MemberReference)
							{
								sig = MethodSignature_Expand(((MethodSpecification*)tok->Data)->Method.MemberReference->Signature, fil);
							}
							else
							{
								Panic("GO AWAY!");
							}
							break;

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Unknown Table for Call!");
							break;
					}
					if (sig->HasThis)
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					for (uint32_t i = 0; i < sig->ParameterCount; i++)
					{
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					}
					if (!(sig->ReturnType->Void))
					{
						StackObject* obj = StackObjectPool_Allocate();
						SetTypeOfStackObjectFromSigElementType(obj, sig->ReturnType->Type, fil, dom);
						SyntheticStack_Push(stack, obj);
					}
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							MethodSignature_Destroy(sig);
							break;
						case MetaData_Table_MethodSpecification:
							MethodSignature_Destroy(sig);
							break;
						case MetaData_Table_MemberReference:
							MethodSignature_Destroy(sig);
							break;
						default:
							Panic("Unknown Table for Call!");
							break;
					}
					free(tok);


					if (mthDef)
					{
						if (mthDef->InternalCall)
						{
							EMIT_IR_2ARG_NO_DISPOSE(IROpCode_Call_Internal, mthDef->InternalCall, parAssmbly->Methods[mthDef->TableIndex - 1]);
						}
						else if (((mthDef->Flags & MethodAttributes_Virtual) == 0) || (mthDef->Flags & MethodAttributes_RTSpecialName) || (mthDef->TypeDefinition->Flags & TypeAttributes_Sealed) || IsStruct(mthDef->TypeDefinition, dom))
						{
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Call_Absolute, parAssmbly->Methods[mthDef->TableIndex - 1]);
						}
						else if (ConstrainedToken)
						{
							Panic("The Constrained prefix isn't currently supported!");
						}
						else
						{
							IRMethodSpec* mthSpec = IRMethodSpec_Create();
							Log_WriteLine(LogFlags_ILReading, "Looking for Method %s.%s.%s from table index %i", mthDef->TypeDefinition->Namespace, mthDef->TypeDefinition->Name, mthDef->Name, (int)mthDef->TableIndex);
							mthSpec->ParentType = parAssmbly->Types[mthDef->TypeDefinition->TableIndex - 1];
							bool_t FoundMethod = FALSE;
							for (uint32_t i = 0; i < mthSpec->ParentType->MethodCount; i++)
							{
								MethodDefinition* mthDef2 = mthSpec->ParentType->Methods[i]->MethodDefinition;
								if (mthDef2->TableIndex)
								{
									Log_WriteLine(LogFlags_ILReading, "Checking Method %s.%s.%s from table index %i", mthDef2->TypeDefinition->Namespace, mthDef2->TypeDefinition->Name, mthDef2->Name, (int)mthDef2->TableIndex);
									Log_WriteLine(LogFlags_ILReading, "i: %i", (int)i);
								}
								else
								{
									Log_WriteLine(LogFlags_ILReading, "Method Index 0! This shouldn't happen!");
								}							
								if (!strcmp(mthDef->Name, mthDef2->Name))
								{
									if (mthDef->Flags & MethodAttributes_HideBySignature)
									{
										if (Signature_Equals(mthDef->Signature, mthDef->SignatureLength, mthDef2->Signature, mthDef2->SignatureLength))
										{
											mthSpec->MethodIndex = i;
											FoundMethod = TRUE;
											break;
										}
									}
									else
									{
										mthSpec->MethodIndex = i;
										FoundMethod = TRUE;
										break;
									}
								}
							}
							if (!FoundMethod)
							{
								Panic("Unable to resolve method to call!");
							}
							EMIT_IR_1ARG(IROpCode_Call, mthSpec);
						}
					}
					else
					{
						// This is just a temporary thing
						// so that this can be the target
						// of a branch (although it shouldn't be).
						EMIT_IR(IROpCode_Nop);
					}
				}
                ClearFlags();
                break;
            case ILOpCode_Jmp:				// 0x27
			{
				EmptyStack(stack);
				MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
				MethodDefinition* mthDef = NULL;
				switch (tok->Table)
				{
					case MetaData_Table_MethodDefinition:
						mthDef = (MethodDefinition*)tok->Data;
						break;
					default:
						Panic("Invalid Table for Jmp!");
						break;
				}
				free(tok);
				if (mthDef)
				{
					if (mthDef->InternalCall)
					{
						Panic("Attempting to Jump into Internal Call");
					}
					else if ((mthDef->Flags & MethodAttributes_Static) || (mthDef->Flags & MethodAttributes_RTSpecialName) || (mthDef->TypeDefinition->Flags & TypeAttributes_Sealed) || IsStruct(mthDef->TypeDefinition, dom))
					{
						printf("Jumping via Call_Absolute, not fulling implemented\n");
						EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Call_Absolute, (IRMethod*)(mthDef->TableIndex));
					}
					else
					{
						IRMethodSpec* mthSpec = IRMethodSpec_Create();
						Log_WriteLine(LogFlags_ILReading, "Looking for Method %s.%s.%s from table index %i", mthDef->TypeDefinition->Namespace, mthDef->TypeDefinition->Name, mthDef->Name, (int)mthDef->TableIndex);
						mthSpec->ParentType = asmbly->Types[mthDef->TypeDefinition->TableIndex - 1];
						bool_t FoundMethod = FALSE;
						for (uint32_t i = 0; i < mthSpec->ParentType->MethodCount; i++)
						{
							MethodDefinition* mthDef2 = mthSpec->ParentType->Methods[i]->MethodDefinition;
							if (mthDef2->TableIndex)
							{
								Log_WriteLine(LogFlags_ILReading, "Checking Method %s.%s.%s from table index %i", mthDef2->TypeDefinition->Namespace, mthDef2->TypeDefinition->Name, mthDef2->Name, (int)mthDef2->TableIndex);
								Log_WriteLine(LogFlags_ILReading, "i: %i", (int)i);
							}
							else
							{
								Log_WriteLine(LogFlags_ILReading, "Method Index 0! This shouldn't happen!");
							}							
							if (!strcmp(mthDef->Name, mthDef2->Name))
							{
								if (mthDef->Flags & MethodAttributes_HideBySignature)
								{
									if (Signature_Equals(mthDef->Signature, mthDef->SignatureLength, mthDef2->Signature, mthDef2->SignatureLength))
									{
										mthSpec->MethodIndex = i;
										FoundMethod = TRUE;
										break;
									}
								}
								else
								{
									mthSpec->MethodIndex = i;
									FoundMethod = TRUE;
									break;
								}
							}
						}
						if (!FoundMethod)
						{
							Panic("Unable to resolve method to call!");
						}
						EMIT_IR_1ARG(IROpCode_Jump, mthSpec);
					}
				}
				else
				{
					// This is just a temporary thing
					// so that this can be the target
					// of a branch (although it shouldn't be).
					EMIT_IR(IROpCode_Nop);
				}
				ClearFlags();
                break;
			}

            case ILOpCode_Ret:				// 0x2A
                {
					Log_WriteLine(LogFlags_ILReading, "Read Ret");
                    MethodSignature* mthSig = MethodSignature_Expand(methodDef->Signature, fil);
					if (mthSig->ReturnType->Void)
					{
						bool_t* HasRetValue = (bool_t*)malloc(sizeof(bool_t));
						*HasRetValue = FALSE;

						EMIT_IR_1ARG(IROpCode_Return, HasRetValue);
					}
					else
					{
						StackObject* obj = SyntheticStack_Pop(stack);
						bool_t* HasRetValue = (bool_t*)malloc(sizeof(bool_t));
						*HasRetValue = TRUE;
						ElementType* mtType = (ElementType*)malloc(sizeof(ElementType));
						GetElementTypeOfStackObject(mtType, obj);
						StackObjectPool_Release(obj);

						EMIT_IR_2ARG(IROpCode_Return, HasRetValue, mtType);
					}
					MethodSignature_Destroy(mthSig);
                }
                ClearFlags();
                break;


            case ILOpCode_Br:				// 0x38
                DefineBranchTarget(BranchCondition_Always, Br);
            case ILOpCode_Br_S:				// 0x2B
                DefineBranchTarget_Short(BranchCondition_Always, Br);

            case ILOpCode_BrFalse:			// 0x39
                DefineBranchTarget(BranchCondition_False, BrFalse);
            case ILOpCode_BrFalse_S:		// 0x2C
                DefineBranchTarget_Short(BranchCondition_False, BrFalse);

            case ILOpCode_BrTrue:			// 0x3A
                DefineBranchTarget(BranchCondition_True, BrTrue);
            case ILOpCode_BrTrue_S:			// 0x2D
                DefineBranchTarget_Short(BranchCondition_True, BrTrue);

            case ILOpCode_Beq:				// 0x3B
                DefineBranchTarget(BranchCondition_Equal, Beq);
            case ILOpCode_Beq_S:			// 0x2E
                DefineBranchTarget_Short(BranchCondition_Equal, Beq);

            case ILOpCode_Bne_Un:			// 0x40
                DefineBranchTarget(BranchCondition_NotEqual_Unsigned, Bne.Un);
            case ILOpCode_Bne_Un_S:			// 0x33
                DefineBranchTarget_Short(BranchCondition_NotEqual_Unsigned, Bne.Un);

            case ILOpCode_Bge:				// 0x3C
                DefineBranchTarget(BranchCondition_Greater_Or_Equal, Bge);
            case ILOpCode_Bge_S:			// 0x2F
                DefineBranchTarget_Short(BranchCondition_Greater_Or_Equal, Bge);
            case ILOpCode_Bge_Un:			// 0x41
                DefineBranchTarget(BranchCondition_Greater_Or_Equal_Unsigned, Bge.Un);
            case ILOpCode_Bge_Un_S:			// 0x34
                DefineBranchTarget_Short(BranchCondition_Greater_Or_Equal_Unsigned, Bge.Un);

            case ILOpCode_Bgt:				// 0x3D
                DefineBranchTarget(BranchCondition_Greater, Bgt);
            case ILOpCode_Bgt_S:			// 0x30
                DefineBranchTarget_Short(BranchCondition_Greater, Bgt);
            case ILOpCode_Bgt_Un:			// 0x42
                DefineBranchTarget(BranchCondition_Greater_Unsigned, Bgt.Un);
            case ILOpCode_Bgt_Un_S:			// 0x35
                DefineBranchTarget_Short(BranchCondition_Greater_Unsigned, Bgt.Un);

            case ILOpCode_Ble:				// 0x3E
                DefineBranchTarget(BranchCondition_Less_Or_Equal, Ble);
            case ILOpCode_Ble_S:			// 0x31
                DefineBranchTarget_Short(BranchCondition_Less_Or_Equal, Ble);
            case ILOpCode_Ble_Un:			// 0x43
                DefineBranchTarget(BranchCondition_Less_Or_Equal_Unsigned, Ble.Un);
            case ILOpCode_Ble_Un_S:			// 0x36
                DefineBranchTarget_Short(BranchCondition_Less_Or_Equal_Unsigned, Ble.Un);

            case ILOpCode_Blt:				// 0x3F
                DefineBranchTarget(BranchCondition_Less, Blt);
            case ILOpCode_Blt_S:			// 0x32
                DefineBranchTarget_Short(BranchCondition_Less, Blt);
            case ILOpCode_Blt_Un:			// 0x44
                DefineBranchTarget(BranchCondition_Less_Unsigned, Blt.Un);
            case ILOpCode_Blt_Un_S:			// 0x37
                DefineBranchTarget_Short(BranchCondition_Less_Unsigned, Blt.Un);

Branch_Common:
                {
                    uint32_t* targt = (uint32_t*)malloc(sizeof(uint32_t));
                    *targt = ((((size_t)(*dat)) - orig) + ((int32_t)branch_Target));
                    BranchCondition* condtn = (BranchCondition*)malloc(sizeof(BranchCondition));
                    *condtn = branch_Condition;
					ElementType* arg1Type = (ElementType*)calloc(1, sizeof(ElementType));
					ElementType* arg2Type = (ElementType*)calloc(1, sizeof(ElementType));
					if (branch_Arg1)
					{
						GetElementTypeOfStackObject(arg1Type, branch_Arg1);
						StackObjectPool_Release(branch_Arg1);
						branch_Arg1 = NULL;
					}
					if (branch_Arg2)
					{
						GetElementTypeOfStackObject(arg2Type, branch_Arg2);
						StackObjectPool_Release(branch_Arg2);
						branch_Arg2 = NULL;
					}
                    EMIT_IR_4ARG(IROpCode_Branch, condtn, targt, arg1Type, arg2Type);
                }
                ClearFlags();
                break;


            case ILOpCode_Switch:			// 0x45
				{
					Log_WriteLine(LogFlags_ILReading, "Read Switch");
					uint32_t* count = (uint32_t*)malloc(sizeof(uint32_t));
					*count = ReadUInt32(dat);
					IRInstruction** targets = (IRInstruction**)calloc(*count, sizeof(IRInstruction*));
					uint32_t base = (((size_t)(*dat)) - orig) + (4 * *count);

					for (uint32_t i = 0; i < *count; i++)
					{
						targets[i] = (IRInstruction*)(base + ((int32_t)ReadUInt32(dat)));
					}

					EMIT_IR_2ARG(IROpCode_Switch, count, targets);
					
					StackObjectPool_Release(SyntheticStack_Pop(stack));

					ClearFlags();
					break;
				}
				

            case ILOpCode_LdInd_I:			// 0x4D
				DefineLdInd(I);
            case ILOpCode_LdInd_I1:			// 0x46
				DefineLdInd(I1);
            case ILOpCode_LdInd_U1:			// 0x47
				DefineLdInd(U1);
            case ILOpCode_LdInd_I2:			// 0x48
				DefineLdInd(I2);
            case ILOpCode_LdInd_U2:			// 0x49
				DefineLdInd(U2);
            case ILOpCode_LdInd_I4:			// 0x4A
				DefineLdInd(I4);
            case ILOpCode_LdInd_U4:			// 0x4B
				DefineLdInd(U4);
            case ILOpCode_LdInd_I8:			// 0x4C
				DefineLdInd(I8);
            case ILOpCode_LdInd_R4:			// 0x4D
				DefineLdInd(R4);
            case ILOpCode_LdInd_R8:			// 0x4F
				DefineLdInd(R8);
            case ILOpCode_LdInd_Ref:		// 0x50
				DefineLdInd(Ref);

            case ILOpCode_StInd_I:			// 0xDF
				DefineStInd(I);
            case ILOpCode_StInd_I1:			// 0x52
				DefineStInd(I1);
            case ILOpCode_StInd_I2:			// 0x53
				DefineStInd(I2);
            case ILOpCode_StInd_I4:			// 0x54
				DefineStInd(I4);
            case ILOpCode_StInd_I8:			// 0x55
				DefineStInd(I8);
            case ILOpCode_StInd_R4:			// 0x56
				DefineStInd(R4);
            case ILOpCode_StInd_R8:			// 0x57
				DefineStInd(R8);
            case ILOpCode_StInd_Ref:		// 0x51
				DefineStInd(Ref);
                


            case ILOpCode_Add:				// 0x58
				DefineBinaryNumericOperation(Add, Add, None);
            case ILOpCode_Add_Ovf:			// 0xD6
				DefineBinaryNumericOperation(Add, Add.Ovf, Signed);
            case ILOpCode_Add_Ovf_Un:		// 0xD7
				DefineBinaryNumericOperation(Add, Add.Ovf.Un, Unsigned);

            case ILOpCode_Sub:				// 0x59
				DefineBinaryNumericOperation(Sub, Sub, None);
            case ILOpCode_Sub_Ovf:			// 0xDA
				DefineBinaryNumericOperation(Sub, Sub.Ovf, Signed);
            case ILOpCode_Sub_Ovf_Un:		// 0xDB
				DefineBinaryNumericOperation(Sub, Sub.Ovf.Un, Unsigned);
				
            case ILOpCode_Mul:				// 0x5A
				DefineBinaryNumericOperation(Mul, Mul, None);
            case ILOpCode_Mul_Ovf:			// 0xD8
				DefineBinaryNumericOperation(Mul, Mul.Ovf, Signed);
            case ILOpCode_Mul_Ovf_Un:		// 0xD9
				DefineBinaryNumericOperation(Mul, Mul.Ovf.Un, Unsigned);

            case ILOpCode_Div:				// 0x5B
				DefineBinaryNumericOperation(Div, Div, Signed);
            case ILOpCode_Div_Un:			// 0x5C
				DefineBinaryNumericOperation(Div, Div.Un, Unsigned);

            case ILOpCode_Rem:				// 0x5D
				DefineBinaryNumericOperation(Rem, Rem, Signed);
            case ILOpCode_Rem_Un:			// 0x5E
				DefineBinaryNumericOperation(Rem, Rem.Un, Unsigned);


            case ILOpCode_And:				// 0x5F
				DefineBitwiseIntegerOperation(And);
            case ILOpCode_Or:				// 0x60
				DefineBitwiseIntegerOperation(Or);
            case ILOpCode_Xor:				// 0x61
				DefineBitwiseIntegerOperation(XOr);


            case ILOpCode_Neg:				// 0x65
                DefineUnaryOperation(Neg);
            case ILOpCode_Not:				// 0x66
                DefineUnaryOperation(Not);


            case ILOpCode_Shl:				// 0x62
				DefineShift(Left, Shl);
            // The only language I know of that 
            // emits this currently is J#.
            // (This is a sign-extended right shift,
            // aka. the >>> operator)
            case ILOpCode_Shr:				// 0x63
				DefineShift(Right_Sign_Extended, Shr);
            case ILOpCode_Shr_Un:			// 0x64
				DefineShift(Right, Shr.Un);



            case ILOpCode_Conv_I1:			// 0x67
                DEFINE_CONV_UNCHECKED(I1, Int32, Int8);
            case ILOpCode_Conv_U1:			// 0xD2
                DEFINE_CONV_UNCHECKED(U1, Int32, UInt8);
            case ILOpCode_Conv_I2:			// 0x68
                DEFINE_CONV_UNCHECKED(I2, Int32, Int16);
            case ILOpCode_Conv_U2:			// 0xD1
                DEFINE_CONV_UNCHECKED(U2, Int32, UInt16);
            case ILOpCode_Conv_I4:			// 0x69
                DEFINE_CONV_UNCHECKED(I4, Int32, Int32);
            case ILOpCode_Conv_U4:			// 0x6D
                DEFINE_CONV_UNCHECKED(U4, Int32, UInt32);
            case ILOpCode_Conv_I8:			// 0x6A
                DEFINE_CONV_UNCHECKED(I8, Int64, Int64);
            case ILOpCode_Conv_U8:			// 0x6E
                DEFINE_CONV_UNCHECKED(U8, Int64, UInt64);
            case ILOpCode_Conv_R4:			// 0x6B
                DEFINE_CONV_UNCHECKED(R4, Float, Float32);
            case ILOpCode_Conv_R8:			// 0x6C
                DEFINE_CONV_UNCHECKED(R8, Float, Float64);
            case ILOpCode_Conv_I:			// 0xD3
                DEFINE_CONV_UNCHECKED(I, NativeInt, Pointer);
            case ILOpCode_Conv_U:			// 0xE0
                DEFINE_CONV_UNCHECKED(U, NativeInt, UPointer);

            case ILOpCode_Conv_R_Un:		// 0x76
                DefineUnSupportedOpCode(Conv.R.Un);
                ClearFlags();
                break;

            case ILOpCode_NewObj:			// 0x73
				{
					Log_WriteLine(LogFlags_ILReading, "Read NFI-NewObj");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					MethodSignature* sig = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
							{
								sig = MethodSignature_Expand(((MethodDefinition*)tok->Data)->Signature, fil);
								EMIT_IR_1ARG_NO_DISPOSE(IROpCode_NewObject, asmbly->Methods[((MethodDefinition*)tok->Data)->TableIndex - 1]);
								break;
							}
						case MetaData_Table_MemberReference:
							{
								MemberReference* membRef = (MemberReference*)tok->Data;
								sig = MethodSignature_Expand(membRef->Signature, fil);
								MethodDefinition* methodDef = membRef->Resolved.MethodDefinition;
								EMIT_IR_1ARG_NO_DISPOSE(IROpCode_NewObject, methodDef->File->Assembly->Methods[methodDef->TableIndex - 1]);
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Unknown Table for NewObj!");
							break;
					}
					for (uint32_t i = 0; i < sig->ParameterCount; i++)
					{
						StackObjectPool_Release(SyntheticStack_Pop(stack));
					}
					switch(tok->Table)
					{
						case MetaData_Table_MethodDefinition:
						case MetaData_Table_MemberReference:
							MethodSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for NewObj!");
							break;
					}
					free(tok);
	
					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_ReferenceType;
					obj->NumericType = StackObjectNumericType_Ref;
					SyntheticStack_Push(stack, obj);
				}
                ClearFlags();
                break;
            case ILOpCode_NewArr:			// 0x8D
				{
					Log_WriteLine(LogFlags_ILReading, "Read NewArr");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					switch (tok->Table)
					{
						case MetaData_Table_TypeDefinition:
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_NewArray, asmbly->Types[((TypeDefinition*)tok->Data)->TableIndex - 1]);
							break;
						case MetaData_Table_TypeReference:
							{
								TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
								EMIT_IR_1ARG_NO_DISPOSE(IROpCode_NewArray, typeDef->File->Assembly->Types[typeDef->TableIndex - 1]);
								break;
							}
						case MetaData_Table_TypeSpecification:
							printf("No idea what to do here!\n");
							break;
						default:
							Panic(String_Format("Unknown table (0x%x) for NewArr!", (unsigned int)tok->Table));
							break;
					}
					free(tok);
                
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObject* obj = StackObjectPool_Allocate();
					obj->NumericType = StackObjectNumericType_Ref;
					obj->Type = StackObjectType_ReferenceType;
					SyntheticStack_Push(stack, obj);
				}
                ClearFlags();
                break;

            case ILOpCode_CastClass:		// 0x74
				{
					Log_WriteLine(LogFlags_ILReading, "Read CastClass");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
							EMIT_IR_1ARG(IROpCode_CastClass, asmbly->Types[((TypeDefinition*)tok->Data)->TableIndex - 1]);
							break;
						case MetaData_Table_TypeReference:
							{
								TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
								EMIT_IR_1ARG(IROpCode_CastClass, typeDef->File->Assembly->Types[typeDef->TableIndex - 1]);
								break;
							}
						case MetaData_Table_TypeSpecification:
							printf("Don't know what to do here!\n");
							break;
						default:
							Panic("Unknown table for CastClass!");
							break;
					}
					free(tok);

					StackObject* obj = SyntheticStack_Pop(stack);
					obj->Type = StackObjectType_ReferenceType;
					obj->NumericType = StackObjectNumericType_Ref;
					SyntheticStack_Push(stack, obj);

	                
					ClearFlags();
					break;
				}
            case ILOpCode_IsInst:			// 0x75
				{
					Log_WriteLine(LogFlags_ILReading, "Read IsInst");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
							EMIT_IR_1ARG(IROpCode_IsInst, asmbly->Types[((TypeDefinition*)tok->Data)->TableIndex - 1]);
							break;
						case MetaData_Table_TypeReference:
							{
								TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
								EMIT_IR_1ARG(IROpCode_IsInst, typeDef->File->Assembly->Types[typeDef->TableIndex - 1]);
								break;
							}
						case MetaData_Table_TypeSpecification:
							printf("Don't know what to do here!\n");
							break;
						default:
							Panic(String_Format("Unknown table (0x%x) for IsInst!", (unsigned int)tok->Table));
							break;
					}
					free(tok);

					StackObject* obj = SyntheticStack_Pop(stack);
					obj->Type = StackObjectType_ReferenceType;
					obj->NumericType = StackObjectNumericType_Ref;
					SyntheticStack_Push(stack, obj);

	                
					ClearFlags();
					break;
				}

            // 0x77 Doesn't exist
            // 0x78 Doesn't exist
            case ILOpCode_UnBox:			// 0x79
				{
					Log_WriteLine(LogFlags_ILReading, "Read Unbox");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					StackObject* obj = SyntheticStack_Pop(stack);
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* tdef = (TypeDefinition*)tok->Data;
							if (IsStruct(tdef, dom))
							{
								obj->Type = StackObjectType_ManagedPointer;
								obj->NumericType = StackObjectNumericType_ManagedPointer;
							}
							else
							{
								obj->Type = StackObjectType_ReferenceType;
								obj->NumericType = StackObjectNumericType_Ref;
							}
							EMIT_IR_1ARG(IROpCode_Unbox, asmbly->Types[tdef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* tdef = ((TypeReference*)tok->Data)->ResolvedType;
							if (IsStruct(tdef, dom))
							{
								obj->Type = StackObjectType_ManagedPointer;
								obj->NumericType = StackObjectNumericType_ManagedPointer;
							}
							else
							{
								obj->Type = StackObjectType_ReferenceType;
								obj->NumericType = StackObjectNumericType_Ref;
							}
							EMIT_IR_1ARG(IROpCode_Unbox, tdef->File->Assembly->Types[tdef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeSpecification:
						{
							printf("WARNING: This is only a temporary hack, and might not work in the future.\n");
							obj->Type = StackObjectType_ReferenceType;
							obj->NumericType = StackObjectNumericType_Ref;
							//TypeSpecification* tspec = (TypeSpecification*)tok->Data;
							break;
						}
						default:
							Panic(String_Format("Unknown table (%x) for Unbox!", (unsigned int)tok->Table));
							break;
					}
					SyntheticStack_Push(stack, obj);
					free(tok);
					
					ClearFlags();
					break;
				}
            case ILOpCode_Unbox_Any:		// 0xA5
				{
					Log_WriteLine(LogFlags_ILReading, "Read Unbox.Any");
	
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					StackObject* obj = SyntheticStack_Pop(stack);
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* tdef = (TypeDefinition*)tok->Data;
							ElementType et = (ElementType)0;
							GetElementTypeFromTypeDef(tdef, dom, &et);
							SetObjectTypeFromElementType(obj, et);
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Unbox_Any, asmbly->Types[tdef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* tdef = ((TypeReference*)tok->Data)->ResolvedType;
							ElementType et = (ElementType)0;
							GetElementTypeFromTypeDef(tdef, dom, &et);
							SetObjectTypeFromElementType(obj, et);
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Unbox_Any, tdef->File->Assembly->Types[tdef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeSpecification:
						{
							printf("WARNING: This is only a temporary hack, and might not work in the future.\n");
							obj->Type = StackObjectType_ReferenceType;
							obj->NumericType = StackObjectNumericType_Ref;
							//TypeSpecification* tspec = (TypeSpecification*)tok->Data;
							break;
						}
						default:
							Panic(String_Format("Unknown table (%x) for Unbox.Any!", (unsigned int)tok->Table));
							break;
					}
					SyntheticStack_Push(stack, obj);
					free(tok);

					ClearFlags();
					break;
				}
				
            case ILOpCode_Box:				// 0x8C
				{
					Log_WriteLine(LogFlags_ILReading, "Read Box");

					StackObjectPool_Release(SyntheticStack_Pop(stack));

					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					TypeDefinition* tdef = NULL;
					StackObject* obj = StackObjectPool_Allocate();
					switch (tok->Table)
					{
						case MetaData_Table_TypeDefinition:
							tdef = (TypeDefinition*)tok->Data;
							if ((tdef->Flags & TypeAttributes_Interface) == 0)
							{
								obj->Type = StackObjectType_ReferenceType;
								obj->NumericType = StackObjectNumericType_Ref;
							}
							else
							{
								switch(tdef->TypeOfExtends)
								{
									case TypeDefOrRef_Type_TypeDefinition:
										if (tdef->Extends.TypeDefinition == dom->CachedType___System_ValueType)
										{
											ElementType elmType;
											GetElementTypeFromTypeDef(tdef, dom, &elmType);
											SetObjectTypeFromElementType(obj, elmType);
										}
										else if (tdef->Extends.TypeDefinition == dom->CachedType___System_Enum)
										{
											for (uint32_t i = 0; i < tdef->FieldListCount; i++) 
											{ 
												Field* fld = &(tdef->FieldList[i]); 
												if (!strcmp(fld->Name, "value__")) 
												{ 
													FieldSignature* sig2 = FieldSignature_Expand(fld->Signature, fil); 
													switch (sig2->Type->ElementType)
													{
														case Signature_ElementType_I1:
														case Signature_ElementType_I2:
														case Signature_ElementType_I4:
														case Signature_ElementType_I8:
														case Signature_ElementType_U1:
														case Signature_ElementType_U2:
														case Signature_ElementType_U4:
														case Signature_ElementType_U8:
															SetTypeOfStackObjectFromSigElementType(obj, sig2->Type, fil, dom);
															break;
														default:
															Panic("Invalid value type for an Enum!");
															break;
													}
													FieldSignature_Destroy(sig2);
													break;
												}
											}
										}
										else
										{
											Panic("Don't know what to do here!");						
										}
										break;

									default:
										Panic("Don't know what to do here!");
										break;
								}
							}
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Box, asmbly->Types[tdef->TableIndex - 1]);
							break;
						case MetaData_Table_TypeReference:
							tdef = ((TypeReference*)tok->Data)->ResolvedType;
							if ((tdef->Flags & TypeAttributes_Interface) == 0)
							{
								obj->Type = StackObjectType_ReferenceType;
								obj->NumericType = StackObjectNumericType_Ref;
							}
							else
							{
								switch(tdef->TypeOfExtends)
								{
									case TypeDefOrRef_Type_TypeDefinition:
										if (tdef->Extends.TypeDefinition == dom->CachedType___System_ValueType)
										{
											ElementType elmType;
											GetElementTypeFromTypeDef(tdef, dom, &elmType);
											SetObjectTypeFromElementType(obj, elmType);
										}
										else if (tdef->Extends.TypeDefinition == dom->CachedType___System_Enum)
										{
											for (uint32_t i = 0; i < tdef->FieldListCount; i++) 
											{ 
												Field* fld = &(tdef->FieldList[i]); 
												if (!strcmp(fld->Name, "value__")) 
												{ 
													FieldSignature* sig2 = FieldSignature_Expand(fld->Signature, fld->File); 
													switch (sig2->Type->ElementType)
													{
														case Signature_ElementType_I1:
														case Signature_ElementType_I2:
														case Signature_ElementType_I4:
														case Signature_ElementType_I8:
														case Signature_ElementType_U1:
														case Signature_ElementType_U2:
														case Signature_ElementType_U4:
														case Signature_ElementType_U8:
															SetTypeOfStackObjectFromSigElementType(obj, sig2->Type, fld->File, dom);
															break;
														default:
															Panic("Invalid value type for an Enum!");
															break;
													}
													FieldSignature_Destroy(sig2);
													break;
												}
											}
										}
										else
										{
											Panic("Don't know what to do here!");						
										}
										break;

									default:
										Panic("Don't know what to do here!");
										break;
								}
							}
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Box, tdef->File->Assembly->Types[tdef->TableIndex - 1]);
							break;
						case MetaData_Table_TypeSpecification:
							//tspec = (TypeSpecification*)tok->Data;
							// This is typically used for generics.
							
							obj->NumericType = StackObjectNumericType_Generic;
							obj->Type = StackObjectType_Generic;

							//SignatureType* sig = SignatureType_Expand(tspec->Signature, fil);
							
							//printf("Don't deal with this yet! Box with table index: 0x%x\n", (unsigned int)tok->Table);
							/*
							if (tspec->Flags & TypeAttributes_Class)
							{
								obj->Type = StackObjectType_ReferenceType;
								obj->NumericType = StackObjectNumericType_Ref;
							}
							else if (tspec->Extends.TypeDefinition == dom->CachedType___System_ValueType)
							{
								obj->Type = StackObjectType_ManagedPointer;
								obj->NumericType = StackObjectNumericType_ManagedPointer;
							}
							else
							{
								Panic("Don't know what to do here!");
							}
							*/
							break;

						//case MetaData_Table_MemberReference:
						//	sig = ((MemberReference*)tok->Data)->Signature, fil);

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Unknown Table for Box!");
							break;
					}
					SyntheticStack_Push(stack, obj);

					free(tok);	
				}
                ClearFlags();
                break;
            case ILOpCode_Throw:			// 0x7A
				Log_WriteLine(LogFlags_ILReading, "Read NI-Throw");
				
				StackObjectPool_Release(SyntheticStack_Pop(stack));
                
                ClearFlags();
                break;


            case ILOpCode_LdFld:			// 0x7B
				{
					Log_WriteLine(LogFlags_ILReading, "Read LdFld");
					StackObjectPool_Release(SyntheticStack_Pop(stack));

					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					FieldSignature* sig = NULL;
					Field* fld = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							{
								fld = (Field*)tok->Data;
								sig = FieldSignature_Expand(fld->Signature, fil);
								TypeDefinition* tdef = fld->TypeDefinition;
								bool_t Found = FALSE;
								for (uint32_t i = 0; i < tdef->FieldListCount; i++)
								{
									if (!strcmp(fld->Name, tdef->FieldList[i].Name))
									{
										if (Signature_Equals(fld->Signature, fld->SignatureLength, tdef->FieldList[i].Signature, tdef->FieldList[i].SignatureLength))
										{
											IRFieldSpec* spec = IRFieldSpec_Create();
											spec->FieldIndex = i;
											spec->ParentType = asmbly->Types[tdef->TableIndex - 1];
											spec->FieldType = GetIRTypeOfSignatureType(dom, fil, asmbly, sig->Type);
											EMIT_IR_1ARG(IROpCode_Load_Field, spec);
											Found = TRUE;
											break;
										}
									}
								}
								if (!Found)
									Panic("Unable to resolve field!");
							}
							break;

						case MetaData_Table_MemberReference:
							{
								fld = ((MemberReference*)tok->Data)->Resolved.Field;
								sig = FieldSignature_Expand(fld->Signature, fld->File);
								TypeDefinition* tdef = fld->TypeDefinition;
								bool_t Found = FALSE;
								for (uint32_t i = 0; i < tdef->FieldListCount; i++)
								{
									if (!strcmp(fld->Name, tdef->FieldList[i].Name))
									{
										if (Signature_Equals(fld->Signature, fld->SignatureLength, tdef->FieldList[i].Signature, tdef->FieldList[i].SignatureLength))
										{
											IRFieldSpec* spec = IRFieldSpec_Create();
											spec->FieldIndex = i;
											spec->ParentType = tdef->File->Assembly->Types[tdef->TableIndex - 1];
											spec->FieldType = GetIRTypeOfSignatureType(dom, tdef->File, tdef->File->Assembly, sig->Type);
											EMIT_IR_1ARG(IROpCode_Load_Field_Address, spec);
											Found = TRUE;
											break;
										}
									}
								}
								FieldSignature_Destroy(sig);
								if (!Found)
									Panic("Unable to resolve field!");
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Definitely not good");
							break;
					}
					/*if (fld)
					{
						printf("Loading field name: %s\n", fld->Name);
						printf("Custom Modifier Count: %i \n", (int)sig->CustomModifierCount);
						printf("Field Index: %i \n", (int)fld->TableIndex);
						printf("Element Type: 0x%x \n", (unsigned int)sig->Type->ElementType);
					}*/
					StackObject* obj = StackObjectPool_Allocate();
					SetTypeOfStackObjectFromSigElementType(obj, sig->Type, fil, dom);
					SyntheticStack_Push(stack, obj);
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							FieldSignature_Destroy(sig);
							break;

						case MetaData_Table_MemberReference:
							FieldSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for LdFld!");
							break;
					}
					free(tok);

					ClearFlags();
					break;
				}

            case ILOpCode_LdFldA:			// 0x7C
				{
					Log_WriteLine(LogFlags_ILReading, "Read LdFldA");
					StackObjectPool_Release(SyntheticStack_Pop(stack));

					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					FieldSignature* sig = NULL;
					Field* fld = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							{
								fld = (Field*)tok->Data;
								sig = FieldSignature_Expand(fld->Signature, fil);
								TypeDefinition* tdef = fld->TypeDefinition;
								bool_t Found = FALSE;
								for (uint32_t i = 0; i < tdef->FieldListCount; i++)
								{
									if (!strcmp(fld->Name, tdef->FieldList[i].Name))
									{
										if (Signature_Equals(fld->Signature, fld->SignatureLength, tdef->FieldList[i].Signature, tdef->FieldList[i].SignatureLength))
										{
											IRFieldSpec* spec = IRFieldSpec_Create();
											spec->FieldIndex = i;
											spec->ParentType = asmbly->Types[tdef->TableIndex - 1];
											spec->FieldType = GetIRTypeOfSignatureType(dom, fil, asmbly, sig->Type);
											EMIT_IR_1ARG(IROpCode_Load_Field_Address, spec);
											Found = TRUE;
											break;
										}
									}
								}
								if (!Found)
									Panic("Unable to resolve field!");
							}
							break;

						case MetaData_Table_MemberReference:
							{
								fld = ((MemberReference*)tok->Data)->Resolved.Field;
								sig = FieldSignature_Expand(fld->Signature, fld->File);
								TypeDefinition* tdef = fld->TypeDefinition;
								bool_t Found = FALSE;
								for (uint32_t i = 0; i < tdef->FieldListCount; i++)
								{
									if (!strcmp(fld->Name, tdef->FieldList[i].Name))
									{
										if (Signature_Equals(fld->Signature, fld->SignatureLength, tdef->FieldList[i].Signature, tdef->FieldList[i].SignatureLength))
										{
											IRFieldSpec* spec = IRFieldSpec_Create();
											spec->FieldIndex = i;
											spec->ParentType = tdef->File->Assembly->Types[tdef->TableIndex - 1];
											spec->FieldType = GetIRTypeOfSignatureType(dom, tdef->File, tdef->File->Assembly, sig->Type);
											EMIT_IR_1ARG(IROpCode_Load_Field_Address, spec);
											Found = TRUE;
											break;
										}
									}
								}
								FieldSignature_Destroy(sig);
								if (!Found)
									Panic("Unable to resolve field!");
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Definitely not good");
							break;
					}
					free(tok);

					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_NativeInt;
					obj->NumericType = StackObjectNumericType_Pointer;
					SyntheticStack_Push(stack, obj);

					ClearFlags();
					break;
				}

            case ILOpCode_StFld:			// 0x7D
				{
					Log_WriteLine(LogFlags_ILReading, "Read StFld");
					
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					FieldSignature* sig = NULL;
					Field* fld = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							{
								fld = (Field*)tok->Data;
								sig = FieldSignature_Expand(fld->Signature, fil);
								TypeDefinition* tdef = fld->TypeDefinition;
								bool_t Found = FALSE;
								for (uint32_t i = 0; i < tdef->FieldListCount; i++)
								{
									if (!strcmp(fld->Name, tdef->FieldList[i].Name))
									{
										if (Signature_Equals(fld->Signature, fld->SignatureLength, tdef->FieldList[i].Signature, tdef->FieldList[i].SignatureLength))
										{
											IRFieldSpec* spec = IRFieldSpec_Create();
											spec->FieldIndex = i;
											spec->ParentType = asmbly->Types[tdef->TableIndex - 1];
											spec->FieldType = GetIRTypeOfSignatureType(dom, fil, asmbly, sig->Type);
											EMIT_IR_1ARG(IROpCode_Store_Field, spec);
											Found = TRUE;
											break;
										}
									}
								}
								FieldSignature_Destroy(sig);
								if (!Found)
									Panic("Unable to resolve field!");
							}
							break;

						case MetaData_Table_MemberReference:
							{
								fld = ((MemberReference*)tok->Data)->Resolved.Field;
								sig = FieldSignature_Expand(fld->Signature, fld->File);
								TypeDefinition* tdef = fld->TypeDefinition;
								bool_t Found = FALSE;
								for (uint32_t i = 0; i < tdef->FieldListCount; i++)
								{
									if (!strcmp(fld->Name, tdef->FieldList[i].Name))
									{
										if (Signature_Equals(fld->Signature, fld->SignatureLength, tdef->FieldList[i].Signature, tdef->FieldList[i].SignatureLength))
										{
											IRFieldSpec* spec = IRFieldSpec_Create();
											spec->FieldIndex = i;
											spec->ParentType = tdef->File->Assembly->Types[tdef->TableIndex - 1];
											spec->FieldType = GetIRTypeOfSignatureType(dom, tdef->File, tdef->File->Assembly, sig->Type);
											EMIT_IR_1ARG(IROpCode_Store_Field, spec);
											Found = TRUE;
											break;
										}
									}
								}
								FieldSignature_Destroy(sig);
								if (!Found)
									Panic("Unable to resolve field!");
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Definitely not good");
							break;
					}
					free(tok);

					
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));
                
					ClearFlags();
					break;
				}

            case ILOpCode_LdSFld:			// 0x7E
				{
					Log_WriteLine(LogFlags_ILReading, "Read LdSFld");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					FieldSignature* sig = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							sig = FieldSignature_Expand(((Field*)tok->Data)->Signature, fil);
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Static_Field, asmbly->Fields[((Field*)tok->Data)->TableIndex - 1]);
							break;

						case MetaData_Table_MemberReference:
							{
								Field* field = ((MemberReference*)tok->Data)->Resolved.Field;
								sig = FieldSignature_Expand(field->Signature, field->File);
								EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Static_Field, field->File->Assembly->Fields[field->TableIndex - 1]);
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Definitely not good");
							break;
					}
					StackObject* obj = StackObjectPool_Allocate();
					SetTypeOfStackObjectFromSigElementType(obj, sig->Type, fil, dom);
					SyntheticStack_Push(stack, obj);

					switch(tok->Table)
					{
						case MetaData_Table_Field:
							FieldSignature_Destroy(sig);
							break;

						case MetaData_Table_MemberReference:
							FieldSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for LdSFld!");
							break;
					}
					free(tok);
	                
					ClearFlags();
					break;
				}

            case ILOpCode_LdSFldA:			// 0x7F
				{
					Log_WriteLine(LogFlags_ILReading, "Read LdSFldA");
					
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					FieldSignature* sig = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							sig = FieldSignature_Expand(((Field*)tok->Data)->Signature, fil);
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Static_Field_Address, asmbly->Fields[((Field*)tok->Data)->TableIndex - 1]);
							break;

						case MetaData_Table_MemberReference:
							{
								Field* field = ((MemberReference*)tok->Data)->Resolved.Field;
								sig = FieldSignature_Expand(field->Signature, field->File);
								EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Static_Field_Address, field->File->Assembly->Fields[field->TableIndex - 1]);
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Definitely not good");
							break;
					}

					switch(tok->Table)
					{
						case MetaData_Table_Field:
							FieldSignature_Destroy(sig);
							break;

						case MetaData_Table_MemberReference:
							FieldSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for LdSFldA!");
							break;
					}
					free(tok);

					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_NativeInt;
					obj->NumericType = StackObjectNumericType_UPointer;
					SyntheticStack_Push(stack, obj);
					ClearFlags();
					break;
				}

            case ILOpCode_StSFld:			// 0x80
				{
					Log_WriteLine(LogFlags_ILReading, "Read StSFld");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					FieldSignature* sig = NULL;
					switch(tok->Table)
					{
						case MetaData_Table_Field:
							sig = FieldSignature_Expand(((Field*)tok->Data)->Signature, fil);
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Store_Static_Field, asmbly->Fields[((Field*)tok->Data)->TableIndex - 1]);
							break;

						case MetaData_Table_MemberReference:
							{
								Field* field = ((MemberReference*)tok->Data)->Resolved.Field;
								sig = FieldSignature_Expand(field->Signature, field->File);
								EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Store_Static_Field, field->File->Assembly->Fields[field->TableIndex - 1]);
								break;
							}

						default:
							printf("Table: 0x%x\n", (unsigned int)tok->Table);
							Panic("Definitely not good");
							break;
					}

					switch(tok->Table)
					{
						case MetaData_Table_Field:
							FieldSignature_Destroy(sig);
							break;

						case MetaData_Table_MemberReference:
							FieldSignature_Destroy(sig);
							break;

						default:
							Panic("Unknown Table for StSFld!");
							break;
					}
					free(tok);

					StackObjectPool_Release(SyntheticStack_Pop(stack));


					ClearFlags();
					break;
				}

            case ILOpCode_LdObj:			// 0x71
                {
                    Log_WriteLine(LogFlags_ILReading, "Read LdObj");
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					StackObject* obj = StackObjectPool_Allocate();
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
		                    ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeFromTypeDef(typeDef, dom, et);
							SetObjectTypeFromElementType(obj, *et);
							IRType* type = asmbly->Types[typeDef->TableIndex - 1];
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Load_Object, et, type);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
		                    ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeFromTypeDef(typeDef, dom, et);
							SetObjectTypeFromElementType(obj, *et);
							IRType* type = typeDef->File->Assembly->Types[typeDef->TableIndex - 1];
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Load_Object, et, type);
							break;
						}
						case MetaData_Table_TypeSpecification:
							printf("Not Sure what to do here!\n");
							break;
						default:
							Panic("Unknown table for LdObj!");
							break;
					}
					SyntheticStack_Push(stack, obj);
					free(tok);

					ClearFlags();
	                break;			
                }
			
			case ILOpCode_StObj:			// 0x81
				{
					Log_WriteLine(LogFlags_ILReading, "Read StObj");
					uint32_t typeToken = ReadUInt32(dat);
					MetaDataToken* tok = CLIFile_ResolveToken(fil, typeToken);
					switch (tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
		                    ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeFromTypeDef(typeDef, dom, et);
							IRType* type = asmbly->Types[typeDef->TableIndex - 1];
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Store_Object, et, type);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
		                    ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeFromTypeDef(typeDef, dom, et);
							IRType* type = typeDef->File->Assembly->Types[typeDef->TableIndex - 1];
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Store_Object, et, type);
							break;
						}
						case MetaData_Table_TypeSpecification:
							printf("Not Sure what to do here!\n");
							break;
						default:
							Panic("Unknown Table for StObj!");
							break;
					}
					free(tok);
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));


					ClearFlags();
					break;
				}

            case ILOpCode_CpObj:			// 0x70
				{
					Log_WriteLine(LogFlags_ILReading, "Read CpObj");
					uint32_t typeToken = ReadUInt32(dat);
					MetaDataToken* tok = CLIFile_ResolveToken(fil, typeToken);
					switch (tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
		                    ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeFromTypeDef(typeDef, dom, et);
							IRType* type = asmbly->Types[typeDef->TableIndex - 1];
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Copy_Object, et, type);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
		                    ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeFromTypeDef(typeDef, dom, et);
							IRType* type = typeDef->File->Assembly->Types[typeDef->TableIndex - 1];
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_Copy_Object, et, type);
							break;
						}
						case MetaData_Table_TypeSpecification:
							printf("Not Sure what to do here!\n");
							break;
						default:
							Panic("Unknown Table for CpObj!");
							break;
					}
					free(tok);
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));


					ClearFlags();
					break;
				}

            case ILOpCode_Conv_Ovf_I1:		// 0xB3
                DEFINE_CONV_OVF(I1, Int32, Int8);
            case ILOpCode_Conv_Ovf_I1_Un:	// 0x82
                DEFINE_CONV_OVF_UN(I1, Int32, Int8);
            case ILOpCode_Conv_Ovf_U1:		// 0xB4
                DEFINE_CONV_OVF(U1, Int32, UInt8);
            case ILOpCode_Conv_Ovf_U1_Un:	// 0x86
                DEFINE_CONV_OVF_UN(U1, Int32, UInt8);
            case ILOpCode_Conv_Ovf_I2:		// 0xB5
                DEFINE_CONV_OVF(I2, Int32, Int16);
            case ILOpCode_Conv_Ovf_I2_Un:	// 0x83
                DEFINE_CONV_OVF_UN(I2, Int32, Int16);
            case ILOpCode_Conv_Ovf_U2:		// 0xB6
                DEFINE_CONV_OVF(U2, Int32, UInt16);
            case ILOpCode_Conv_Ovf_U2_Un:	// 0x87
                DEFINE_CONV_OVF_UN(U2, Int32, UInt16);
            case ILOpCode_Conv_Ovf_I4:		// 0xB7
                DEFINE_CONV_OVF(I4, Int32, Int32);
            case ILOpCode_Conv_Ovf_I4_Un:	// 0x84
                DEFINE_CONV_OVF_UN(I4, Int32, Int32);
            case ILOpCode_Conv_Ovf_U4:		// 0xB8
                DEFINE_CONV_OVF(U4, Int32, UInt32);
            case ILOpCode_Conv_Ovf_U4_Un:	// 0x88
                DEFINE_CONV_OVF_UN(U4, Int32, UInt32);
            case ILOpCode_Conv_Ovf_I8:		// 0xB9
                DEFINE_CONV_OVF(I8, Int64, Int64);
            case ILOpCode_Conv_Ovf_I8_Un:	// 0x85
                DEFINE_CONV_OVF_UN(I8, Int64, Int64);
            case ILOpCode_Conv_Ovf_U8:		// 0xBA
                DEFINE_CONV_OVF(U8, Int64, UInt64);
            case ILOpCode_Conv_Ovf_U8_Un:	// 0x89
                DEFINE_CONV_OVF_UN(U8, Int64, UInt64);
            case ILOpCode_Conv_Ovf_I:		// 0xD4
                DEFINE_CONV_OVF(I, NativeInt, Pointer);
            case ILOpCode_Conv_Ovf_I_Un:	// 0x8A
                DEFINE_CONV_OVF_UN(I, NativeInt, Pointer);
            case ILOpCode_Conv_Ovf_U:		// 0xD5
                DEFINE_CONV_OVF(U, NativeInt, UPointer);
            case ILOpCode_Conv_Ovf_U_Un:	// 0x8B
                DEFINE_CONV_OVF_UN(U, NativeInt, UPointer);


            case ILOpCode_LdLen:			// 0x8E
				{
					Log_WriteLine(LogFlags_ILReading, "Read LdLen");
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_NativeInt;
					obj->NumericType = StackObjectNumericType_UPointer;
					SyntheticStack_Push(stack, obj);
					EMIT_IR(IROpCode_Load_Array_Length);
				} 
                ClearFlags();
                break;
            case ILOpCode_LdElemA:			// 0x8F
				{
					Log_WriteLine(LogFlags_ILReading, "Read NI-LdElemA");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Element_Address, (void*)asmbly->Types[typeDef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Element_Address, (void*)typeDef->File->Assembly->Types[typeDef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeSpecification:
						{
							printf("This isn't implemented.\n");
							break;
						}
						default:
							Panic(String_Format("Unknown table (%x) for StElem!", (unsigned int)tok->Table));
							break;
					}
					free(tok);
				
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));
                
					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_NativeInt;
					obj->NumericType = StackObjectNumericType_UPointer;
					SyntheticStack_Push(stack, obj);
					
	                ClearFlags();
					break;
				}


            case ILOpCode_LdElem_I1:		// 0x90
				DefineLdElem(I1, Int32, Int8);
            case ILOpCode_LdElem_U1:		// 0x91
				DefineLdElem(U1, Int32, UInt8);
            case ILOpCode_LdElem_I2:		// 0x92
				DefineLdElem(I2, Int32, Int16);
            case ILOpCode_LdElem_U2:		// 0x93
				DefineLdElem(U2, Int32, UInt16);
            case ILOpCode_LdElem_I4:		// 0x94
				DefineLdElem(I4, Int32, Int32);
            case ILOpCode_LdElem_U4:		// 0x95
				DefineLdElem(U4, Int32, UInt32);
			// This is also known as LdElem.U8
            case ILOpCode_LdElem_I8:		// 0x96
				DefineLdElem(I8, Int64, Int64);
            case ILOpCode_LdElem_I:			// 0x97
				DefineLdElem(I, NativeInt, Pointer);
            case ILOpCode_LdElem_R4:		// 0x98
				DefineLdElem(R4, Float, Float32);
            case ILOpCode_LdElem_R8:		// 0x99
				DefineLdElem(R8, Float, Float64);
            case ILOpCode_LdElem_Ref:		// 0x9A
				DefineLdElem(Ref, ReferenceType, Ref);


            case ILOpCode_LdElem:			// 0xA3
				{
					Log_WriteLine(LogFlags_ILReading, "Read AFI-LdElem");
					
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					StackObject* obj = StackObjectPool_Allocate();
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
							ElementType tp = ElementType_Ref;
							GetElementTypeFromTypeDef(typeDef, dom, &tp);
							SetObjectTypeFromElementType(obj, tp);

							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Element_Evil, (void*)asmbly->Types[typeDef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
							ElementType tp = ElementType_Ref;
							GetElementTypeFromTypeDef(typeDef, dom, &tp);
							SetObjectTypeFromElementType(obj, tp);

							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Load_Element_Evil, (void*)typeDef->File->Assembly->Types[typeDef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeSpecification:
						{
							printf("This isn't fully valid, but it will work for now.\n");
							obj->Type = StackObjectType_ReferenceType;
							obj->NumericType = StackObjectNumericType_Ref;
							break;
						}
						default:
							Panic(String_Format("Unknown table (%x) for LdElem!", (unsigned int)tok->Table));
							break;
					}
					SyntheticStack_Push(stack, obj);
					free(tok);
					
					//DefineUnSupportedOpCode(LdElem);
					ClearFlags();
					break;
				}

            case ILOpCode_StElem_I:			// 0x9B
				DefineStElem(I);
            case ILOpCode_StElem_I1:		// 0x9C
				DefineStElem(I1);
            case ILOpCode_StElem_I2:		// 0x9D
				DefineStElem(I2);
            case ILOpCode_StElem_I4:		// 0x9E
				DefineStElem(I4);
            case ILOpCode_StElem_I8:		// 0x9F
				DefineStElem(I8);
            case ILOpCode_StElem_R4:		// 0xA0
				DefineStElem(R4);
            case ILOpCode_StElem_R8:		// 0xA1
				DefineStElem(R8);
            case ILOpCode_StElem_Ref:		// 0xA2
				DefineStElem(Ref);


            case ILOpCode_StElem:			// 0xA4
				{
					Log_WriteLine(LogFlags_ILReading, "Read NFI-StElem");
					MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
					switch(tok->Table)
					{
						case MetaData_Table_TypeDefinition:
						{
							TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Store_Element_Evil, (void*)asmbly->Types[typeDef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeReference:
						{
							TypeDefinition* typeDef = ((TypeReference*)tok->Data)->ResolvedType;
							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_Store_Element_Evil, (void*)typeDef->File->Assembly->Types[typeDef->TableIndex - 1]);
							break;
						}
						case MetaData_Table_TypeSpecification:
						{
							printf("This isn't implemented.\n");
							break;
						}
						default:
							Panic(String_Format("Unknown table (%x) for StElem!", (unsigned int)tok->Table));
							break;
					}
					free(tok);

					
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					StackObjectPool_Release(SyntheticStack_Pop(stack));
					//DefineUnSupportedOpCode(StElem);
                
					ClearFlags();
					break;
				}


            // 0xA6 Doesn't exist
            // 0xA7 Doesn't exist
            // 0xA8 Doesn't exist
            // 0xA9 Doesn't exist
            // 0xAA Doesn't exist
            // 0xAB Doesn't exist
            // 0xAC Doesn't exist
            // 0xAD Doesn't exist
            // 0xAE Doesn't exist
            // 0xAF Doesn't exist
            // 0xB0 Doesn't exist
            // 0xB1 Doesn't exist
            // 0xB2 Doesn't exist
            // 0xBB Doesn't exist
            // 0xBC Doesn't exist
            // 0xBD Doesn't exist
            // 0xBE Doesn't exist
            // 0xBF Doesn't exist
            // 0xC0 Doesn't exist
            // 0xC1 Doesn't exist
            case ILOpCode_RefAnyVal:		// 0xC2
                DefineUnSupportedOpCode(RefAnyVal);
                ClearFlags();
                break;
            case ILOpCode_CkFinite:			// 0xC3
				Log_WriteLine(LogFlags_ILReading, "Read CkFinite");
				EMIT_IR(IROpCode_CheckFinite);
                ClearFlags();
				break;
            // 0xC4 Doesn't exist
            // 0xC5 Doesn't exist
            case ILOpCode_MkRefAny:			// 0xC6
                DefineUnSupportedOpCode(MkRefAny);
                ClearFlags();
                break;
            // 0xC7 Doesn't exist
            // 0xC8 Doesn't exist
            // 0xC9 Doesn't exist
            // 0xCA Doesn't exist
            // 0xCB Doesn't exist
            // 0xCC Doesn't exist
            // 0xCD Doesn't exist
            // 0xCE Doesn't exist
            // 0xCF Doesn't exist
            case ILOpCode_LdToken:			// 0xD0
				{
					Log_WriteLine(LogFlags_ILReading, "Read NI-LdToken");
					ReadUInt32(dat);

					StackObject* obj = StackObjectPool_Allocate();
					obj->Type = StackObjectType_ReferenceType;
					obj->NumericType = StackObjectNumericType_Ref;
					SyntheticStack_Push(stack, obj);
                
					ClearFlags();
					break;
				}
            case ILOpCode_EndFinally:		// 0xDC
				Log_WriteLine(LogFlags_ILReading, "Read NI-EndFinally");
                
				EMIT_IR(IROpCode_Nop);
                ClearFlags();
                break;
            case ILOpCode_Leave:			// 0xDD
				Log_WriteLine(LogFlags_ILReading, "Read NI-Leave");
                ReadUInt32(dat);
                
				EmptyStack(stack);

				EMIT_IR(IROpCode_Nop);
                
                ClearFlags();
                break;
            case ILOpCode_Leave_S:			// 0xDE
				Log_WriteLine(LogFlags_ILReading, "Read NI-Leave.S");
                ReadUInt8(dat);
				
				EmptyStack(stack);
                
				EMIT_IR(IROpCode_Nop);


                ClearFlags();
                break;
            // 0xE1 Doesn't Exist
            // 0xE2 Doesn't Exist
            // 0xE3 Doesn't Exist
            // 0xE4 Doesn't Exist
            // 0xE5 Doesn't Exist
            // 0xE6 Doesn't Exist
            // 0xE7 Doesn't Exist
            // 0xE8 Doesn't Exist
            // 0xE9 Doesn't Exist
            // 0xEA Doesn't Exist
            // 0xEB Doesn't Exist
            // 0xEC Doesn't Exist
            // 0xED Doesn't Exist
            // 0xEE Doesn't Exist
            // 0xEF Doesn't Exist
            // 0xF0 Doesn't Exist
            // 0xF1 Doesn't Exist
            // 0xF2 Doesn't Exist
            // 0xF3 Doesn't Exist
            // 0xF4 Doesn't Exist
            // 0xF5 Doesn't Exist
            // 0xF6 Doesn't Exist
            // 0xF7 Doesn't Exist
            // 0xF8 Doesn't Exist
            // 0xF9 Doesn't Exist
            // 0xFA Doesn't Exist
            // 0xFB Doesn't Exist
            // 0xFC Doesn't Exist
            // 0xFD Doesn't Exist
            case ILOpCode_Extended:         // 0xFE
                b = ReadUInt8(dat);

                switch (b)
                {
                    case ILOpCodes_Extended_ArgList:		// 0x00
                        DefineUnSupportedOpCode(ArgList);
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_Ceq:			// 0x01
						DefineCompare(Equal, Ceq);
                    case ILOpCodes_Extended_Cgt:			// 0x02
						DefineCompare(Greater_Than, Cgt);
                    case ILOpCodes_Extended_Cgt_Un:			// 0x03
						DefineCompare(Greater_Than_Unsigned, Cgt.Un);
                    case ILOpCodes_Extended_Clt:			// 0x04
						DefineCompare(Less_Than, Clt);
                    case ILOpCodes_Extended_Clt_Un:			// 0x05
						DefineCompare(Less_Than_Unsigned, Clt.Un);
                    case ILOpCodes_Extended_LdFtn:			// 0x06
						{
							Log_WriteLine(LogFlags_ILReading, "Read LdFtn");
							IRMethod* method = NULL;
							MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
							switch (tok->Table)
							{
								case MetaData_Table_MethodDefinition:
									{
										MethodDefinition* methodDef = (MethodDefinition*)tok->Data;
										method = asmbly->Methods[methodDef->TableIndex - 1];
										break;
									}
								case MetaData_Table_MemberReference:
									{
										MemberReference* membRef = (MemberReference*)tok->Data;
										MethodDefinition* methodDef = membRef->Resolved.MethodDefinition;
										method = methodDef->File->Assembly->Methods[methodDef->TableIndex - 1];
										break;
									}
								default:
									Panic("Unknown Table for LdFtn");
									break;
							}
							free(tok);

							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_LoadFunction, method);

							StackObject* obj = StackObjectPool_Allocate();
							obj->Type = StackObjectType_ReferenceType;
							obj->NumericType = StackObjectNumericType_Ref;
							SyntheticStack_Push(stack, obj);

							ClearFlags();
							break;
						}
                    case ILOpCodes_Extended_LdVirtFtn:		// 0x07
						{
							Log_WriteLine(LogFlags_ILReading, "Read LdVirtFtn");
							MethodDefinition* mthDef = NULL;
							MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
							switch (tok->Table)
							{
								case MetaData_Table_MethodDefinition:
									{
										mthDef = (MethodDefinition*)tok->Data;
										break;
									}
								case MetaData_Table_MemberReference:
									{
										MemberReference* membRef = (MemberReference*)tok->Data;
										mthDef = membRef->Resolved.MethodDefinition;
										break;
									}
								default:
									Panic("Unknown Table for LdVirtFtn");
									break;
							}
							free(tok);

							if (mthDef)
							{
								IRMethodSpec* mthSpec = IRMethodSpec_Create();
								Log_WriteLine(LogFlags_ILReading, "Looking for Method %s.%s.%s from table index %i", mthDef->TypeDefinition->Namespace, mthDef->TypeDefinition->Name, mthDef->Name, (int)mthDef->TableIndex);
								mthSpec->ParentType = mthDef->File->Assembly->Types[mthDef->TypeDefinition->TableIndex - 1];
								bool_t FoundMethod = FALSE;
								for (uint32_t i = 0; i < mthSpec->ParentType->MethodCount; i++)
								{
									MethodDefinition* mthDef2 = mthSpec->ParentType->Methods[i]->MethodDefinition;
									if (mthDef2->TableIndex)
									{
										Log_WriteLine(LogFlags_ILReading, "Checking Method %s.%s.%s from table index %i", mthDef2->TypeDefinition->Namespace, mthDef2->TypeDefinition->Name, mthDef2->Name, (int)mthDef2->TableIndex);
										Log_WriteLine(LogFlags_ILReading, "i: %i", (int)i);
									}
									else
									{
										Log_WriteLine(LogFlags_ILReading, "Method Index 0! This shouldn't happen!");
									}							
									if (!strcmp(mthDef->Name, mthDef2->Name))
									{
										if (mthDef->Flags & MethodAttributes_HideBySignature)
										{
											if (Signature_Equals(mthDef->Signature, mthDef->SignatureLength, mthDef2->Signature, mthDef2->SignatureLength))
											{
												mthSpec->MethodIndex = i;
												FoundMethod = TRUE;
												break;
											}
										}
										else
										{
											mthSpec->MethodIndex = i;
											FoundMethod = TRUE;
											break;
										}
									}
								}
								if (!FoundMethod)
								{
									Panic("Unable to resolve method to call!");
								}
								EMIT_IR_1ARG(IROpCode_Load_Virtual_Function, mthSpec);
							}
							else
							{
								// This is just a temporary thing
								// so that this can be the target
								// of a branch (although it shouldn't be).
								EMIT_IR(IROpCode_Nop);
							}

							StackObject* obj = StackObjectPool_Allocate();
							obj->Type = StackObjectType_ReferenceType;
							obj->NumericType = StackObjectNumericType_Ref;
							SyntheticStack_Push(stack, obj);

							ClearFlags();
							break;
						}
                    // 0x08 Doesn't exist
                    case ILOpCodes_Extended_LdArg:			// 0x09
						{
							Log_WriteLine(LogFlags_ILReading, "Read LdArg");
							uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
							*dt = (uint32_t)ReadUInt16(dat);
							EMIT_IR_1ARG(IROpCode_Load_Parameter, dt);
							
							ElementType mTp = (ElementType)NULL;
							GetElementTypeFromTypeDef(m->Parameters[*dt]->Type->TypeDef, dom, &mTp);
							StackObject* obj = StackObjectPool_Allocate();
							SetObjectTypeFromElementType(obj, mTp);
							SyntheticStack_Push(stack, obj);
						}
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_LdArgA:			// 0x0A
						{
							Log_WriteLine(LogFlags_ILReading, "Read LdArg");
							uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
							*dt = (uint32_t)ReadUInt16(dat);
							EMIT_IR_1ARG(IROpCode_Load_Parameter_Address, dt);
							
							StackObject* obj = StackObjectPool_Allocate();
							obj->Type = StackObjectType_ManagedPointer;
							obj->NumericType = StackObjectNumericType_ManagedPointer;
							SyntheticStack_Push(stack, obj);
						}
                        ClearFlags();
                        break;

                    case ILOpCodes_Extended_StArg:			// 0x0B
						{
							Log_WriteLine(LogFlags_ILReading, "Read StArg");
							uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
							*dt = (uint32_t)ReadUInt16(dat);
							EMIT_IR_1ARG(IROpCode_Store_Parameter, dt);

							StackObjectPool_Release(SyntheticStack_Pop(stack));
						}
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_LdLoc:			// 0x0C
                        {
							Log_WriteLine(LogFlags_ILReading, "Read LdLoc");
							uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
							*dt = (uint32_t)ReadUInt16(dat);
							EMIT_IR_1ARG(IROpCode_Load_LocalVar, dt);
							
							ElementType mTp = (ElementType)NULL;
							GetElementTypeFromTypeDef(m->LocalVariables[*dt]->VariableType->TypeDef, dom, &mTp);
							StackObject* obj = StackObjectPool_Allocate();
							SetObjectTypeFromElementType(obj, mTp);
							SyntheticStack_Push(stack, obj);
                        }
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_LdLocA:			// 0x0D
                        {
                            Log_WriteLine(LogFlags_ILReading, "Read LdLocA");

                            uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                            *dt = (uint32_t)ReadUInt16(dat);
                            EMIT_IR_1ARG(IROpCode_Load_LocalVar_Address, dt);

                            StackObject* obj = StackObjectPool_Allocate();
                            obj->Type = StackObjectType_ManagedPointer;
                            obj->NumericType = StackObjectNumericType_ManagedPointer;
                            SyntheticStack_Push(stack, obj);
                        }
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_StLoc:			// 0x0E
                        {
                            Log_WriteLine(LogFlags_ILReading, "Read StLoc");
                            uint32_t* dt = (uint32_t*)malloc(sizeof(uint32_t));
                            *dt = (uint32_t)ReadUInt16(dat);
                            EMIT_IR_1ARG(IROpCode_Store_LocalVar, dt);

                            StackObjectPool_Release(SyntheticStack_Pop(stack));
                        }
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_LocAlloc:		// 0x0F
						{
                            Log_WriteLine(LogFlags_ILReading, "Read LocAlloc");
							StackObject* obj = SyntheticStack_Pop(stack);
                            ElementType* et = (ElementType*)malloc(sizeof(ElementType));
                            GetElementTypeOfStackObject(et, obj);
							StackObjectPool_Release(obj);

                            EMIT_IR_1ARG(IROpCode_LocalAllocate, et);

							obj = StackObjectPool_Allocate();
							obj->Type = StackObjectType_NativeInt;
							obj->NumericType = StackObjectNumericType_UPointer;
							SyntheticStack_Push(stack, obj);
						}
                        ClearFlags();
                        break;
					// 0x10 Doesn't exist
                    case ILOpCodes_Extended_EndFilter:		// 0x11
                        DefineUnSupportedOpCode(EndFilter);
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_InitObj:		// 0x15
						{
							Log_WriteLine(LogFlags_ILReading, "Read InitObj");
							StackObject* obj = SyntheticStack_Pop(stack);
							ElementType* et = (ElementType*)malloc(sizeof(ElementType));
							GetElementTypeOfStackObject(et, obj);
							StackObjectPool_Release(obj);
							MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
							IRType* type = NULL;
							switch (tok->Table)
							{
								case MetaData_Table_TypeDefinition:
									{
										TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
										type = asmbly->Types[typeDef->TableIndex - 1];
										break;
									}
								case MetaData_Table_TypeReference:
									{
										TypeReference* typeRef = (TypeReference*)tok->Data;
										type = typeRef->ResolvedType->File->Assembly->Types[typeRef->ResolvedType->TableIndex - 1];
										break;
									}
								case MetaData_Table_TypeSpecification:
									{
										printf("InitObj requires TypeSpecification lookup\n");
										break;
									}
								default:
									Panic("Invalid Table for InitObj");
									break;
							}
							free(tok);
							EMIT_IR_2ARG_DISPOSE__NO_DISPOSE(IROpCode_InitObject, et, type);
							ClearFlags();
							break;
						}
                    case ILOpCodes_Extended_CpBlk:			// 0x17
                        DefineUnSupportedOpCode(CpBlk);
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_InitBlk:		// 0x18
                        DefineUnSupportedOpCode(InitBlk);
                        ClearFlags();
                        break;
                    case ILOpCodes_Extended_ReThrow:		// 0x1A
						DefineUnSupportedOpCode(ReThrow);
						ClearFlags();
                        break;
                    // 0x1B Doesn't exist
                    case ILOpCodes_Extended_SizeOf:			// 0x1C
                        {	
							Log_WriteLine(LogFlags_ILReading, "Read SizeOf");
							IRType* type = NULL;
							MetaDataToken* tok = CLIFile_ResolveToken(fil, ReadUInt32(dat));
							switch (tok->Table)
							{
								case MetaData_Table_TypeDefinition:
									{
										TypeDefinition* typeDef = (TypeDefinition*)tok->Data;
										type = asmbly->Types[typeDef->TableIndex - 1];
										break;
									}
								case MetaData_Table_TypeReference:
									{
										TypeReference* typeRef = (TypeReference*)tok->Data;
										type = typeRef->ResolvedType->File->Assembly->Types[typeRef->ResolvedType->TableIndex - 1];
										break;
									}
								case MetaData_Table_TypeSpecification:
									{
										printf("Need to handle TypeSpecification for SizeOf\n");
										break;
									}
								default:
									Panic("Unknown Table for SizeOf");
									break;
							}
							free(tok);

							EMIT_IR_1ARG_NO_DISPOSE(IROpCode_SizeOf, type);

							StackObject* obj = StackObjectPool_Allocate();
							obj->Type = StackObjectType_Int32;
							obj->NumericType = StackObjectNumericType_UInt32;
							SyntheticStack_Push(stack, obj);

							ClearFlags();
							break;
						}
                    case ILOpCodes_Extended_RefAnyType:		// 0x1D
                        
                        ClearFlags();
                        break;


                    case ILOpCodes_Extended_Constrained__:	// 0x16
						Log_WriteLine(LogFlags_ILReading, "Read Constrained");
						ConstrainedToken = ReadUInt32(dat);
						Constrained = TRUE;
                        break;
                    case ILOpCodes_Extended_No__:			// 0x19
                        No = TRUE;
                        break;
                    case ILOpCodes_Extended_ReadOnly__:		// 0x1E
                        ReadOnly = TRUE;
                        break;
                    case ILOpCodes_Extended_Tail__:			// 0x14
                        Tail = TRUE;
                        break;
                    case ILOpCodes_Extended_Unaligned__:	// 0x12
                        UnAligned = TRUE;
                        break;
                    case ILOpCodes_Extended_Volatile__:		// 0x13
                        Volatile = TRUE;
                        break;

                }
    			break;
    		// 0xFF Doesn't Exist
        }
    }
	SyntheticStack_Destroy(stack);
}

ALWAYS_INLINE uint8_t ReadUInt8(uint8_t** dat)
{
    uint8_t b = **dat;
    (*dat)++;
    return b;
}

ALWAYS_INLINE uint16_t ReadUInt16(uint8_t** dat)
{
	uint16_t i = *((uint16_t*)*dat);
    *dat += 2;
    return i;
}

ALWAYS_INLINE uint32_t ReadUInt32(uint8_t** dat)
{
	uint32_t i = *((uint32_t*)*dat);
    *dat += 4;
    return i;
}

ALWAYS_INLINE uint64_t ReadUInt64(uint8_t** dat)
{
    uint64_t i = *((uint64_t*)*dat);
    *dat += 8;
    return i;
}

ALWAYS_INLINE void GetElementTypeFromTypeDef(TypeDefinition* tdef, AppDomain* dom, ElementType* dst)
{
	if (
		(tdef == dom->CachedType___System_Byte)
	||  (tdef == dom->CachedType___System_Boolean)
	)
	{
		*dst = ElementType_U1;
	}
	else if (tdef == dom->CachedType___System_SByte)
	{
		*dst = ElementType_I1;
	}
	else if (
		(tdef == dom->CachedType___System_Char)
	 || (tdef == dom->CachedType___System_UInt16)
	)
	{
		*dst = ElementType_U2;
	}
	else if (tdef == dom->CachedType___System_Int16)
	{
		*dst = ElementType_I2;
	}
	else if (tdef == dom->CachedType___System_Single)
	{
		*dst = ElementType_R4;
	}
	else if (tdef == dom->CachedType___System_Int32)
	{
		*dst = ElementType_I4;
	}
	else if (tdef == dom->CachedType___System_UInt32)
	{
		*dst = ElementType_U4;
	}
	else if (tdef == dom->CachedType___System_Double)
	{
		*dst = ElementType_R8;
	}
	else if (tdef == dom->CachedType___System_Int64)
	{
		*dst = ElementType_I8;
	}
	else if (tdef == dom->CachedType___System_UInt64)
	{
		*dst = ElementType_U8;
	}
	else if (tdef == dom->CachedType___System_IntPtr)
	{
		*dst = ElementType_I;
	}
	else if (tdef == dom->CachedType___System_UIntPtr)
	{
		*dst = ElementType_U;
	}
	else if ((tdef->Flags & TypeAttributes_Interface) == 0)
	{
		*dst = ElementType_Ref;
	}
	else
	{
		*dst = ElementType_DataType;
	}
}
ALWAYS_INLINE void SetTypeOfStackObjectFromSigElementType(StackObject* obj, SignatureType* TypeSig, CLIFile* fil, AppDomain* dom)
{
	switch (TypeSig->ElementType) 
	{ 
		case Signature_ElementType_Boolean: 
		case Signature_ElementType_I1: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I1"); 
			obj->NumericType = StackObjectNumericType_Int8; 
			obj->Type = StackObjectType_Int32; 
			break; 
		case Signature_ElementType_I2: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I2"); 
			obj->NumericType = StackObjectNumericType_Int16; 
			obj->Type = StackObjectType_Int32; 
			break; 
		case Signature_ElementType_I4: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I4"); 
			obj->NumericType = StackObjectNumericType_Int32; 
			obj->Type = StackObjectType_Int32; 
			break; 
		case Signature_ElementType_I8: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I8"); 
			obj->NumericType = StackObjectNumericType_Int64; 
			obj->Type = StackObjectType_Int64; 
			break; 
		case Signature_ElementType_U1: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U1"); 
			obj->NumericType = StackObjectNumericType_UInt8; 
			obj->Type = StackObjectType_Int32; 
			break; 
		case Signature_ElementType_Char: 
		case Signature_ElementType_U2: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U2"); 
			obj->NumericType = StackObjectNumericType_UInt16; 
			obj->Type = StackObjectType_Int32; 
			break; 
		case Signature_ElementType_U4: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U4"); 
			obj->NumericType = StackObjectNumericType_UInt32; 
			obj->Type = StackObjectType_Int32; 
			break; 
		case Signature_ElementType_U8: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U8"); 
			obj->NumericType = StackObjectNumericType_UInt64; 
			obj->Type = StackObjectType_Int64; 
			break; 
		case Signature_ElementType_R4: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type R4"); 
			obj->NumericType = StackObjectNumericType_Float32; 
			obj->Type = StackObjectType_Float; 
			break; 
		case Signature_ElementType_R8: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type R8"); 
			obj->NumericType = StackObjectNumericType_Float64; 
			obj->Type = StackObjectType_Float; 
			break; 
		case Signature_ElementType_Pointer: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type Pointer"); 
			obj->NumericType = StackObjectNumericType_Pointer; 
			obj->Type = StackObjectType_NativeInt; 
			break; 
		case Signature_ElementType_IPointer: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type IPointer"); 
			obj->NumericType = StackObjectNumericType_Pointer; 
			obj->Type = StackObjectType_NativeInt; 
			break; 
		case Signature_ElementType_UPointer: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type UPointer"); 
			obj->NumericType = StackObjectNumericType_UPointer; 
			obj->Type = StackObjectType_NativeInt; 
			break; 
		case Signature_ElementType_ValueType:
		{
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type ValueType"); 
			MetaDataToken* tok = CLIFile_ResolveTypeDefOrRefOrSpecToken(fil, TypeSig->ValueTypeDefOrRefOrSpecToken); 
			switch(tok->Table) 
			{ 
				case MetaData_Table_TypeDefinition: 
				{
					TypeDefinition* tdef = (TypeDefinition*)tok->Data; 
					if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeDefinition) 
					{ 
						if (tdef->Extends.TypeDefinition == dom->CachedType___System_Enum) 
						{ 
							for (uint32_t i = 0; i < tdef->FieldListCount; i++) 
							{ 
								Field* fld = &(tdef->FieldList[i]); 
								if (!strcmp(fld->Name, "value__")) 
								{ 
									FieldSignature* sig2 = FieldSignature_Expand(fld->Signature, fil); 
									switch (sig2->Type->ElementType) 
									{ 
										case Signature_ElementType_I1: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I1"); 
											obj->NumericType = StackObjectNumericType_Int8; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_I2: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I2"); 
											obj->NumericType = StackObjectNumericType_Int16; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_I4: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I4"); 
											obj->NumericType = StackObjectNumericType_Int32; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_I8: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I8"); 
											obj->NumericType = StackObjectNumericType_Int64; 
											obj->Type = StackObjectType_Int64; 
											break; 
										case Signature_ElementType_U1: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U1"); 
											obj->NumericType = StackObjectNumericType_UInt8; 
											obj->Type = StackObjectType_Int32; 
											break;
										case Signature_ElementType_U2: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U2"); 
											obj->NumericType = StackObjectNumericType_UInt16; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_U4: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U4"); 
											obj->NumericType = StackObjectNumericType_UInt32; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_U8: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U8"); 
											obj->NumericType = StackObjectNumericType_UInt64; 
											obj->Type = StackObjectType_Int64; 
											break; 
										default:
											Panic("Invalid value type for Enum!");
									}
									FieldSignature_Destroy(sig2); 
									break; 
								} 
							} 
						} 
						else 
						{ 
							obj->NumericType = StackObjectNumericType_DataType; 
							obj->Type = StackObjectType_DataType; 
						} 
					} 
					else if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeReference) 
					{ 
						if (tdef->Extends.TypeReference->ResolvedType == dom->CachedType___System_Enum) 
						{ 
							for (uint32_t i = 0; i < tdef->FieldListCount; i++) 
							{ 
								Field* fld = &(tdef->FieldList[i]); 
								if (!strcmp(fld->Name, "value__")) 
								{ 
									FieldSignature* sig2 = FieldSignature_Expand(fld->Signature, tdef->Extends.TypeReference->ResolvedType->File); 
									switch (sig2->Type->ElementType) 
									{ 
										case Signature_ElementType_I1: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I1"); 
											obj->NumericType = StackObjectNumericType_Int8; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_I2: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I2"); 
											obj->NumericType = StackObjectNumericType_Int16; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_I4: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I4"); 
											obj->NumericType = StackObjectNumericType_Int32; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_I8: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type I8"); 
											obj->NumericType = StackObjectNumericType_Int64; 
											obj->Type = StackObjectType_Int64; 
											break; 
										case Signature_ElementType_U1: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U1"); 
											obj->NumericType = StackObjectNumericType_UInt8; 
											obj->Type = StackObjectType_Int32; 
											break;
										case Signature_ElementType_U2: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U2"); 
											obj->NumericType = StackObjectNumericType_UInt16; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_U4: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U4"); 
											obj->NumericType = StackObjectNumericType_UInt32; 
											obj->Type = StackObjectType_Int32; 
											break; 
										case Signature_ElementType_U8: 
											Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type U8"); 
											obj->NumericType = StackObjectNumericType_UInt64; 
											obj->Type = StackObjectType_Int64; 
											break; 
										default:
											Panic("Invalid value type for Enum!");
									}
									FieldSignature_Destroy(sig2); 
									break; 
								} 
							} 
						} 
						else 
						{ 
							obj->NumericType = StackObjectNumericType_DataType; 
							obj->Type = StackObjectType_DataType; 
						} 
					} 
					else 
					{ 
						Panic("Unknown Table For Something-or-Another!"); 
					} 
					break; 
				}
					
				default: 
					Panic(String_Format("Unknown Table Again %i!", (int)tok->Table)); 
					break; 
					
			} 
			free(tok); 
			break; 
		}
		case Signature_ElementType_Object: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type Object"); 
		case Signature_ElementType_Array: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type Array"); 
		case Signature_ElementType_SingleDimensionArray: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type SingleDimArray"); 
		case Signature_ElementType_String: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type String"); 
		case Signature_ElementType_ByReference: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type ReferenceType"); 
		case Signature_ElementType_Class: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type Class"); 
			obj->NumericType = StackObjectNumericType_Ref; 
			obj->Type = StackObjectType_ReferenceType; 
			break; 
		case Signature_ElementType_TypedByReference: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type TypedByReference"); 
			obj->NumericType = StackObjectNumericType_ManagedPointer; 
			obj->Type = StackObjectType_ManagedPointer; 
			break; 
		case Signature_ElementType_Var: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type Var"); 
			obj->NumericType = StackObjectNumericType_Generic; 
			obj->Type = StackObjectType_Generic;
			break; 
		case Signature_ElementType_MethodVar: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type MethodVar"); 
			obj->NumericType = StackObjectNumericType_MethodGeneric; 
			obj->Type = StackObjectType_MethodGeneric; 
			break; 
		case Signature_ElementType_GenericInstantiation: 
			Log_WriteLine(LogFlags_ILReading_ElementTypes, "Element Type GenericInstantation"); 
			obj->NumericType = StackObjectNumericType_UPointer; 
			obj->Type = StackObjectType_NativeInt; 
			break; 
		default: 
			Panic(String_Format("Unknown Element Type '0x%x' at 0x%x!", (unsigned int)(TypeSig->ElementType), (unsigned int)&(TypeSig->ElementType))); 
			break; 
	}
}


ALWAYS_INLINE void SetObjectTypeFromElementType(StackObject* obj, ElementType elemType)
{ 
	switch(elemType) 
	{ 
		case ElementType_I1: 
		case ElementType_I2: 
		case ElementType_I4: 
		case ElementType_U1: 
		case ElementType_U2: 
		case ElementType_U4: 
			obj->Type = StackObjectType_Int32; 
			break; 
		case ElementType_I8: 
		case ElementType_U8: 
			obj->Type = StackObjectType_Int64; 
			break; 
		case ElementType_I: 
		case ElementType_U:
			obj->Type = StackObjectType_NativeInt; 
			break;
		case ElementType_R4: 
		case ElementType_R8:
			obj->Type = StackObjectType_Float; 
			break; 
		case ElementType_Ref: 
			obj->Type = StackObjectType_ReferenceType; 
			break; 
		case ElementType_DataType: 
			obj->Type = StackObjectType_DataType; 
			break; 
		default: 
			Panic("Unknown ElementType!"); 
			break; 
	} 
	switch(elemType) 
	{ 
		case ElementType_I1: 
			obj->NumericType = StackObjectNumericType_Int8; 
			break; 
		case ElementType_I2: 
			obj->NumericType = StackObjectNumericType_Int16; 
			break; 
		case ElementType_I4: 
			obj->NumericType = StackObjectNumericType_Int32; 
			break; 
		case ElementType_U1: 
			obj->NumericType = StackObjectNumericType_UInt8; 
			break; 
		case ElementType_U2: 
			obj->NumericType = StackObjectNumericType_UInt16; 
			break; 
		case ElementType_U4: 
			obj->NumericType = StackObjectNumericType_UInt32; 
			break; 
		case ElementType_I8: 
			obj->NumericType = StackObjectNumericType_Int64; 
			break; 
		case ElementType_U8: 
			obj->NumericType = StackObjectNumericType_UInt64; 
			break; 
		case ElementType_I: 
			obj->NumericType = StackObjectNumericType_Pointer; 
			break; 
		case ElementType_U: 
			obj->NumericType = StackObjectNumericType_UPointer; 
			break; 
		case ElementType_R4: 
			obj->NumericType = StackObjectNumericType_Float32; 
			break; 
		case ElementType_R8: 
			obj->NumericType = StackObjectNumericType_Float64; 
			break; 
		case ElementType_Ref: 
			obj->NumericType = StackObjectNumericType_Ref; 
			break; 
		case ElementType_DataType: 
			obj->NumericType = StackObjectNumericType_DataType; 
			break; 
		default: 
			Panic("Unknown ElementType!"); 
			break; 
	} 
}


ALWAYS_INLINE void GetElementTypeOfStackObject(ElementType* dest, StackObject* stkObj) 
{
	switch (stkObj->NumericType) 
	{ 
		case StackObjectNumericType_UInt8: 
			*dest = ElementType_U1; 
			break; 
		case StackObjectNumericType_UInt16: 
			*dest = ElementType_U2; 
			break; 
		case StackObjectNumericType_UInt32: 
			*dest = ElementType_U4; 
			break; 
		case StackObjectNumericType_UInt64: 
			*dest = ElementType_U8; 
			break; 
		case StackObjectNumericType_Int8: 
			*dest = ElementType_I1; 
			break; 
		case StackObjectNumericType_Int16: 
			*dest = ElementType_I2; 
			break; 
		case StackObjectNumericType_Int32: 
			*dest = ElementType_I4; 
			break; 
		case StackObjectNumericType_Int64: 
			*dest = ElementType_I8; 
			break; 
		case StackObjectNumericType_Float32: 
			*dest = ElementType_R4; 
			break; 
		case StackObjectNumericType_Float64: 
			*dest = ElementType_R8; 
			break; 
		case StackObjectNumericType_Pointer: 
			*dest = ElementType_I; 
			break; 
		case StackObjectNumericType_UPointer: 
			*dest = ElementType_U; 
			break; 
		case StackObjectNumericType_DataType: 
			*dest = ElementType_DataType; 
			break; 
		case StackObjectNumericType_Ref: 
			*dest = ElementType_Ref; 
			break; 
		case StackObjectNumericType_ManagedPointer: 
			*dest = ElementType_ManagedPointer; 
			break; 
		case StackObjectNumericType_Generic: 
			*dest = ElementType_Generic; 
			break; 
		case StackObjectNumericType_MethodGeneric: 
			*dest = ElementType_MethodGeneric; 
			break; 
		default: 
			Panic(String_Format("Unknown StackObjectNumericType %i!", (int)stkObj->NumericType)); 
			break; 
	} 
}


ALWAYS_INLINE void CheckBinaryNumericOperandTypesAndSetResult(ElementType* OperandA, ElementType* OperandB, int BinaryNumericOp, StackObject* ResultObject) 
{
	Log_WriteLine(LogFlags_ILReading_ElementTypes, "Operand A: 0x%x", (unsigned int)*OperandA); 
	Log_WriteLine(LogFlags_ILReading_ElementTypes, "Operand B: 0x%x", (unsigned int)*OperandB);
	//printf("Operand A: 0x%x\n", (unsigned int)*OperandA); 
	//printf("Operand B: 0x%x\n", (unsigned int)*OperandB);
	switch(*OperandA) 
	{ 
		case ElementType_U1: 
		case ElementType_U2: 
		case ElementType_U4: 
		case ElementType_I1: 
		case ElementType_I2: 
		case ElementType_I4: 
		{ 
			if (BinaryNumericOp == BinaryNumericOp_Add) 
			{ 
				if (*OperandB == ElementType_ManagedPointer) 
				{ 
					ResultObject->Type = StackObjectType_ManagedPointer; 
					ResultObject->NumericType = StackObjectNumericType_ManagedPointer; 
					break; 
				} 
			} 
			switch(*OperandB) 
			{ 
				case ElementType_I: 
				case ElementType_U: 
					ResultObject->Type = StackObjectType_NativeInt; 
					ResultObject->NumericType = StackObjectNumericType_Pointer; 
					break; 
				case ElementType_U1: 
				case ElementType_U2: 
				case ElementType_U4: 
				case ElementType_I1: 
				case ElementType_I2: 
				case ElementType_I4: 
					ResultObject->Type = StackObjectType_Int32; 
					ResultObject->NumericType = StackObjectNumericType_Int32; 
					break; 
				default: 
					Panic("Invalid Operands for Binary Numeric Operation!"); 
					break; 
			} 
			break; 
		} 
		
		case ElementType_I8: 
		case ElementType_U8: 
		{ 
			switch(*OperandB) 
			{ 
				case ElementType_I8: 
				case ElementType_U8: 
					ResultObject->Type = StackObjectType_Int64; 
					ResultObject->NumericType = StackObjectNumericType_Int64; 
					break; 
				default: 
					Panic("Invalid Operands for Binary Numeric Operation!"); 
					break; 
			} 
			break; 
		} 
		
		case ElementType_R4: 
		case ElementType_R8: 
		{ 
			switch(*OperandB) 
			{ 
				case ElementType_R4: 
				case ElementType_R8: 
					ResultObject->Type = StackObjectType_Float; 
					ResultObject->NumericType = StackObjectNumericType_Float64; 
					break; 
				default: 
					Panic("Invalid Operands for Binary Numeric Operation!"); 
					break; 
			} 
			break; 
		} 
		
		case ElementType_I: 
		case ElementType_U: 
		{ 
			if (BinaryNumericOp == BinaryNumericOp_Add) 
			{ 
				if (*OperandB == ElementType_ManagedPointer) 
				{ 
					ResultObject->Type = StackObjectType_ManagedPointer; 
					ResultObject->NumericType = StackObjectNumericType_ManagedPointer; 
					break; 
				} 
			} 
			switch(*OperandB) 
			{ 
				case ElementType_I: 
				case ElementType_U: 
				case ElementType_U1: 
				case ElementType_U2: 
				case ElementType_U4: 
				case ElementType_I1: 
				case ElementType_I2: 
				case ElementType_I4: 
					ResultObject->Type = StackObjectType_NativeInt; 
					ResultObject->NumericType = StackObjectNumericType_Pointer; 
					break; 
				default: 
					Panic("Invalid Operands for Binary Numeric Operation!"); 
					break; 
			} 
			break; 
		} 
		
		case ElementType_ManagedPointer: 
		{ 
			if (BinaryNumericOp == BinaryNumericOp_Add || BinaryNumericOp == BinaryNumericOp_Sub) 
			{ 
				if (BinaryNumericOp == BinaryNumericOp_Sub) 
				{ 
					if (*OperandB == ElementType_ManagedPointer) 
					{ 
						ResultObject->Type = StackObjectType_NativeInt; 
						ResultObject->NumericType = StackObjectNumericType_Pointer; 
						break; 
					} 
				} 
				switch(*OperandB) 
				{ 
					case ElementType_I: 
					case ElementType_U: 
					case ElementType_U1: 
					case ElementType_U2: 
					case ElementType_U4: 
					case ElementType_I1: 
					case ElementType_I2: 
					case ElementType_I4: 
						ResultObject->Type = StackObjectType_ManagedPointer; 
						ResultObject->NumericType = StackObjectNumericType_ManagedPointer; 
						break; 
					default: 
						Panic("Invalid Operands for Binary Numeric Operation!"); 
						break; 
				} 
			} 
			else 
			{ 
				Panic("Invalid Operands for Binary Numeric Operation!"); 
			} 
			break; 
		} 
		
		default: 
			Panic("Invalid Operands for Addition!"); 
			break; 
	}
}


ALWAYS_INLINE bool_t IsStruct(TypeDefinition* tdef, AppDomain* dom)
{
	if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeDefinition) 
	{ 
		if (tdef->Extends.TypeDefinition == dom->CachedType___System_Enum) 
		{ 
			return TRUE;
		}
		else if (tdef->Extends.TypeDefinition == dom->CachedType___System_ValueType)
		{
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else if (tdef->TypeOfExtends == TypeDefOrRef_Type_TypeReference) 
	{ 
		if (tdef->Extends.TypeReference->ResolvedType == dom->CachedType___System_Enum) 
		{ 
			return TRUE;
		}
		else if (tdef->Extends.TypeReference->ResolvedType == dom->CachedType___System_ValueType)
		{
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		Log_WriteLine(LogFlags_ILReading, "Unknown type of extends when checking if a type is a struct.");
		return FALSE;
	}
}


ALWAYS_INLINE void EmptyStack(SyntheticStack* stack)
{
	while (stack->StackDepth > 0)
	{
		StackObjectPool_Release(SyntheticStack_Pop(stack));
	}
}

IRType* GetIRTypeOfSignatureType(AppDomain* dom, CLIFile* fil, IRAssembly* asmbly, SignatureType* tp)
{
	switch(tp->ElementType)
	{
		case Signature_ElementType_I1:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_SByte->TableIndex - 1];
		case Signature_ElementType_U1:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Byte->TableIndex - 1];
		case Signature_ElementType_I2:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Int16->TableIndex - 1];
		case Signature_ElementType_U2:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_UInt16->TableIndex - 1];
		case Signature_ElementType_I4:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Int32->TableIndex - 1];
		case Signature_ElementType_U4:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_UInt32->TableIndex - 1];
		case Signature_ElementType_I8:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Int64->TableIndex - 1];
		case Signature_ElementType_U8:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_UInt64->TableIndex - 1];
		case Signature_ElementType_R4:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Single->TableIndex - 1];
		case Signature_ElementType_R8:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Double->TableIndex - 1];
		case Signature_ElementType_Boolean:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Boolean->TableIndex - 1];
		case Signature_ElementType_Char:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Char->TableIndex - 1];
		case Signature_ElementType_Pointer:
		case Signature_ElementType_IPointer:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_IntPtr->TableIndex - 1];
		case Signature_ElementType_UPointer:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_UIntPtr->TableIndex - 1];
		case Signature_ElementType_Object:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Object->TableIndex - 1];
		case Signature_ElementType_String:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_String->TableIndex - 1];
		case Signature_ElementType_Type:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Type->TableIndex - 1];
		case Signature_ElementType_Void:
			return dom->IRAssemblies[0]->Types[dom->CachedType___System_Void->TableIndex - 1];
		case Signature_ElementType_ValueType:
		case Signature_ElementType_Class:
		{
			MetaDataToken* tok = CLIFile_ResolveTypeDefOrRefOrSpecToken(fil, tp->ClassTypeDefOrRefOrSpecToken);
			IRType* retVal = NULL;
			switch(tok->Table)
			{
				case MetaData_Table_TypeDefinition:
					retVal = asmbly->Types[((TypeDefinition*)tok->Data)->TableIndex - 1];
					break;

				case MetaData_Table_TypeReference:
					retVal = ((TypeReference*)tok->Data)->ResolvedType->File->Assembly->Types[((TypeReference*)tok->Data)->ResolvedType->TableIndex - 1];
					break;

				default:
					Panic(String_Format("Unknown table (0x%x)!", (unsigned int)tok->Table));
					break;
			}
			free(tok);
			return retVal;
		}
		case Signature_ElementType_SingleDimensionArray:
		{
			IRType* sysArrayType = dom->IRAssemblies[0]->Types[dom->CachedType___System_Array->TableIndex - 1];
			IRType* elementType = GetIRTypeOfSignatureType(dom, fil, asmbly, tp->SZArrayType);
			IRType* arrayType = NULL;
			IRArrayType* lookupType = NULL;
			HASH_FIND(HashHandle, asmbly->ArrayTypesHashTable, (void*)&elementType, sizeof(void*), lookupType);
			if (!lookupType)
			{
				arrayType = IRType_Create();
				*arrayType = *sysArrayType; // Shallow copy everything first
				arrayType->ArrayType = IRArrayType_Create(elementType, arrayType);
				HASH_ADD(HashHandle, asmbly->ArrayTypesHashTable, ArrayElementType, sizeof(void*), arrayType->ArrayType);
			}
			else arrayType = lookupType->ArrayType;
			return arrayType;
		}

		case Signature_ElementType_MethodVar:
			printf("WARNING: Generics  aren't supported yet!\n");
			return NULL;
		case Signature_ElementType_Var:
			printf("WARNING: Generics aren't supported yet!\n");
			return NULL;
		case Signature_ElementType_GenericInstantiation:
			printf("WARNING: Generics aren't supported yet!\n");
			return NULL;

		default:
			Panic(String_Format("Unknown Signature Element Type (0x%x) from SignatureType @ 0x%x!", (unsigned int)tp->ElementType, (unsigned int)tp));
			break;
	}
}
