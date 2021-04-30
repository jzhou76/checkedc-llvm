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
// for the ones that will not free mmsafe pointers or has only one call
// instruction that may free mmsafe pointers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/CheckedCSplitBB.h"
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
// Function: SplitBB()
//
// This function splits each basic block that has at least one function call
// that may free heap objects. The result is every new basic block either has
// no function calls that may free or has at most one function call that
// may free.
//
void CheckedCSplitBBPass::SplitBB(Module &M, InstSet_t &MayFreeCalls) {
  for (Instruction *Call : MayFreeCalls) {
    Instruction *CallNextInst = Call->getNextNode();
    assert(CallNextInst && "Next Instruction of the CallInst is NULL");
    BasicBlock *BB = Call->getParent();
    BasicBlock *newBB = BB;
    if (BB->getFirstNonPHI() != Call) {
      newBB = BB->splitBasicBlock(Call);
    }
    newBB = newBB->splitBasicBlock(CallNextInst);
    MayFreeBBs.insert(newBB->getPrevNode());
  }
}

//
// Entrance of this pass.
//
bool CheckedCSplitBBPass::runOnModule(Module &M) {
  InstSet_t &MayFreeCalls = getAnalysis<CheckedCFreeFinderPass>().MayFreeCalls;
  SplitBB(M, MayFreeCalls);

  return  MayFreeCalls.empty() ? false : true;
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
