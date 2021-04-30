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

#define MMPTRCHECK_FN "MMPtrKeyCheck"
#define MMARRAYPTRCHECK_FN "MMArrayPtrKeyCheck"

// Data structures
typedef std::vector<Function *> FnList_t;
typedef std::unordered_set<BasicBlock *> BBSet_t;
typedef std::unordered_set<Instruction *> InstSet_t;
typedef std::unordered_set<Value *> ValueSet_t;
typedef std::unordered_set<Function *> FnSet_t;
typedef std::unordered_map<Function *, FnSet_t> FnFnSetMap_t;
typedef std::unordered_map<Function *, BBSet_t> FnBBSetMap_t;
typedef std::unordered_map<Function *, InstSet_t> FnInstSetMap_t;
typedef std::unordered_map<BasicBlock *, InstSet_t> BBInstSetMap_t;
typedef std::unordered_map<BasicBlock *, ValueSet_t> BBValueSetMap_t;
#define TSet_t std::unordered_set<T *>

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

//
// Function: SetInsection()
//
// This function computes the intersection of two sets.
//
template<typename T>
TSet_t SetIntersection(TSet_t &S1, TSet_t &S2) {
  TSet_t newSet;
  for (T *elem : S1) {
    if (S2.find(elem) != S2.end()) {
      newSet.insert(elem);
    }
  }

  return newSet;
}

//
// Function: SetUnion()
//
// This function computes the union of two sets. It directly changes the first
// set and returns true if there the second set contains any element that
// is not in the original first set.
//
template<typename T>
bool SetUnion(TSet_t &S1, TSet_t &S2) {
  unsigned S1Size = S1.size();
  for (T *elem : S2) S1.insert(elem);
  return S1.size() > S1Size;
}

} // end of llvm namespace

#endif
