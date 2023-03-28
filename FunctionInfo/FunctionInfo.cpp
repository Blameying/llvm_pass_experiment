//=============================================================================
// FILE:
//    HelloWorld.cpp
//
// DESCRIPTION:
//    Visits all functions in a module, prints their names and the number of
//    arguments via stderr. Strictly speaking, this is an analysis pass (i.e.
//    the functions are not modified). However, in order to keep things simple
//    there's no 'print' method here (every analysis pass should implement it).
//
// USAGE:
//    1. Legacy PM
//      opt -enable-new-pm=0 -load libHelloWorld.dylib -legacy-hello-world
//      -disable-output `\`
//        <input-llvm-file>
//    2. New PM
//      opt -load-pass-plugin=libHelloWorld.dylib -passes="hello-world" `\`
//        -disable-output <input-llvm-file>
//
//
// License: MIT
//=============================================================================
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/Casting.h>
#include <tuple>
#include <utility>

using namespace llvm;

//-----------------------------------------------------------------------------
// HelloWorld implementation
//-----------------------------------------------------------------------------
// No need to expose the internals of the pass to the outside world - keep
// everything in an anonymous namespace.
namespace {

struct FuncInfo {
  std::string name;
  unsigned argc;
  unsigned bb_counter;
  unsigned ins_counter;
  MapVector<const Function *, unsigned> calls;
};

void printWrapperFuncInfo(FuncInfo &info) {
  errs() << "Func Name: " << info.name << ", argc: " << info.argc
         << ", basic blocks: " << info.bb_counter
         << ", instructions: " << info.ins_counter;
  errs() << " called: {";
  for (auto f : info.calls) {
    errs() << f.first->getName() << ":" << f.second << ", ";
  }
  errs() << "}\n";
}

using ResultStaticCC = MapVector<const Function *, FuncInfo>;
struct StaticCallCounter : AnalysisInfoMixin<StaticCallCounter> {
  using Result = ResultStaticCC;
  Result run(Module &M, ModuleAnalysisManager &MAM) { return runOnModule(M); };

  Result runOnModule(Module &M) {
    MapVector<const Function *, FuncInfo> Res;

    auto funcInfo =
        FuncInfo{"", 0, 0, 0, MapVector<const Function *, unsigned>()};
    for (auto &Func : M) {
      funcInfo.name = Func.getName().str();
      funcInfo.argc = Func.arg_size();
      for (auto &BB : Func) {
        funcInfo.bb_counter++;
        for (auto &Ins : BB) {
          funcInfo.ins_counter++;
          auto *CB = dyn_cast<CallBase>(&Ins);
          if (nullptr == CB) {
            continue;
          }

          auto DirectInvoc = CB->getCalledFunction();
          if (nullptr == DirectInvoc) {
            continue;
          }

          auto CallCount = funcInfo.calls.find(DirectInvoc);
          if (CallCount == funcInfo.calls.end()) {
            CallCount =
                funcInfo.calls.insert(std::make_pair(DirectInvoc, 0)).first;
          }
          ++CallCount->second;
        }
      }
      if (Res.find(&Func) == Res.end()) {
        Res.insert(std::make_pair(&Func, funcInfo));
      }
    }

    return Res;
  };

  static bool isRequired() { return true; }

private:
  static AnalysisKey Key;
  friend struct AnalysisInfoMixin<StaticCallCounter>;
};

AnalysisKey StaticCallCounter::Key;

struct StaticCallCounterPrinter : PassInfoMixin<StaticCallCounterPrinter> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    auto DirectCalls = MAM.getResult<StaticCallCounter>(M);

    for (auto info : DirectCalls) {
      printWrapperFuncInfo(info.second);
    }
    return PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};
// Legacy PM implementation
// struct LegacyHelloWorld : public FunctionPass {
//  static char ID;
//  LegacyHelloWorld() : FunctionPass(ID) {}
//  // Main entry point - the name conveys what unit of IR this is to be run on.
//  bool runOnFunction(Function &F) override {
//    visitor(F);
//    // Doesn't modify the input unit of IR, hence 'false'
//    return false;
//  }
//};
} // namespace

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getHelloWorldPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FunctionInfo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "function-info") {
                    MPM.addPass(StaticCallCounterPrinter());
                    return true;
                  }
                  return false;
                });
            PB.registerAnalysisRegistrationCallback(
                [](ModuleAnalysisManager &MAM) {
                  MAM.registerPass([&] { return StaticCallCounter(); });
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize HelloWorld when added to the pass pipeline on the
// command line, i.e. via '-passes=hello-world'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getHelloWorldPluginInfo();
}

//-----------------------------------------------------------------------------
// Legacy PM Registration
//-----------------------------------------------------------------------------
// The address of this variable is used to uniquely identify the pass. The
// actual value doesn't matter.
// char LegacyHelloWorld::ID = 0;
//
//// This is the core interface for pass plugins. It guarantees that 'opt' will
//// recognize LegacyHelloWorld when added to the pass pipeline on the command
//// line, i.e.  via '--legacy-hello-world'
// static RegisterPass<LegacyHelloWorld>
//     X("legacy-hello-world", "Hello World Pass",
//       true, // This pass doesn't modify the CFG => true
//       false // This pass is not a pure analysis pass => false
//     );
