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
#include "llvm/IR/IRBuilder.h"
#include "../../IR/ConstantsContext.h"
#include "llvm/IR/IntrinsicInst.h"
#include <array>

using namespace llvm;

#ifndef DEBUG_TYPE
#define DEBUG_TYPE "CheckedCKeyCheckOptPass"
#endif

#define ADD_CHECK_BEFORE_CALL
#undef ADD_CHECK_BEFORE_CALL

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

//---------- Helper Functions ------------------------------------------------//
//
bool isInt64Ty(Type *T) {
  if (IntegerType *IT = dyn_cast<IntegerType>(T)) {
    return IT->getBitWidth() == 64;
  }

  return false;
}
//
//---------- End of Helper Functions -----------------------------------------//

//
// Function: addKeyCheckForCalls()
//
// This function adds dynamic key check(s) for checked pointer argument(s)
// right before the call. This has two potential benefits.
//
// First, without this the compiler inserts at least one key check for
// each checked pointer function argument as long as the argument is
// dereferenced in the function. Moving the check before the function call
// may save some unnecessary checks. For example, if function foo() calls bar()
// which calls baz(), and foo() passes a checked pointer p to bar() and bar()
// passes the same pointer to baz(). Without the check before the call to bar(),
// there would be at least two key checks, one in bar() and one in baz().
// With this pass, there could be only one check before call to bar()
// because the one before the call to baz() can be optimized away.
//
// Second, this may make the compiler optimize away unnecessary metadata
// propagation. If a function never does key check on a checked pointer
// argument or never propagates it, the compiler can omit passing the
// metadata at the asm level when the function is called.
//
void CheckedCKeyCheckOptPass::addKeyCheckForCalls(Module &M) {
  Function *MMPtrCheckFn = M.getFunction(MMPTRCHECK_FN),
           *MMArrayPtrCheckFn = M.getFunction(MMARRAYPTRCHECK_FN);
  assert((MMPtrCheckFn && MMArrayPtrCheckFn) && "Key Check Functions not Found");

  for (Function &F : M) {
    // Use a container to hold materials for add key check calls later.
    std::vector<std::array<Value*, 4>> AddKeyChecks;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (CallBase *Call = dyn_cast<CallBase>(&I)) {
          // Would the next line make the compiling a little faster or slower?
          if (isa<IntrinsicInst>(Call)) continue;
          for (unsigned i = 0; i < Call->arg_size(); i++) {
            Value *arg = Call->getArgOperand(i)->stripPointerCasts();
            // Before this pass the compiler breaks a checked pointer argument
            // to scalar types. For the current implementation, MMPtr is broken
            // to "pointee_type*, i64" and MMArrayPtr becomes
            // "pointee_type*, i64, i64*". The following code tries to find
            // MMSafe pointers based on these facts.
            if (arg->getType()->isPointerTy() && i + 1 < Call->arg_size() &&
                isInt64Ty(Call->getArgOperand(i + 1)->getType())) {
              Type *MMSafePtrTy = nullptr;
              Value *MMSafePtrPtr = nullptr;
              if (isa<ExtractValueInst>(arg)) {
                Value *V = cast<ExtractValueInst>(arg)->getAggregateOperand();
                if ((MMSafePtrTy = V->getType())->isMMSafePointerTy()) {
                  if (isa<LoadInst>(V)) {
                    MMSafePtrPtr = cast<LoadInst>(V)->getPointerOperand();
                  } else if (isa<CallBase>(V)) {
                    const DataLayout &DL = M.getDataLayout();
                    MMSafePtrPtr = new AllocaInst(MMSafePtrTy,
                                                  DL.getAllocaAddrSpace(),
                                                  "AllocaForMMSafePtr",
                                                  Call);
                    new StoreInst(V, MMSafePtrPtr, Call);
                  } else {
                    assert(false && "Unknown type");
                  }
                }
              } else if (isa<LoadInst>(arg)) {
                if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(
                      cast<LoadInst>(arg)->getPointerOperand())) {
                  if ((MMSafePtrTy = GEP->getSourceElementType()
                        )->isMMSafePointerTy()) {
                    MMSafePtrPtr = GEP->getPointerOperand();
                  }
                }
              } else {
                // Are there any other cases to handle?
              }
              if (!MMSafePtrPtr) continue;

              // Found an MMSafe pointer argument.
              Function *KeyCheckFn;
              if (MMSafePtrTy->isMMPointerTy()) {
                KeyCheckFn = MMPtrCheckFn;
                i++;
              } else {
                KeyCheckFn = MMArrayPtrCheckFn;
                i += 2;
              }
              AddKeyChecks.push_back
                (std::array<Value*, 4>({Call, arg, MMSafePtrPtr, KeyCheckFn}));
            }
          }
        }
      } // End of BasicBlock iterator
    } // End of Function iterator

    // Insert key checks for a call as needed.
    for (std::array<Value *, 4> &KeyCheck : AddKeyChecks) {
      Instruction *Call = cast<Instruction>(KeyCheck[0]);
      Value *PtrArg = KeyCheck[1], *MMSafePtrPtr = KeyCheck[2];
      Function *KeyCheckFn = cast<Function>(KeyCheck[3]);
      BasicBlock *OldBB = Call->getParent();

      // Split the BB by the Call instruction.
      BasicBlock *BBWithCall = OldBB->splitBasicBlock(Call, "");

      // First check if the pointer is NULL.
      IRBuilder<> Builder(&OldBB->back());
      Value *Cond = Builder.CreateIsNotNull(PtrArg);
      BasicBlock *KeyCheckBB = BasicBlock::Create(M.getContext(),
                                                   "KeyCheckForCall",
                                                   Call->getFunction());
      Builder.CreateCondBr(Cond, KeyCheckBB, BBWithCall);
      Builder.SetInsertPoint(KeyCheckBB);
      MMSafePtrPtr = Builder.CreatePointerCast(MMSafePtrPtr,
                                               KeyCheckFn->arg_begin()->getType());
      // Insert a call to a proper key check function. This check would be
      // optimized away later if the same MMSafe pointer is checked earlier.
      CallInst *CheckFnCall = Builder.CreateCall(KeyCheckFn->getFunctionType(),
                                                  KeyCheckFn, {MMSafePtrPtr});
      // It's necessary to explicitly set the calling convention to "fastcc";
      // it would otherwise cause the compiler to generate an
      // unreachable instruction for this call.  When the compiler inserts
      // key check call during IR function generation, it doesn't explicitly set
      // the CC but somehow the "fastcc" is added later.
      // Jie Zhou: I don't understand the reason for this.
      CheckFnCall->setCallingConv(CallingConv::Fast);
      Builder.CreateBr(BBWithCall);

      // Delete the original unconditional branch in the old BB.
      OldBB->back().eraseFromParent();
    }
  } // End of Module iterator
}

