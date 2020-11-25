//===- CheckedCSplitBB.h - Split Basic Blocks by Function Calls -----------===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the inferface for the pass of spliting each basic block
/// by function call(s) that may free mmsafe pointers used in the function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORM_SCALAR_CHECKEDCSPLITBB_H
#define LLVM_TRANSFORM_SCALAR_CHECKEDCSPLITBB_H

#include "llvm/IR/PassManager.h"

namespace llvm{

struct CheckedCSplitBBPass : ModulePass {
  static char ID;

  CheckedCSplitBBPass();

  StringRef getPassName() const override;

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

ModulePass *createCheckedCSplitBBPass(void);

} // end of llvm namespace

#endif
