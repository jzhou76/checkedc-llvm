//===-- CheckedCAddLockToMultiple.h - Create a lock for _multiple object----==//
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

#ifndef LLVM_IR_CHECKEDCADDLOCKTOMULTIPLE_H
#define LLVM_IR_CHECKEDCADDLOCKTOMULTIPLE_H

#include "llvm/IR/PassManager.h"

namespace llvm {
namespace CheckedCAddLockToMultiple {

struct CheckedCAddLockToMultiplePass : ModulePass {
  static char ID;

  CheckedCAddLockToMultiplePass();

  bool runOnModule(Module &M) override;
};

} // End of namespace CheckedCAddLockToMultiple

ModulePass *createCheckedCAddLockToMultiplePass();

} // End of namespace llvm

#endif  // End of LLVM_IR_CHECKEDCADDLOCKTOMULTIPLE_H
