//===- CheckedCFreeFinder.h - Find Functions that may Free Heap Objects----===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the inferface for the pass of finding all function calls
/// that may directly or indirectly (by its call-chain descendents) free memory
/// object(s) pointed by mmsafe pointers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORM_SCALAR_CHECKEDCFREEFINDER_H
#define LLVM_TRANSFORM_SCALAR_CHECKEDCFREEFINDER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/CheckedCUtil.h"

namespace llvm{

struct CheckedCFreeFinderPass : ModulePass {
  static char ID;

  CheckedCFreeFinderPass();

  StringRef getPassName() const override;

  // A set of call instructions that may directly or indirectly free heap memory.
  InstSet_t MayFreeCalls;

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  // A set of functions that may directly or indirectly free heap objects.
  FnSet_t MayFreeFns;
  // Find which function calls which function(s) based on CallGraph.
  void FnReachAnalysis(Module &M, CallGraph &CG);

  // Find call instructions that may cause freeing heap objects.
  void FindMayFreeCalls(Module &M, CallGraph &CG);
};

ModulePass *createCheckedCFreeFinderPass(void);

} // end of namespace llvm

#endif

