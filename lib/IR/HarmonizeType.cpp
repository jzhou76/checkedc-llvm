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

#include "llvm/IR/HarmonizeType.h"

using namespace llvm;
using namespace harmonizeType;

HarmonizeTypePass::HarmonizeTypePass() : FunctionPass(ID) {
  initializeHarmonizeTypePassPass(*PassRegistry::getPassRegistry());
}

// Main body of this pass.
bool HarmonizeTypePass::runOnFunction(Function &F) {
  errs() << "Running HarmonizeType pass on function " << F.getName() << "\n";

  return true;
}

char HarmonizeTypePass::ID = 0;

INITIALIZE_PASS(HarmonizeTypePass, "harmonizetype",
                "MMSafePtr type mediator", false, false)

// Public interface to the HarmonizeType pass
FunctionPass *llvm::createHarmonizeTypePass() {
  return new HarmonizeTypePass();
}
