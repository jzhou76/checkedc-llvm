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
 * A syntax that will cause ill-formed stores is "*++/--p" for MMArrayPtr.
 * Because the increment/decrement operator generates an Address for
 * an MMArrayPtr with mutated type, the following store instruction is broken.
 * To be more specific, code like the following snnipet would be created:
 *
 *   %2 = insertvalue { i32*, i64, i64* } %1, i32* %incdec.ptr, 0
 *   store i32* %2, { i32*, i64, i64* }* %p, align 32
 *
 * To fix it, first restore the type of the MMSafePtr; this will automatically
 * fix the store. Next extract the inner raw pointer from the MMSafePtr
 * and replace all the uses of the ill-typed MMArrayPtr in load instructions
 * with the extracted one. For the example, the fixed code would be like:
 *
 *   %2 = insertvalue { i32*, i64, i64* } %1, i32* %incdec.ptr, 0
 *   %_innerPtr1 = extractvalue { i32*, i64, i64* } %2, 0
 *   store { i32*, i64, i64* } %2, { i32*, i64, i64* }* %p, align 32
 *
 * And the corresponding load that uses the MMArrayPtr would be converted from
 * "%3 = load i32, i32* %2, align 4" to
 * "%3 = load i32, i32* %_innerPtr1, align 4".
 *
 * */
bool HarmonizeTypePass::runOnFunction(Function &F) {
  bool change = false;

  std::vector<LoadInst *> illFormedLoads;
  std::vector<Instruction *> GEPforLoad, newLoads;
  std::vector<StoreInst *> illFormedStores;

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (isa<LoadInst>(&I)) {
        LoadInst *LI = cast<LoadInst>(&I);
        Type *ptrOpTy = LI->getPointerOperandType();
        Type *loadedType = LI->getType();
        Type *pointeeTy = cast<PointerType>(ptrOpTy)->getElementType();
        if (pointeeTy->isMMSafePointerTy() &&
            !loadedType->isMMSafePointerTy()) {
          // This is an ill-formed load.
          // Put this load to the to-delete list.
          illFormedLoads.push_back(LI);

          // Create a GEP instruction to replace the original load
          Value *zero = ConstantInt::get(Type::getInt32Ty(F.getContext()), 0);
          GetElementPtrInst *GEP =
            GetElementPtrInst::Create(pointeeTy,
                                      LI->getPointerOperand(),
                                      ArrayRef<Value *>({zero, zero}),
                                      "ObjRawPtr_Ptr");
          // Create a new load to load the raw C pointer from the new GEP.
          newLoads.push_back(new LoadInst(loadedType, GEP, "ObjRawPtr"));

          GEPforLoad.push_back(GEP);

          change = true;
        }
      } else if (isa<StoreInst>(&I)) {
        StoreInst *SI = cast<StoreInst>(&I);
        Type *ptrOpTy = SI->getPointerOperandType();
        Type *valueTy = SI->getValueOperand()->getType();
        Type *pointeeTy = cast<PointerType>(ptrOpTy)->getElementType();
        if (pointeeTy->isMMArrayPointerTy() && !valueTy->isMMArrayPointerTy()) {
          // Thi is an ill-formed store.
          illFormedStores.push_back(SI);
        }
      }
    }
  }

  // Fix ill-formed loads and related instructions.
  for (unsigned i = 0; i < illFormedLoads.size(); i++) {
    LoadInst *illLoad = illFormedLoads[i];
    std::vector<Instruction *> InstToFix;
    for (User *U : illLoad->users()) {
      if (Instruction *Inst = dyn_cast<Instruction>(U)) {
        if (isa<ExtractValueInst>(Inst) || isa<InsertValueInst>(Inst)) {
          // Collect all the ill-formed ExtractValueInst and InsertValueInst
          // resulting from the ill-formed load.
          InstToFix.push_back((Inst));
        }
      }
    }
    if (!InstToFix.empty()) {
      // Create a new load to load the whole MMArrayPtr for the use in
      // the ill-formed ExtractValueInst and InsertValueInst.
      Type *pointeeTy =
        cast<PointerType>(illLoad->getPointerOperandType())->getElementType();
      LoadInst *MMArrayPtrLoad = new LoadInst(pointeeTy,
                                     illLoad->getPointerOperand(),
                                     "FullMMArrayPtr",
                                     illLoad);
      // Replace all the uses of the ill-formed load.
      for (Instruction *Inst : InstToFix) {
        Inst->replaceUsesOfWith(illLoad, MMArrayPtrLoad);
      }
    }

    // Insert a new load that loads the inner raw pointer and replaces the
    // ill-formed load with it. The ReplaceInstWithInst() will automatically
    // replaces all the uses of the ill-formed load with the new load.
    GEPforLoad[i]->insertBefore(illLoad);
    ReplaceInstWithInst(illLoad, newLoads[i]);
  }

  // Fix ill-formed stores and related instructions.
  for (unsigned i = 0; i < illFormedStores.size(); i++) {
    StoreInst *illStore = illFormedStores[i];
    Value *valueOp = illStore->getValueOperand();
    // We have only seen this kind of ill-formed stores from "*++/--p"
    // for MMArrayPtr.  The assert helps detect other cases.
    assert(isa<InsertValueInst>(valueOp) && "Unknown ill-formed StoreInst");

    // Restore the type of the MMSafePtr and get the inner raw pointer.
    valueOp->mutateType(
      cast<PointerType>(illStore->getPointerOperandType())->getElementType());
    Value *rawPtr =
      ExtractValueInst::Create(valueOp, 0, valueOp->getName() + "_innerPtr",
                               illStore);

    // Replace the uses of the ill-typed MMArrayPtr in load instructions with
    // the extracted inner raw pointer.
    std::vector<Instruction *> loadToFix;
    for (User *U : valueOp->users()) {
      if (LoadInst *load = dyn_cast<LoadInst>(U)) {
        loadToFix.push_back(load);
      }
    }
    for (Instruction *Inst : loadToFix) {
      Inst->replaceUsesOfWith(valueOp, rawPtr);
    }
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
