//===-- HarmonizeType.cpp - Resolve MMSafePtr Type Mismatch-----------------==//
//
//          Temporal Memory Safety for Checked C
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This pass tries to resolve the type mismatch problem caused by Checked C's
// new safe pointers for temporal memory safety. Those new types of pointers
// are implemented as llvm::StructType, while by default pointers are
// implemented as llvm::PointerType.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/CheckedCHarmonizeType.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <vector>

using namespace llvm;
using namespace harmonizeType;

HarmonizeTypePass::HarmonizeTypePass() : FunctionPass(ID) {
  initializeHarmonizeTypePassPass(*PassRegistry::getPassRegistry());
}

/**
 * Main body of this pass.
 *
 * Before running this pass, when a _MM_Ptr or a _MM_array_ptr is dereferenced,
 * a malformed load would be generated. For example,
 *
 *   %p_Obj_Ptr = load %struct.Node*, { %struct.Node*, i64 }* %p
 *
 * Here we have a type mismatch, which may causes a later pass to fail.
 * For example, the EarlyCSE pass calls doRAUW() which finds the type mismatch
 * between struct.Node* and {struct.Node*, i64}.
 *
 * This pass will replace this kind of load with a GEP and a new load.
 * For the example above, it would be replace by
 *
 *   %Struct_Ptr = getelementptr { %struct.Node*, i64 }, { %struct.Node*, i64 }* %p, i32 0, i32 0
 *   %loadStructPtr = load %struct.Node*, %struct.Node** %Struct_Ptr
 *
 * */
bool HarmonizeTypePass::runOnFunction(Function &F) {
  bool change = false;

  std::vector<Instruction *> newGEPs, newLoads, toDelLoads;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      LoadInst *LI = dyn_cast<LoadInst>(&I);
      if (LI) {
        Type *ptrOpTy = LI->getPointerOperandType();
        Type *loadedType = LI->getType();
        Type *pointeeTy = dyn_cast<PointerType>(ptrOpTy)->getElementType();
        if (pointeeTy->isMMSafePointerTy() && !loadedType->isMMSafePointerTy()) {
          // This is a prblematic load.

          toDelLoads.push_back(LI);

          // Create a GEP instruction to replace the original load
          Value *zero = ConstantInt::get(Type::getInt32Ty(F.getContext()), 0);
          GetElementPtrInst *GEP =
            GetElementPtrInst::Create(pointeeTy,
                                      LI->getPointerOperand(),
                                      ArrayRef<Value *>({zero, zero}),
                                      "ObjRawPtr_Ptr");
          newGEPs.push_back(GEP);

          // Create a new load.
          newLoads.push_back(new LoadInst(loadedType, GEP, "ObjRawPtr"));

          change = true;
        }
      }
    }
  }

  // Replace old problematic loads with a new GEP and a new load.
  for (unsigned i = 0; i < toDelLoads.size(); i++) {
    newGEPs[i]->insertBefore(toDelLoads[i]);
    ReplaceInstWithInst(toDelLoads[i], newLoads[i]);
  }

  return change;
}

/**
 * Function:examineLoadInst()
 *
 * This function prints out certain information about a load instruction.
 * */
void HarmonizeTypePass::examineLoadInst(LoadInst &LI) const {
  Type *loadedType = LI.getType();
  Type *ptrOpTy = LI.getPointerOperandType();
  Type *pointeeTy = dyn_cast<PointerType>(ptrOpTy)->getElementType();

  errs() << "Load: "; LI.dump();
  errs() << "Loaded Value Type: "; loadedType->dump();
  errs() << "Pointer Operand: "; LI.getPointerOperand()->dump();
  errs() << "Pointer Operand Type: "; ptrOpTy->dump();
  errs() << "Pointee Type: "; pointeeTy->dump();
  errs() << "\n";
}

char HarmonizeTypePass::ID = 0;

INITIALIZE_PASS(HarmonizeTypePass, "harmonizetype",
                "MMSafePtr type mediator", false, false)

// Public interface to the HarmonizeType pass
FunctionPass *llvm::createHarmonizeTypePass() {
  return new HarmonizeTypePass();
}
