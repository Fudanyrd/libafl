/*
   LibAFL - DumpCfg LLVM pass
   --------------------------------------------------

   Written by Fudanyrd <3096842671@qq.com>

   Copyright 2022-2023 AFLplusplus Project. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0


  ENVIRONMENT
    CFG_OUTPUT_PATH: output path of the generated cfg file.
*/
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <map>

#define INFO "\033[01;92m[+]\033[0;m "
#define ERR "\033[01;31m[!]\033[0;m "
#define ASSERT(cond) assert(cond)
#define assert_non_null(ptr) ASSERT(((ptr) != nullptr) && #ptr " is a nullptr")
#define die(message) ASSERT(0 && message)

#define TRACE_PC_GUARD __sanitizer_cov_trace_pc_guard
#define TRACE_PC_GUARD_NAME "__sanitizer_cov_trace_pc_guard"
#define TOSTR(X) #X

#define NOTE(message, ...)                                                     \
  do {                                                                         \
  } while (0)

using std::cerr;
using std::endl;
using std::map;
using std::ofstream;

template <typename K, typename V, typename _Compare, typename _Alloc>
inline V mapCheckAndGet(const map<K, V, _Compare, _Alloc> &table,
                        const K &key) {
  auto it = table.find(key);
  if (it == table.end()) {
    die("failed to find key in map");
  }

  return it->second;
}

namespace llvm {

class DumpCfgPass;

class DumpCfgPass : public PassInfoMixin<DumpCfgPass> {
private:
  map<StringRef, size_t> guardToOffset;

  bool getGuardIndexOfBasicBlock(BasicBlock &bb, size_t &ret) const;

  size_t eval(Operator *op) const;
  size_t eval(Value *value) const;

  /**
   * @return any of the predecessor of bb; or nullptr if `bb` does not have one.
   */
  static BasicBlock *getOnePredecessor(BasicBlock &bb);

  /**
   * @return any of the successor of bb; or nullptr if `bb` does not have one.
   */
  static BasicBlock *getOneSuccessor(BasicBlock &bb) {
    BasicBlock *ret = nullptr;
    auto it = succ_begin(&bb);
    if (it != succ_end(&bb)) {
      ret = *it;
      assert_non_null(ret);
    }

    return ret;
  }

  std::ofstream &dumpCfg(Function &fn, std::ofstream &fout) const;

  typedef int guard_t;

  bool isLLVMIntrinsicFn(Function &fn) const {
    auto n = fn.getName();
    // Not interested in these LLVM's functions
#if LLVM_VERSION_MAJOR >= 18
#define STARTS_WITH(str) (n.starts_with(str))
#else
#define STARTS_WITH(str) (n.startswith(str))
#endif
    if (STARTS_WITH("llvm.") || STARTS_WITH("sancov.") ||
        STARTS_WITH("asan.")) {
      return true;
    } else {
      return false;
    }
#undef STARTS_WITH
  }

public:
  DumpCfgPass() = default;
  virtual ~DumpCfgPass() = default;

