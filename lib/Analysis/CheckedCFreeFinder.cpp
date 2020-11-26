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

#include <stack>

using namespace llvm;

// The whitelist of functions that will not free heap memory.
std::unordered_set<std::string> MayFreeFnWL = {
  "malloc", "mm_alloc", "mm_array_alloc",
  // libc C functions. Need add more.
  "printf", "abort", "exit", "srand",
  "atoi", "atol",
  "abort",
};

// Map a function to all the functions that it can reach.
static FnFnSetMap_t FnReaching;
// Map a function to all the functions that can reach it.
static FnFnSetMap_t FnReached;

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
// Function: FnReachAnalysis()
//
// For each function F, find all the functions that F can reach on and
// all the functions that can reach F on the call graph.
//
// This is a helper function for FindMayFreeFns().
//
static void FnReachAnalysis(Module &M, CallGraph &CG) {
  std::stack<Function *> workingList;
  for (Function &F : M) {
    if (F.isDeclaration() || F.getName().contains("PtrKeyCheck")) continue;
    workingList.push(&F);
  }

  Function *F = NULL;
  FnSet_t visited;
  // Traverse the call graph to collect the function-reaching relations.
  // It is a breadth first search.
  while (!workingList.empty()) {
    do {
      F = workingList.top();
      workingList.pop();
    } while (visited.find(F) != visited.end() && !workingList.empty());
    if (visited.find(F) != visited.end()) return;

    visited.insert(F);
    FnSet_t visitedCallee;
    for (CallGraphNode::iterator CGNI = CG[F]->begin();
        CGNI != CG[F]->end(); CGNI++) {
      // The iterator gives a pair of <WeakTrackingVH, CallGraphNode *>
      Function *callee = CGNI->second->getFunction();

      if (visitedCallee.find(callee) != visitedCallee.end()) continue;
      visitedCallee.insert(callee);

      // Skip indirect calls unknown to the compiler (indirect calls)
      // and calls to functions defined in another source file or library.
      if (callee == NULL ||  callee->isDeclaration() ||
          callee->getName().contains("PtrKeyCheck")) {
        continue;
      }

      // Update the function-reach database.
      FnReaching[F].insert(callee);
      FnReached[callee].insert(F);
      // All functions that can reach F can reach callee.
      for (Function *reached : FnReached[F]) {
        FnReaching[reached].insert(callee);
        FnReached[callee].insert(reached);
      }
      // All functions reachable from callee can be reached by F.
      for (Function *reaching : FnReaching[callee]) {
        FnReaching[F].insert(reaching);
        FnReached[reaching].insert(F);
      }
      // All function that can reach F can reach all functions reachable
      // from callee.
      for (Function *reached : FnReached[F]) {
        for (Function *reaching : FnReaching[callee]) {
          FnReaching[reached].insert(reaching);
          FnReached[reaching].insert(reached);
        }
      }

      workingList.push(callee);
    }
  }
}

//
// Function: FindMayFreeFns()
//
// This function finds which user defined functions in the current module
// may directly or indirectly frees heap memory. It conservertively assuems
// that a function may free heap objects if it
// 1. has indirect call(s) that is not resolved by compiler or
// 2. calls functions defined in other module or library that or
// 3. indirectly calls a function that meets one of the first two conditions.
//
// For the second condition, we have a whitelist that contains all the functions
// that we are sure will not free memory, such as malloc. We need expand the
// list to include almost all libc functions.
//
// The algorithm is straightforward. First, it finds all the functions that
// directly meets condition 1 or 2. Second, it uses the result from the
// function-reaching analysis to find all the functions that may reach
// the functions found in step 1.
//
static void FindMayFreeFns(Module &M, CallGraph &CG, FnSet_t MayFreeFns) {
  MayFreeFnWL.insert(M.getName().str() + "_MMPtrKeyCheck");
  MayFreeFnWL.insert(M.getName().str() + "_MMArrayPtrKeyCheck");
  for (Function &caller : M) {
    if (caller.isDeclaration() || caller.getName().contains("PtrKeyCheck")) {
      // Skip functions defined outside this module and the key check functions.
      continue;
    }
    for (CallGraphNode::iterator CGNI = CG[&caller]->begin();
         CGNI != CG[&caller]->end(); CGNI++) {
      Function *callee = CGNI->second->getFunction();
      if (callee == NULL || (callee->isDeclaration() &&
          MayFreeFnWL.find(callee->getName()) == MayFreeFnWL.end())) {
        // We conservatively assume all indirect calls may free heap objects.
        // Also all functions not defined in this module may free heap.
        MayFreeFns.insert(&caller);
        break;
      }
    }
  }

  // Find functions that indirectly call free.
  for (Function *MayFreeFn : MayFreeFns) {
    for (Function *caller : FnReached[MayFreeFn]) {
      MayFreeFns.insert(caller);
    }
  }
}

//---- Debugging helper functions --------------------------------------------//
#if 0
//
// dumpFnReachingResult()
//
// This function prints out the analysis result of which function can reach
// which function(s).
//
static void dumpFnReachingResult(FnFnSetMap_t &funcReaching) {
  errs() << "========== Printing Out Function-Reaching Data ==========\n";
  for (auto &funcReach : funcReaching) {
    errs() << "Function " << funcReach.first->getName() << " can reach : ";
    for (Function *reaching : funcReaching[funcReach.first]) {
      errs() << reaching->getName() << " ";
    }
    errs() << "\n";
  }
  errs() << "========== End of Printing Function-Reaching Data ==========\n";
}

//
// Dump a list of functions.
//
static void dumpFuncSet(FnSet_t &funcs, std::string msg) {
  errs() << msg << "\n";
  for (Function *F : funcs) {
    errs() << F->getName() << "\n";
  }
}
#endif
//---- End of debugging helper functions -------------------------------------//

//
// Entrance of this pass.
//
bool CheckedCFreeFinderPass::runOnModule(Module &M) {
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();

  FnReachAnalysis(M, CG);
  FindMayFreeFns(M, CG, MayFreeFns);

  return false;
}

// Create a new instance of this pass.
ModulePass *llvm::createCheckedCFreeFinderPass(void) {
  return new CheckedCFreeFinderPass();
}

// Initialization
INITIALIZE_PASS_BEGIN(CheckedCFreeFinderPass, "checkedc-free-finder-pass",
                      "Checked C Free Finder pass", false, true);
INITIALIZE_PASS_END(CheckedCFreeFinderPass, "checkedc-free-finder-pass",
                    "Checked C Free Finder pass", false, true);
