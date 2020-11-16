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
// new safe pointers for temporal memory safety. The root casue is that
// the new types of pointers are implemented as llvm::StructType,
// while by default pointers are implemented as llvm::PointerType.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CHECKEDCHARMONIZETYPE_H
#define LLVM_IR_CHECKEDCHARMONIZETYPE_H

#include "llvm/IR/PassManager.h"
#include "Instructions.h"

namespace llvm {
namespace CheckedCHarmonizeType {

struct CheckedCHarmonizeTypePass : FunctionPass {
  static char ID;

  CheckedCHarmonizeTypePass();

  bool runOnFunction(Function &F) override;

  void examineLoadInst(LoadInst &LI) const;
};

} // End of namespace CheckedCHarmonizeType

FunctionPass *createCheckedCHarmonizeTypePass();

} // End of namespace llvm

#endif // LLVM_IR_CHECKEDCHARMONIZETYPE_H
