//===- CheckedCFreeFinder.h - Find Functions that may Free Heap Objects----===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the inferface for the pass of finding all functions that
/// may directly or indirectly (by its call-chain descendents) free memory
/// object(s) pointed by mmsafe pointers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORM_SCALAR_CHECKEDCFREEFINDER_H
#define LLVM_TRANSFORM_SCALAR_CHECKEDCFREEFINDER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/CheckedCUtil.h"

namespace llvm{

struct CheckedCFreeFinderPass : ModulePass {
  static char ID;

  CheckedCFreeFinderPass();

  StringRef getPassName() const override;

  // A set of functions that may directly or indirectly free heap objects.
  FnSet_t MayFreeFns;

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

ModulePass *createCheckedCFreeFinderPass(void);

} // end of namespace llvm

#endif

