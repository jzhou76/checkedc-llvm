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

typedef std::vector<Function *> FnList_t;
typedef std::unordered_set<BasicBlock *> BBSet_t;
typedef std::unordered_set<Instruction *> InstSet_t;
typedef std::unordered_set<Function *> FnSet_t;
typedef std::unordered_map<Function *, FnSet_t> FnFnSetMap_t;
typedef std::unordered_map<Function *, BBSet_t> FnBBSetMap_t;

} // end of llvm namespace

#endif
