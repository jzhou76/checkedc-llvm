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
// new safe pointers for temporal memory safety: those new types of pointers
// are implemented as llvm::StructType, while by default pointers are
// implemented as llvm::PointerType.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_HARMONIZETYPE_H
#define LLVM_IR_HARMONIZETYPE_H

#include "llvm/IR/PassManager.h"
#include "Instructions.h"

namespace llvm {
namespace harmonizeType {

struct HarmonizeTypePass : FunctionPass {
  static char ID;

  HarmonizeTypePass();

  bool runOnFunction(Function &F) override;

  void examineLoadInst(LoadInst &LI) const;
};

} // End of namespace harmonizeType

FunctionPass *createHarmonizeTypePass();

} // End of namespace llvm

#endif // LLVM_IR_HARMONIZETYPE_H
