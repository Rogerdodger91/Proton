#pragma once

#include <CLR/IRStructures.h>
#include <CLR/JIT/BranchRegistry.h>

void JIT_CompileMethod(IRMethod* mthd);

void JIT_Layout_Parameters(IRMethod* pMethod);
void JIT_Layout_LocalVariables(IRMethod* pMethod);
void JIT_Layout_Fields(IRType* pType);
char* JIT_Emit_Prologue(IRMethod* mth, char* compMethod);
char* JIT_Emit_Epilogue(IRMethod* mth, char* compMethod);
char* JIT_LinkBranches(char* compMethod, BranchRegistry* branchRegistry, uint32_t pLength);


char* JIT_Compile_Nop						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_BreakForDebugger			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Return					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadInt32_Val				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadInt64_Val				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadFloat32_Val			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadFloat64_Val			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Branch					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Optimized_Jump			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_LocalVar			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_LocalVar				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_LocalVar_Address		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Convert_OverflowCheck		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Convert_Unchecked			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Parameter			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Parameter_Address	(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_Parameter			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Element				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Element_Evil			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Element_Address		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_Element				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_Element_Evil		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Array_Length			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Pop						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Shift						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Add						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Sub						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Mul						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Div						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Rem						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadIndirect				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_StoreIndirect				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadNull					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_NewObject					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_NewArray					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Dup						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_And						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Or						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_XOr						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Neg						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Not						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_String				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Field				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Field_Address		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_Field				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Static_Field			(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Static_Field_Address	(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_Static_Field		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Load_Object				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Store_Object				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Copy_Object				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Switch					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_CastClass					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_IsInst					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Unbox						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Unbox_Any					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Box						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Compare					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_CheckFinite				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LocalAllocate				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_InitObject				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_SizeOf					(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadFunction				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_LoadVirtualFunction		(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);

char* JIT_Compile_Call						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Call_Absolute				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Call_Internal				(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);
char* JIT_Compile_Jump						(IRInstruction* instr, char* compMethod, IRMethod* mth, BranchRegistry* branchRegistry);