//
// Function: addCheckedArgToFnFront()
//
// This is a helper function for Opt(). It adds the pointer(s) to mmsafe pointer
// argument(s) to the checked pointer sets for the first BB of a function.
// This action is based on the fact that mmsafe pointer arguments have been
// checked before a function call.
//
static void addCheckedArgToFnFront(Module &M, BBValueSetMap_t &BBOut) {
  for (Function &F : M) {
    for (Function::arg_iterator AI = F.arg_begin(); AI != F.arg_end(); AI++) {
      if (AI->getType()->isPointerTy() &&
          AI->getArgNo() < F.arg_size() - 1 && isInt64Ty((AI + 1)->getType())) {
        // This might be an mmsafe pointer; if so, there would be an alloca
        // of struct for it and the raw pointer argument of the mmsafe pointer
        // would be stored to the location pointed by the first field of the
        // alloca struct.
        for (User *U : AI->users()) {
          if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            if (GetElementPtrInst *GEP =
                dyn_cast<GetElementPtrInst>(SI->getPointerOperand())) {
              Type *SrcElemTy = GEP->getSourceElementType();
              if (SrcElemTy->isMMSafePointerTy()) {
                BBOut[&F.front()].insert(GEP->getPointerOperand());
                AI = AI + (SrcElemTy->isMMPointerTy() ? 1 : 2);
                break;
              }
            }
          }
        }
      }
    }
  }
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
  // a little time to later iterate over BBs that do not contain key check calls.
  Function *MMPtrCheckFn = M.getFunction(MMPTRCHECK_FN);
  Function *MMArrayPtrCheckFn = M.getFunction(MMARRAYPTRCHECK_FN);
  // Map a BB to all the key check calls in it.
  BBInstSetMap_t BBWithChecks;
  // Map each key check function call to its argument.
  std::unordered_map<Instruction *, Value *> KeyCheckCallArg;
  if (MMPtrCheckFn) {
    for (User *U : MMPtrCheckFn->users()) {
      if (CallBase *Call = dyn_cast<CallBase>(U)) {
        BBWithChecks[Call->getParent()].insert(Call);
        Value *KeyCheckArg = Call->getArgOperand(0);
        // For non-global variables, this is a bitcast.
        KeyCheckArg = KeyCheckArg->stripPointerCasts();
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
        KeyCheckArg = KeyCheckArg->stripPointerCasts();
        KeyCheckCallArg.insert(std::pair<Instruction*, Value*>(Call, KeyCheckArg));
      }
    }
  }

  // Valid pointers of checked pointers at the beginning and end of a BB.
  BBValueSetMap_t BBIn;
  BBValueSetMap_t BBOut;

#ifdef ADD_CHECK_BEFORE_CALL
  addCheckedArgToFnFront(M, BBOut);
#endif

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
          // Skip BBs that may free.
          continue;
        }

        //  Propagate from BBIn to BBOut.
        ValueSet_t CheckedPtrBBIn(BBIn[BB]);
        for (Instruction &I : BBRef) {
          if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
            CheckedPtrBBIn.erase(SI->getPointerOperand());
          }
        }
        changed |= SetUnion(BBOut[BB], CheckedPtrBBIn);

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
    } while (changed == true);     // End of propagation

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
          // Check if a store may kill any checked mmsafe pointer.
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

#ifdef ADD_CHECK_BEFORE_CALL
  addKeyCheckForCalls(M);
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
