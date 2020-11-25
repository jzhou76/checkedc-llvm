//===- CheckedCFreeFinder.cpp - Find Functions that may Free Heap Object---===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
//
// This file implments the pass of finding all functions that may directly or
// indirectly (by its call-chain descendents) free memory object(s) pointed
// by mmsafe pointers. It uses llvm's CallGraph analysis results to query
// the call relations of the functions in the current module.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CheckedCFreeFinder.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

char CheckedCFreeFinderPass::ID = 0;

CheckedCFreeFinderPass::CheckedCFreeFinderPass() : ModulePass(ID) {
  initializeCheckedCFreeFinderPassPass(*PassRegistry::getPassRegistry());
}

StringRef CheckedCFreeFinderPass::getPassName() const {
  return "CheckedCFreeFinder";
}

void CheckedCFreeFinderPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CallGraphWrapperPass>();
  AU.setPreservesAll();
}

//
// Entrance of this pass.
//
bool CheckedCFreeFinderPass::runOnModule(Module &M) {
  return false;
}

ModulePass *llvm::createCheckedCFreeFinderPass(void) {
  return new CheckedCFreeFinderPass();
}

// Initialization
INITIALIZE_PASS_BEGIN(CheckedCFreeFinderPass, "checkedc-free-finder-pass",
                      "Checked C Free Finder pass", false, true);
INITIALIZE_PASS_END(CheckedCFreeFinderPass, "checkedc-free-finder-pass",
                    "Checked C Free Finder pass", false, true);
