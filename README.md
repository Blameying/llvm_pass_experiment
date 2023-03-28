
## Introduction

This is only an exprienment to build the LLVM passes.

1. FunctionInfo analyses the information of a function, including the function name, parameters, and directly called counts.
2. LocalOpt implements a simple LVN(Local Value Numbering) algorithm to do the limited optimization.

## Build & Run
```shell
./FunctionInfo/compile_and_run.sh
./LocalOpt/compile_and_run.sh
```

The test input source file is the ./FunctionInfo/test\_input.c, you can edit it to implement more cases.
