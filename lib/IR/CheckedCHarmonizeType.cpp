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
// new safe pointers for temporal memory safety. The new safe pointers
// are implemented as llvm::StructType, while by default pointers are
// implemented as llvm::PointerType; when generating an clang::CodeGen::Address,
// the compiler mutates the type of an MMSafeptr of the Value *Pointer to
// the type of raw C pointer. Because of this mutation, there would be
// are ill-formed load and store instructions generated. This pass fixes
// those problematic memory instructions.
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
 * Before running this pass, when an MMSafePtr is dereferenced or
 * an MMArrayPtr has a pointer arithmetic, LLVM will generate a
 * clang::CodeGen::Address to wrap the pointer, and in our current
 * implemenation we let the compiler mutate the type of the MMSafePtr
 * to the type of the raw C pointer in the MMSafePtr, i.e.,
 * from an llvm::StructTytpe to an llvm::PointerType. To be more specific,
 * every time a clang::CodeGen::Address is created for an MMSafePtr,
 * the type of its llvm::Value *Pointer field will be mutated.
 * This would cause the generation of ill-formed load and store instructions.
 *
 * For example,
 *
 *   %p_Obj_Ptr = load %struct.Node*, { %struct.Node*, i64 }* %p
 *
 * Here we have a type mismatch, which may causes a later pass to fail.
 * For example, the EarlyCSE pass calls doRAUW() which catches the type mismatch
 * between struct.Node* and {struct.Node*, i64}.
 * This pass will replace this kind of load with a GEP and a new load.
 * For the example above, it would be replace by
 *
 *   %Struct_Ptr = getelementptr { %struct.Node*, i64 }, { %struct.Node*, i64 }* %p, i32 0, i32 0
 *   %loadStructPtr = load %struct.Node*, %struct.Node** %Struct_Ptr
 *
 * Further more, an ill-formed load will also contaminate the ExtractValue
 * and InsertValue instructions created for MMArrayPtr ++/-- operators.
 * For example, for *p++/--, the following code would be created:
 *
 *   %1 = load i32*, { i32*, i64, i64* }* %p, align 32
 *   %_innerPtr = extractvalue i32* %1, 0
 *   %incdec.ptr = getelementptr inbounds i32, i32* %_innerPtr, i32 -1
 *   %2 = insertvalue i32* %1, i32* %incdec.ptr, 0
 *
 * To fix this, we let the compiler to load the whole MMArrayPtr and replace
 * the polluted one in ExtractValue and InsertValue. For the example above,
 * the fixed code looks like:
 *
 *   %MMArrayPtr = load { i32*, i64, i64* }, { i32*, i64, i64* }* %p
 *   %_innerPtr = extractvalue { i32*, i64, i64* } %MMArrayPtr, 0
 *   %incdec.ptr = getelementptr inbounds i32, i32* %_innerPtr, i32 -1
 *   %1 = insertvalue { i32*, i64, i64* } %MMArrayPtr, i32* %incdec.ptr, 0
 *
 * Note that although sematically "*p++/--" is equal to "*p; p++",
 * LLVM generates different IR code for the two situations. For the latter,
 * the ++/-- operator does not use the type-crippled MMArrayPtr but uses
 * a complete MMArrayPtr loaded earlier.
 *
 * */
bool HarmonizeTypePass::runOnFunction(Function &F) {
  bool change = false;

  std::vector<Instruction *> newGEPs, newLoads, toDelLoads, MMArrayLoads;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      LoadInst *LI = dyn_cast<LoadInst>(&I);
      if (LI) {
        Type *ptrOpTy = LI->getPointerOperandType();
        Type *loadedType = LI->getType();
        Type *pointeeTy = dyn_cast<PointerType>(ptrOpTy)->getElementType();
        if (pointeeTy->isMMSafePointerTy() &&
            !loadedType->isMMSafePointerTy()) {
          // This is a problematic load.
          // Put this load to the to-delete list.
          toDelLoads.push_back(LI);

          // Create a GEP instruction to replace the original load
          Value *zero = ConstantInt::get(Type::getInt32Ty(F.getContext()), 0);
          GetElementPtrInst *GEP =
            GetElementPtrInst::Create(pointeeTy,
                                      LI->getPointerOperand(),
                                      ArrayRef<Value *>({zero, zero}),
                                      "ObjRawPtr_Ptr");
          // Create a new load to load the raw C pointer from the new GEP.
          newLoads.push_back(new LoadInst(loadedType, GEP, "ObjRawPtr"));

          // In case the ill-formed load results in ill-formed ExtractValue
          // and InsertValue instructions, create a new load to load
          // the whole MMArrayPtr.
          MMArrayLoads.push_back(new LoadInst(pointeeTy,
                                              LI->getPointerOperand(),
                                              "MMArrayPtr"));

          newGEPs.push_back(GEP);
          change = true;
        }
      }
    }
  }

  // Replace old ill-formed loads with a new GEP and a new load of the
  // inner raw pointer.  Also replace the uses of the ill-formed loads
  // with the new load that is a complete MMArrayPtr.
  for (unsigned i = 0; i < toDelLoads.size(); i++) {
    Instruction *brokenLI = toDelLoads[i];
    MMArrayLoads[i]->insertBefore(brokenLI);
    std::vector<Instruction *> InstToFix;
    for (User *U : brokenLI->users()) {
      if (Instruction *Inst = dyn_cast<Instruction>(U)) {
        if (isa<ExtractValueInst>(Inst) || isa<InsertValueInst>(Inst)) {
          // Collect all the ill-formed ExtractValueInst and InsertValueInst.
          InstToFix.push_back((Inst));
        }
      }
    }
    // Replace all the uses of ill-formed loads.
    for (Instruction *Inst : InstToFix) {
      Inst->replaceUsesOfWith(brokenLI, MMArrayLoads[i]);
    }

    // Insert a new load that loads the inner raw pointer and replace the
    // ill-formed load with it.
    newGEPs[i]->insertBefore(toDelLoads[i]);
    ReplaceInstWithInst(toDelLoads[i], newLoads[i]);
  }

  return change;
}

#if 1
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
#endif

char HarmonizeTypePass::ID = 0;

INITIALIZE_PASS(HarmonizeTypePass, "harmonizetype",
                "MMSafePtr type mediator", false, false)

// Public interface to the HarmonizeType pass
FunctionPass *llvm::createHarmonizeTypePass() {
  return new HarmonizeTypePass();
}
