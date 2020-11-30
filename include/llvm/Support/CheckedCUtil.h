//===- CheckedCUtil.h - Utility Data Structures for Checkecd C ------------===//
//
//                     The LLVM Compiler Infrastructure
//
//        Copyright (c) 2019-2021, University of Rochester and Microsoft
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_SUPPORT_CHECKEDCUTIL_H
#define LLVM_SUPPORT_CHECKEDCUTIL_H

#include "llvm/IR/Function.h"

#include <set>
#include <unordered_set>
#include <vector>
#include <unordered_map>

namespace llvm {

// Data structures
typedef std::vector<Function *> FnList_t;
typedef std::unordered_set<BasicBlock *> BBSet_t;
typedef std::unordered_set<Instruction *> InstSet_t;
typedef std::unordered_set<Function *> FnSet_t;
typedef std::unordered_map<Function *, FnSet_t> FnFnSetMap_t;
typedef std::unordered_map<Function *, BBSet_t> FnBBSetMap_t;
typedef std::unordered_map<Function *, InstSet_t> FnInstSetMap_t;
typedef std::unordered_set<BasicBlock *, InstSet_t> BBInstSetMap_t;

// Functions

//
// Dump a set.
//
template<typename T>
void dumpSet(std::unordered_set<T*> S, std::string msg) {
  errs() << msg << "\n";
  if (std::is_same<T, Function>::value) {
    for (T *elem : S) { errs() << elem->getName() << "\n"; }
  } else if (std::is_base_of<Instruction, T>::value ||
             std::is_base_of<BasicBlock, T>::value) {
    for (T *elem : S) elem->dump();
  }
}

} // end of llvm namespace

#endif
