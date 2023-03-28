//=============================================================================
// FILE:
//    LocalOpt.cpp
//
// DESCRIPTION:
//    Local Optimizations for basic block.
//    1. Algebraic Identities (x + 0 = 0 + x => x)
//    2. Strength Reductions (2 * x = x << 1)
//    3. Multi-Inst. Optimization (a = b + 1, c = a - 1 => a = b + 1, c = b)
//
//
// License: MIT
//=============================================================================
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/Casting.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

using namespace llvm;

//-----------------------------------------------------------------------------
// HelloWorld implementation
//-----------------------------------------------------------------------------
// No need to expose the internals of the pass to the outside world - keep
// everything in an anonymous namespace.
namespace {

struct lvn_item {
  int id;
  std::tuple<std::string, int, int> value;
  Value *ref;
};

void localValueNumbering(Function &F) {
  int lvn_counter = 0;
  std::unordered_map<std::string, int> hashmap;
  std::vector<lvn_item> lvn_table;
  std::unordered_map<Value *, Value *> replace_table;

  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (auto *BO = dyn_cast_or_null<BinaryOperator>(&Inst)) {
        std::string oper1 = std::string(BO->getOperand(0)->getName());
        std::string oper2 = std::string(BO->getOperand(1)->getName());

        auto insert_lvn = [&hashmap, &lvn_counter,
                           &lvn_table](std::string name, std::string op,
                                       int oper1, int oper2, Value *ref) {
          hashmap.insert(std::make_pair(name, lvn_counter));
          lvn_table.push_back(
              lvn_item{lvn_counter, std::make_tuple(op, oper1, oper2), ref});
          lvn_counter++;
        };

        auto register_handle = [&hashmap, &lvn_table, &lvn_counter, &BO,
                                &insert_lvn](std::string &oper, int index) {
          if (oper == "") {
            oper = std::to_string(((size_t)BO->getOperand(index)));
            if (hashmap.find(oper) == hashmap.end()) {
              insert_lvn(oper, "assign", 0, 0, BO->getOperand(index));
            }
          }
        };

        register_handle(oper1, 0);
        register_handle(oper2, 1);

        auto constant_handle = [&hashmap, &lvn_table, &lvn_counter, &BO,
                                &insert_lvn](std::string &oper, int index) {
          if (isa<ConstantInt>(BO->getOperand(index))) {
            int temp =
                (dyn_cast<ConstantInt>(BO->getOperand(index)))->getSExtValue();
            oper = std::to_string(temp);
            if (hashmap.find(oper) == hashmap.end()) {
              insert_lvn(oper, "constant", temp, temp, BO->getOperand(index));
            }
          }
        };

        constant_handle(oper1, 0);
        constant_handle(oper2, 1);

        auto iter_oper1 = hashmap.find(oper1);
        auto iter_oper2 = hashmap.find(oper2);

        /* if the symbol is not in the hashtable, they should be added in the
         * above code. */
        assert(iter_oper1 != hashmap.end());
        assert(iter_oper2 != hashmap.end());

        auto exp = std::make_tuple(Inst.getOpcodeName(), iter_oper1->second,
                                   iter_oper2->second);

        auto compare_func = [&lvn_table](std::tuple<std::string, int, int> tp) {
          for (int i = 0; i < lvn_table.size(); i++) {
            if (lvn_table[i].value == tp) {
              return i;
            }
          }
          return -1;
        };

        auto pattern_func = [&lvn_table](unsigned opcode, int oper1,
                                         int oper2) {
          /* a = b - 1, c = a + 1 => a = b - 1, c = b */
          if (opcode == Instruction::Add) {
            if ((std::get<0>(lvn_table[oper1].value) == "sub") &&
                (std::get<2>(lvn_table[oper1].value) == oper2)) {
              return std::get<1>(lvn_table[oper1].value);
            }
          }

          /* a = c + 1, b = a - 1; => a = c + 1, b = c */
          if (opcode == Instruction::Sub) {
            if ((std::get<0>(lvn_table[oper1].value) == "add") &&
                (std::get<2>(lvn_table[oper1].value) == oper2)) {
              return std::get<1>(lvn_table[oper1].value);
            }
          }

          return -1;
        };

        int result = compare_func(exp);
        if (result >= 0) {
          hashmap.insert(std::make_pair(std::to_string((size_t)&Inst), result));
          replace_table.insert(std::make_pair(&Inst, lvn_table[result].ref));
          continue;
        }

        result = pattern_func(Inst.getOpcode(), iter_oper1->second,
                              iter_oper2->second);
        if (result > 0) {
          hashmap.insert(std::make_pair(std::to_string((size_t)&Inst), result));
          replace_table.insert(std::make_pair(&Inst, lvn_table[result].ref));
          continue;
        }

        insert_lvn(std::to_string((size_t)&Inst), std::get<0>(exp),
                   std::get<1>(exp), std::get<2>(exp), &Inst);
      }
    }
  }

  // for (auto &pair : hashmap) {
  //   lvn_item *ptr = &lvn_table[pair.second];
  //   errs() << "Key: " << pair.first << " lvn: " << ptr->id << ", ("
  //          << std::get<0>(ptr->value) << ", " << std::get<1>(ptr->value) <<
  //          ", "
  //          << std::get<2>(ptr->value) << ")\n";
  // }

  for (auto &BB : F) {
    for (auto iter = BB.begin(); iter != BB.end();) {
      auto *inst = dyn_cast_or_null<BinaryOperator>(iter++);
      if (inst) {
        auto find_result = replace_table.find(inst);
        if (find_result != replace_table.end()) {
          inst->replaceAllUsesWith(find_result->second);
          inst->eraseFromParent();
        }
      }
    }
  }
}

