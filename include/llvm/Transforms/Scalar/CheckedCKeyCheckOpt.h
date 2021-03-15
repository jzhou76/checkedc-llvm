//===- CheckedCKeyCheckOpt.h - MMSafePtr Redundant Key Check Removol ------===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the inferface for the pass of removing redundant key
/// checks on MMSafe pointers based on intra-procedural data-flow analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORM_SCALAR_CHECKEDCKEYCHECKOPT_H
#define LLVM_TRANSFORM_SCALAR_CHECKEDCKEYCHECKOPT_H

#include "llvm/IR/PassManager.h"

namespace llvm{

struct CheckedCKeyCheckOptPass : ModulePass {
  static char ID;

  CheckedCKeyCheckOptPass();

  StringRef getPassName() const override;

  bool runOnModule(Module &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  // Find or create the prototype of a key check function.
  Function *getKeyCheckFnPrototype(Module &M, bool isMMPtr=true);

  // Add key check(s) for all MMSafePtr argument(s) of a function call.
  void addKeyCheckForCalls(Module &M);

  void Opt(Module &M);
};

ModulePass *createCheckedCKeyCheckOptPass(void);

} // end of llvm namespace

#endif
