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
#include "llvm/IR/Dominators.h"
#include "llvm/ADT/Statistic.h"
#include "../../IR/ConstantsContext.h"

using namespace llvm;

#define DEBUG_TYPE "CheckedCKeyCheckOptPass"
STATISTIC(NumDynamicKeyCheckRemoved, "The # of removed dynamic key checks");

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

#if 0
  // LLVM's AA is too conservative and may not help.
  AU.addRequired<AAResultsWrapperPass>();

  AU.addRequired<DominatorTreeWrapperPass>();
#endif
}

//
// Function: Opt()
//
// This is the main body of this pass. It is a pretty straightforward
// data-flow analysis. It computes the valid checked pointers (actually
// in the current implementation it collects the pointers to checked pointers)
// at the beginning and the end of each basic block. Valid checked pointers
// may propagate between BBs and within a BB. A valid pointer may be killed
// by a function call or a pointer update.
//
void CheckedCKeyCheckOptPass::Opt(Module &M) {
  BBSet_t &MayFreeBBs = getAnalysis<CheckedCSplitBBPass>().MayFreeBBs;

  // Find all BBs that have mmsafe pointer check calls.  This saves us
  // a little time of iterating BBs that do not contain key check calls.
  Function *MMPtrCheckFn = M.getFunction(MMPTRCHECK_FN);
  Function *MMArrayPtrCheckFn = M.getFunction(MMARRAYPTRCHECK_FN);
  // Map a BB to all the key check calls in it.
  BBInstSetMap_t BBWithChecks;
  // Map each key check function to its argument.
  std::unordered_map<Instruction *, Value *> KeyCheckCallArg;
  if (MMPtrCheckFn) {
    for (User *U : MMPtrCheckFn->users()) {
      if (CallBase *Call = dyn_cast<CallBase>(U)) {
        BBWithChecks[Call->getParent()].insert(Call);
        Value *KeyCheckArg = Call->getArgOperand(0);
        // For non-global variables, this is a bitcast.
        if (isa<BitCastInst>(KeyCheckArg)) {
          KeyCheckArg = cast<BitCastInst>(KeyCheckArg)->getOperand(0);
        }
        KeyCheckCallArg.insert(std::pair<Instruction*, Value*>(Call, KeyCheckArg));
      }
    }
  }
  if (MMArrayPtrCheckFn) {
    for (User *U : MMArrayPtrCheckFn->users()) {
      if (CallBase *Call = dyn_cast<CallBase>(U)) {
        BBWithChecks[Call->getParent()].insert(Call);
        Value *KeyCheckArg = Call->getArgOperand(0);
        // For non-global variables, this is a bitcast.
        if (isa<BitCastInst>(KeyCheckArg)) {
          KeyCheckArg = cast<BitCastInst>(KeyCheckArg)->getOperand(0);
        }
        KeyCheckCallArg.insert(std::pair<Instruction*, Value*>(Call, KeyCheckArg));
      }
    }
  }

  // Valid pointers of checked pointers at the beginning and end of a BB.
  BBValueSetMap_t BBIn;
  BBValueSetMap_t BBOut;

  // BB-local optimization and initialization of BBOut. Since there are no
  // other function calls (due to the SplitBB pass), a key check would survive
  // to the end of a BB if it is not updated in the BB.
  InstSet_t CheckToDel;
  for (auto BBKeyChecks : BBWithChecks) {
    BasicBlock *BB = BBKeyChecks.first;
    InstSet_t &KeyCheckInsts = BBKeyChecks.second;
    ValueSet_t &CheckedPtrs = BBOut[BB];
    for (BasicBlock::iterator BBI = BB->begin(); BBI != BB->end(); BBI++) {
      Instruction *I = &*BBI;
      if (KeyCheckInsts.find(I) != KeyCheckInsts.end()) {
        Value *KeyCheckArg = KeyCheckCallArg[I];
        if (CheckedPtrs.find(KeyCheckArg) != CheckedPtrs.end()) {
          // This checked pointer has already been checked.
          CheckToDel.insert(I);
        } else {
          CheckedPtrs.insert(KeyCheckArg);
        }
      } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        // JZ: Do we really need this check?

        // Check if the store may kill any checked mmsafe pointer.
        CheckedPtrs.erase(SI->getPointerOperand());
      }
    }
  }

  // Propagate checked pointers within and between basic blocks.
  // The BBIn[BB] is the intersection of all BB's predecessors' BBOut[Pred].
  for (Function &F : M) {
    bool changed = false;
    do {
      changed = false;
      for (BasicBlock &BBRef : F) {
        BasicBlock *BB = &BBRef;
        if (MayFreeBBs.find(BB) != MayFreeBBs.end()) {
          // Skil BBs that have non-key-check function calls.
          continue;
        }

        //  Propagate from BBIn to BBOut.
        ValueSet_t CheckedPtrBBIn(BBIn[BB]);
        for (Instruction &I : BBRef) {
          if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
            CheckedPtrBBIn.erase(SI->getPointerOperand());
          }
        }
        changed = SetUnion(BBOut[BB], CheckedPtrBBIn);

        // Propagate from BBOut to BBIn.
        if (&F.front() == BB) continue;  // Skip the first BB of a function.
        BasicBlock *firstPred = *pred_begin(BB);
        // If any predecessor of BB may free, then BBIn[BB] = empty;
        if (MayFreeBBs.find(firstPred) != MayFreeBBs.end()) {
          BBIn[BB].clear();
          continue;
        }
        ValueSet_t predIntersection(BBOut[firstPred]);
        for (pred_iterator pit = ++pred_begin(BB); pit != pred_end(BB); pit++) {
          if (MayFreeBBs.find(*pit) != MayFreeBBs.end()) {
            BBIn[BB].clear();
            continue;
          }
          predIntersection = SetIntersection(predIntersection, BBOut[*pit]);
        }
        changed |= SetUnion(BBIn[BB], predIntersection);
      }
    } while (changed == true);

    // Collect all redundant checks.
    for (auto BBKeyChecks : BBWithChecks) {
      BasicBlock *BB = BBKeyChecks.first;
      InstSet_t &KeyCheckInsts = BBKeyChecks.second;
      ValueSet_t &CheckedPtrs = BBIn[BB];
      for (BasicBlock::iterator BBI = BB->begin(); BBI != BB->end(); BBI++) {
        Instruction *I = &*BBI;
        if (KeyCheckInsts.find(I) != KeyCheckInsts.end()) {
          // Skip the check calls already marked to be deleted before.
          if (CheckToDel.find(I) != CheckToDel.end()) continue;

          Value *KeyCheckArg = KeyCheckCallArg[I];
          if (CheckedPtrs.find(KeyCheckArg) != CheckedPtrs.end()) {
            // This mmsafe pointer has already been checked.
            CheckToDel.insert(I);
          }
        } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
          // Check if the store may kill any checked mmsafe pointer.
          CheckedPtrs.erase(SI->getPointerOperand());
        }
      }
    }
  }

  // Remove redundant checks
  NumDynamicKeyCheckRemoved += CheckToDel.size();
  for (Instruction *I : CheckToDel) {
#if 0
    errs() << "Redundant check: "; I->dump();
#endif
    I->eraseFromParent();
  }
}

//
// Entrance of this pass.
//
bool CheckedCKeyCheckOptPass::runOnModule(Module &M) {
#if 0
  return false;
#endif

  Opt(M);

  return NumDynamicKeyCheckRemoved > 0;
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