struct LocalOptPass : PassInfoMixin<LocalOptPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    bool modified = false;

    errs() << "Original: \n";
    for (auto &BB : F) {
      for (auto iter = BB.begin(); iter != BB.end();) {
        errs() << *iter << "\n";
        if (auto *BO = dyn_cast_or_null<BinaryOperator>(iter++)) {
          /* x + 0 or 0 + x => x*/
          if (BO->getOpcode() == Instruction::BinaryOps::Add &&
              ((isa<ConstantInt>(BO->getOperand(1)) &&
                cast<ConstantInt>(BO->getOperand(1))->isZeroValue()) ||
               (isa<ConstantInt>(BO->getOperand(0)) &&
                cast<ConstantInt>(BO->getOperand(0))->isZeroValue()))) {
            BO->replaceAllUsesWith(BO->getOperand(
                (isa<ConstantInt>(BO->getOperand(0)) &&
                 cast<ConstantInt>(BO->getOperand(0))->isZeroValue())
                    ? 1
                    : 0));
            BO->eraseFromParent();
            modified = true;
          } else if (BO->getOpcode() == Instruction::BinaryOps::Mul &&
                     ((isa<ConstantInt>(BO->getOperand(0)) &&
                       (cast<ConstantInt>(BO->getOperand(0))->getZExtValue() ==
                        1)) ||
                      (isa<ConstantInt>(BO->getOperand(1)) &&
                       (cast<ConstantInt>(BO->getOperand(1))->getZExtValue() ==
                        1)))) {
            /* x * 1 or 1 * x => x */
            BO->replaceAllUsesWith(BO->getOperand(
                (isa<ConstantInt>(BO->getOperand(0)) &&
                 (cast<ConstantInt>(BO->getOperand(0))->getZExtValue() == 1))
                    ? 1
                    : 0));
            BO->eraseFromParent();
            modified = true;
          } else if (BO->getOpcode() == Instruction::BinaryOps::Mul &&
                     (isa<ConstantInt>(BO->getOperand(0)) ||
                      isa<ConstantInt>(BO->getOperand(1)))) {
            int op_index = isa<ConstantInt>(BO->getOperand(0)) ? 0 : 1;
            Value *Op = BO->getOperand(op_index);

            uint64_t need_to_shift = cast<ConstantInt>(Op)->getZExtValue();
            if (__builtin_popcount(need_to_shift) == 1) {
              uint64_t mask_value = 1;
              for (int i = 1; i < sizeof(uint64_t) * 8; i++) {
                mask_value <<= 1;
                if ((mask_value & need_to_shift) != 0) {
                  auto inst = BinaryOperator::CreateShl(
                      BO->getOperand(1 - op_index),
                      ConstantInt::get(Op->getType(), i));

                  ReplaceInstWithInst(BO, inst);

                  modified = true;
                  break;
                }
              }
            }
          }
        }
      }
    }
    errs() << "New: \n";
    for (auto &BB : F) {
      for (auto &I : BB) {
        errs() << I << "\n";
      }
    }

    errs() << "After LVN Optimization\n";
    localValueNumbering(F);
    for (auto &BB : F) {
      for (auto &I : BB) {
        errs() << I << "\n";
      }
    }

    return PreservedAnalyses::all();
  }
};
//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getLocalOptPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LocalOpt", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "local-opt") {
                    FPM.addPass(LocalOptPass());
                    return true;
                  }
                  return false;
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize HelloWorld when added to the pass pipeline on the
// command line, i.e. via '-passes=hello-world'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getLocalOptPluginInfo();
}

} // namespace

//-----------------------------------------------------------------------------
// Legacy PM Registration
//-----------------------------------------------------------------------------
// The address of this variable is used to uniquely identify the pass. The
// actual value doesn't matter.
// char LegacyHelloWorld::ID = 0;
//
//// This is the core interface for pass plugins. It guarantees that 'opt'
/// will / recognize LegacyHelloWorld when added to the pass pipeline on the
/// command / line, i.e.  via '--legacy-hello-world'
// static RegisterPass<LegacyHelloWorld>
//     X("legacy-hello-world", "Hello World Pass",
//       true, // This pass doesn't modify the CFG => true
//       false // This pass is not a pure analysis pass => false
//     );
