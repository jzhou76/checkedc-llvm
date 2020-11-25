//===- CheckedCSplitBB.cpp - Split Basic Blocks by Function Calls ---------===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
//
// This file implementes the CheckedCSplitBB pass. It splits each basic block
// by function call(s) that may free any mmsafe pointers used in the function.
// The result is that every new basic block either has no function calls except
// for the ones that will not freeing mmsafe pointers or has only one call
// instruction that may free mmsafe pointers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/CheckedCSplitBB.h"
#include "llvm/Analysis/CheckedCFreeFinder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

char CheckedCSplitBBPass::ID = 0;

CheckedCSplitBBPass::CheckedCSplitBBPass() : ModulePass(ID) {
  initializeCheckedCSplitBBPassPass(*PassRegistry::getPassRegistry());
}

StringRef CheckedCSplitBBPass::getPassName() const {
  return "CheckedCSplitBB";
}

void CheckedCSplitBBPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CheckedCFreeFinderPass>();
  AU.addPreserved<CheckedCFreeFinderPass>();
}

//
// Entrance of this pass.
//
bool CheckedCSplitBBPass::runOnModule(Module &M) {
  bool changed = false;
  StringRef MName = M.getName();
  errs() << "[CheckedCSplitBBPass]: processing " << MName << "\n";

  return changed;
}

// Create a new instance of this pass.
ModulePass *llvm::createCheckedCSplitBBPass(void) {
  return new CheckedCSplitBBPass();
}

// Initialization.
INITIALIZE_PASS_BEGIN(CheckedCSplitBBPass, "checkedc-split-bb-pass",
                      "Checked C Split BB pass", true, false);
INITIALIZE_PASS_DEPENDENCY(CheckedCFreeFinderPass);
INITIALIZE_PASS_END(CheckedCSplitBBPass, "checkedc-split-bb-pass",
                    "Checked C Split BB pass", true, false);