  PreservedAnalyses run(Module &mod, ModuleAnalysisManager &MAM);
};

BasicBlock *DumpCfgPass::getOnePredecessor(BasicBlock &block) {
  Function &fn = *(block.getParent());

  for (BasicBlock &bb : fn) {
    for (auto it = succ_begin(&bb); it != succ_end(&bb); it++) {
      BasicBlock *succ = *it;
      assert_non_null(succ);
      if (succ == &block) {
        return &bb;
      }
    }
  }

  return nullptr;
}

PreservedAnalyses DumpCfgPass::run(Module &mod, ModuleAnalysisManager &MAM) {
  const std::string name = std::string(mod.getName());
  cerr << INFO "Running on module " << name << endl;
  auto &context = mod.getContext();
  const auto &layout = mod.getDataLayout();

  auto getGuardSizeByName = [&mod, &layout](StringRef varName) -> uint64_t {
    auto *sancovGenValue = mod.getNamedValue(varName);
    assert_non_null(sancovGenValue);

    if (sancovGenValue->getSection() != "__sancov_guards") {
      return 0;
    }

    bool isNull, isFreed;
    return sancovGenValue->getPointerDereferenceableBytes(layout, isNull,
                                                          isFreed) /
           sizeof(guard_t);
  };
  auto getGuardSize = getGuardSizeByName;

  Module::GlobalListType &list = mod.getGlobalList();
  size_t offset = 0;
  for (auto it = list.begin(); it != list.end(); ++it) {
    StringRef name = it->getName();
    if (it->getSection() == "__sancov_guards") {
      const auto arrayLen = getGuardSize(name);
      this->guardToOffset[name] = offset;
      offset += arrayLen * sizeof(guard_t);
    }
  }

  const char *outputPath = getenv("CFG_OUTPUT_PATH");
  assert_non_null(outputPath);

  std::ofstream fout(outputPath);
  for (Function &fn : mod) {
    dumpCfg(fn, fout);
  }
  fout.close();

  return PreservedAnalyses::all();
}

bool DumpCfgPass::getGuardIndexOfBasicBlock(BasicBlock &bb, size_t &ret) const {
  bool empty = true;
  for (Instruction &_inst : bb) {
    empty = false;
    break;
  }
  if (empty) {
    die("empty basic block found");
  }

  ret = 0;
  bool foundCallToPCGuard = false;
  for (Instruction &inst : bb) {
    auto *callInst = dyn_cast<CallInst>(&inst);
    if (callInst == nullptr) {
      /* Instrumented with ASAN, this may happen. */
      continue;
    }

    auto *fn = callInst->getCalledFunction();
    if (fn == nullptr || fn->getName() != TRACE_PC_GUARD_NAME) {
      /* Instrumented with ASAN, this may happen. */
      continue;
    }

    Value *arg = callInst->getArgOperand(0);
    if (!arg) {
      die("null argument to " TRACE_PC_GUARD_NAME);
    }

    ret = this->eval(arg) / sizeof(guard_t);
    foundCallToPCGuard = true;
    break;
  }

  return foundCallToPCGuard;
}

size_t DumpCfgPass::eval(Operator *op) const {
#define IF_IS_INSTANCE(type, varname)                                          \
  type *varname = dyn_cast<type>(op);                                          \
  if (varname != nullptr)

  IF_IS_INSTANCE(PtrToIntOperator, ptrToIntOp) {
    auto *operand = ptrToIntOp->getOperand(0);
    assert_non_null(operand);
    return this->eval(operand);
  }
  IF_IS_INSTANCE(IntToPtrInst, intToPtrOp) {
    /* FIXME: remove this block */
    auto *operand = intToPtrOp->getOperand(0);
    assert_non_null(operand);
    return this->eval(operand);
  }
  IF_IS_INSTANCE(AddOperator, addOp) {
    auto *leftOperand = addOp->getOperand(0);
    auto *rightOperand = addOp->getOperand(1);

    return eval(leftOperand) + eval(rightOperand);
  }

  NOTE("Possibly unhandled operator type ", op->getOpcode());
  ASSERT(op->getNumOperands() == 1 && "cannot assume op is a cast operator");
  auto *operand = op->getOperand(0);
  assert_non_null(operand);
  return this->eval(operand);

#undef IF_IS_INSTANCE
}

size_t DumpCfgPass::eval(Value *value) const {
  Operator *inst = dyn_cast<Operator>(value);
  if (inst) {
    return eval(inst);
  }

  auto *integer = dyn_cast<ConstantInt>(value);
  if (integer) {
    APInt val = integer->getValue();
    unsigned int bitWidth = val.getBitWidth();
    ASSERT(bitWidth <= 64);
    uint64_t ret = 0;
    for (unsigned int i = 0; i < bitWidth; i++) {
      if (val[i]) {
        uint64_t mask = 1;
        mask = mask << i;
        ret |= mask;
      }
    }
    ASSERT(ret % sizeof(guard_t) == 0);
    return ret;
  }

  // assert_non_null(dyn_cast<GlobalValue>(value));
  return mapCheckAndGet(guardToOffset, value->getName());
}

std::ofstream &DumpCfgPass::dumpCfg(Function &fn, std::ofstream &fout) const {
  if (this->isLLVMIntrinsicFn(fn)) {
    return fout;
  }

  cerr << INFO "Instrumenting function " << std::string(fn.getName()) << endl;
  map<BasicBlock *, size_t> bbToIndex;

#define SOLVED(bPtr, mapIter)                                                  \
  map<BasicBlock *, size_t>::const_iterator mapIter = bbToIndex.find(bPtr);    \
  if (mapIter != bbToIndex.end())

  /* Get the index into guards, for each basic block. */
  for (BasicBlock &bb : fn) {
    size_t index;
    if (this->getGuardIndexOfBasicBlock(bb, index)) {
      bbToIndex[&bb] = index;
    }
  }

  /* Iterate till fixed point (i.e. each basic blocks has index) */
  for (;;) {
    size_t oldSize = bbToIndex.size();

    for (BasicBlock &bb : fn) {
      auto *bPtr = &bb;
      SOLVED(bPtr, mapIter) {
        for (auto it = succ_begin(bPtr); it != succ_end(bPtr); it++) {
          BasicBlock *child = *it;
          assert_non_null(child);
          SOLVED(child, childIter) {}
          else {
            bbToIndex[child] = mapIter->second;
          }
        }
      }
      else {
        /* If one of its children is solved, use the result. */
        for (auto it = succ_begin(bPtr); it != succ_end(bPtr); it++) {
          BasicBlock *child = *it;
          SOLVED(child, childIter) {
            bbToIndex[bPtr] = childIter->second;
            break;
          }
        }
      }
    }

    if (bbToIndex.size() == oldSize) {
      break;
    }
  }

  for (BasicBlock &bb : fn) {
    SOLVED(&bb, iter) {}
    else {
      cerr << ERR "Function " << std::string(fn.getName())
           << " has incomplete instrumentation" << endl;
      return fout;
    }
  }

  /* Dump the inter-procedural CFG into `fout`. */
  for (BasicBlock &bb : fn) {
    const size_t curIndex = mapCheckAndGet(bbToIndex, &bb);
    for (auto it = succ_begin(&bb); it != succ_end(&bb); it++) {
      BasicBlock *dstBlock = *it;
      assert_non_null(dstBlock);
      const size_t dstIndex = mapCheckAndGet(bbToIndex, dstBlock);

      if (curIndex == dstIndex) {
        NOTE("src = dst in CFG. Your CFG is possibly incorrect");
        NOTE("if address sanitizer is used, ignore this warning.");
      } else {
        fout << curIndex << " " << dstIndex << endl;
      }
    }
  }

  return fout;
#undef SOLVED
}

} /* namespace llvm */

using namespace llvm;

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "dump-cfg", "v0.3", [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL
#if LLVM_VERSION_MAJOR >= 20
                   ,
                   ThinOrFullLTOPhase Phase
#endif

                ) { MPM.addPass(DumpCfgPass()); });
          }};
}
