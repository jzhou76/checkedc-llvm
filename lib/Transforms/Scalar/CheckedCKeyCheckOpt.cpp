//===- CheckedCKeyCheckOpt.cpp - MMSafePtr Redundant Key Check Removol ----===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//
//
// This file implements the CheckedCKeyCheckOpt pass. It does conservative
// intra-procedural data-flow analysis to remove redundant key checks on
// MMSafe pointers.  It is conservative in that for any function call that
// it is not sure if the callee would free the memory pointed by any pointer
// in the current basic block, it assumes the callee would.
//
//===----------------------------------------------------------------------===//


#include "llvm/Transforms/Scalar/CheckedCKeyCheckOpt.h"
#include "llvm/Transforms/Scalar/CheckedCSplitBB.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

char CheckedCKeyCheckOptPass::ID = 0;

// Constructor
CheckedCKeyCheckOptPass::CheckedCKeyCheckOptPass() : ModulePass(ID) {
  initializeCheckedCKeyCheckOptPassPass(*PassRegistry::getPassRegistry());
}

// Return the name of this pass.
StringRef CheckedCKeyCheckOptPass::getPassName() const {
  return "CheckedCKeyCheckOpt";
}

void CheckedCKeyCheckOptPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CheckedCSplitBBPass>();
  AU.addPreserved<CheckedCSplitBBPass>();

  // LLVM's AA is too conservative and may not help.
  AU.addRequired<AAResultsWrapperPass>();
}

//
// Entrance of this pass.
//
bool CheckedCKeyCheckOptPass::runOnModule(Module &M) {
  bool changed = false;

  CheckedCSplitBBPass &SBB = getAnalysis<CheckedCSplitBBPass>();

  return changed;
}

// Create a new pass.
ModulePass *llvm::createCheckedCKeyCheckOptPass(void) {
  return new CheckedCKeyCheckOptPass();
}

// Initialize the pass.
INITIALIZE_PASS_BEGIN(CheckedCKeyCheckOptPass, "checkedc-key-check-opt",
                      "Checked C Redundant Key Check Removal", false, false);
INITIALIZE_PASS_DEPENDENCY(CheckedCSplitBBPass);
INITIALIZE_PASS_END(CheckedCKeyCheckOptPass, "checkedc-key-check-opt",
                    "Checked C Redundant Key Check Removal", false, false);
