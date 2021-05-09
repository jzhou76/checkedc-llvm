//===-- CheckedCAddLockToMultiple.cpp - Create a lock for _multiple object--==//
//
//          Temporal Memory Safety for Checked C
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This pass adds a lock to each _multiple stack and global object.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/CheckedCAddLockToMultiple.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <vector>

using namespace llvm;
using namespace CheckedCAddLockToMultiple;

CheckedCAddLockToMultiplePass::CheckedCAddLockToMultiplePass() : ModulePass(ID) {
  initializeCheckedCAddLockToMultiplePassPass(*PassRegistry::getPassRegistry());
}

//
// This function replaces each _multiple stack variable with a struct that
// contains a lock and the orignal stack variable. The lock for all stack
// variables is 1. It also replaces all the uses of the original Alloca with
// a GEP to the stack variable in the struct.
//
static bool AllocateLockForMultipleStackVars(Module &M) {
  std::vector<AllocaInst *> MultipleStackVars;
  for (Function &Fn : M) {
    if (Fn.isDeclaration()) continue;
    // Collect all _multiple stack variables. We only need to iterate over
    // the front BB because all AllocaInst are in it.
    for (Instruction &I : Fn.front()) {
      if (AllocaInst *Alloca = dyn_cast<AllocaInst>(&I)) {
        if (Alloca->isMultipleQualified()) {
          MultipleStackVars.push_back(Alloca);
        }
      }
    }
  }

  // Process each _multiple AllocaInst.
  IRBuilder<> Builder(M.getContext());
  Type *Int64Ty = Builder.getInt64Ty();
  for (AllocaInst *Alloca : MultipleStackVars) {
    Builder.SetInsertPoint(Alloca);
    Type *AllocaTy = Alloca->getAllocatedType();
    Value *VarPtr = nullptr;   // Pointer to the target stack variable.
    ConstantInt *One = Builder.getInt64(1);
    if (AllocaTy->isMMSafePointerTy()) {
      // For MMSafe pointers, we need to guarantee that they are aligned
      // by 16 bytes. When an mmsafe pointer is declared in a struct in
      // the source code, Clang guarantees that it is 16 bytes aligned (mmptr)
      // or 32 bytes aligned (mmarrayptr) because we updated related src code
      // (include/clang/Basic/TargetInfo.h) for it.  However, here when we
      // manually build a struct that contains a 64-bit integer and an mmsafe
      // pointer, no padding is inserted between the integer and the pointer
      // to guarantee the alignment of the mmsafe ptr field, and it may cause
      // segfault when a movaps instruction is used to load/store the mmsafe ptr
      // field. I did not find an API to tweak the alignment of individual
      // struct fields. Here we insert another 64-bit integer in the
      // new struct and set the alignment of the whole struct to be 16 bytes.
      // Jie Zhou: This solution is not elegant in my opinion, although the
      // generated binary code should be the same even if there is an API
      // function that pads a struct for alignment.
      StructType *ST = StructType::get(Int64Ty, Int64Ty, AllocaTy);
      AllocaInst *NewAlloca = Builder.CreateAlloca(ST);
      NewAlloca->setAlignment(16);
      VarPtr = Builder.CreateStructGEP(NewAlloca, 2);
      Builder.CreateStore(One, Builder.CreateStructGEP(NewAlloca, 1));
    } else {
      StructType *ST = StructType::get(Int64Ty, AllocaTy);
      AllocaInst *NewAlloca = Builder.CreateAlloca(ST);
      VarPtr = Builder.CreateStructGEP(NewAlloca, 1);
      Builder.CreateStore(One, Builder.CreateStructGEP(NewAlloca, 0));
    }

    // Replace the uses of Alloca with the GEP to the new Alloca.
    BasicBlock::iterator BI(Alloca);
    ReplaceInstWithValue(Alloca->getParent()->getInstList(), BI, VarPtr);
  }

  return MultipleStackVars.empty();
}

//
// This function replaces each _multiple global (including static local)
// variabels with a struct that contains a lock and the original global var.
// The lock for all global variables is 2.
//
static bool AllocateLockForMultipleGlobals(Module &M) {
  // Collect _multiple global variables, including static local variables.
  std::vector<GlobalVariable *> MultipleGV;
  for (GlobalVariable &GV : M.globals()) {
    if (GV.isMultipleQualified()) {
      MultipleGV.push_back(&GV);
      if (GV.hasCommonLinkage()) {
        // If a global variable is defined without explicit initialization,
        // LLVM would set its linkage type to "common" and globals of
        // common linkage must be initialized with zero initializer.
        // Here we set it to ExternalLinkage because we would initialize
        // it with a lock of value 2.
        GV.setLinkage(GlobalValue::ExternalLinkage);
      }
    }
  }

  // Replace each global variable with a struct that contains a lock and
  // the original variable.
  for (GlobalVariable *GV : MultipleGV) {
    LLVMContext &llvmContext = M.getContext();
    Type *Int64Ty = Type::getInt64Ty(llvmContext);
    Type *Int32Ty = Type::getInt32Ty(llvmContext);

    Type *GVTy = GV->getType()->getElementType();
    Constant *GVInit = GV->hasInitializer() ? GV->getInitializer() : nullptr;
    // All global variables have a lock of value 2.
    Constant *Zero = ConstantInt::get(Int64Ty, 0);
    Constant *Two = ConstantInt::get(Int64Ty, 2);
    StructType *ST;
    // The indices to GEP the original global variable.
    Constant *Indices[] = {ConstantInt::get(Int32Ty, 0), nullptr};
    Constant *NewGVInit;
    if (GVTy->isMMSafePointerTy()) {
      // See the comment for the corresponding if condition for stack objects.
      ST = StructType::get(Int64Ty, Int64Ty, GVTy);
      Indices[1] = ConstantInt::get(Int32Ty, 2);
      NewGVInit = GVInit ? ConstantStruct::get(ST, {Zero, Two, GVInit}) : nullptr;
    } else {
      ST = StructType::get(Int64Ty, GVTy);
      Indices[1] = ConstantInt::get(Int32Ty, 1);
      NewGVInit = GVInit ? ConstantStruct::get(ST, {Two, GVInit}) : nullptr;
    }

    unsigned AS = GV->getType()->getPointerAddressSpace();
    // Create a struct that contains the lock and the original GV.
    GlobalVariable *GVWithLock =
      new GlobalVariable(M, ST, GV->isConstant(), GV->getLinkage(), NewGVInit,
          GV->getName() + "_multiple", nullptr, GlobalVariable::NotThreadLocal,
          AS, GV->isExternallyInitialized());
    GVWithLock->setAlignment(16);
    Constant *NewGVGEP = ConstantExpr::getGetElementPtr(ST, GVWithLock, Indices);
    GV->replaceAllUsesWith(NewGVGEP);
    GV->eraseFromParent();
  }

  return MultipleGV.empty();
}

//
// Entrance of this pass.
//
bool CheckedCAddLockToMultiplePass::runOnModule(Module &M) {
  bool hasMultipleStackVars = AllocateLockForMultipleStackVars(M);
  bool hasMultipleGlobals = AllocateLockForMultipleGlobals(M);
  return hasMultipleStackVars || hasMultipleGlobals;
}

char CheckedCAddLockToMultiplePass::ID = 0;

INITIALIZE_PASS(CheckedCAddLockToMultiplePass, "add_lock_to_multiple",
                "Add locks to _multiple objects", false, false);

ModulePass *llvm::createCheckedCAddLockToMultiplePass() {
  return new CheckedCAddLockToMultiplePass();
}
