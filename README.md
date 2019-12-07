## Bistra

Bistra is a code generator and programming language designed to make it easier
to write high-performance kernels. The library is designed to allow state of the art
compiler optimizations and code generation of numeric programs.

## Getting Started

### System Requirements

Bistra builds and runs on macOS and Linux. The software depends on a modern C++
compiler that supports C++11, on CMake, LLVM and gtest.

#### Submodules

Load the submodules with the command:

  ```bash
  git submodule update --init --recursive
  ```

#### macOS

Install the required dependencies using either [Homebrew](https://brew.sh/) or
[MacPorts](https://www.macports.org/). If using Homebrew, run:

  ```bash
  brew install cmake ninja
  brew install llvm@8
  ```

If using MacPorts, run:

  ```bash
  port install cmake ninja llvm-8.0 
  ```

Note that LLVM is installed in a non-default location to avoid conflicts with
the system's LLVM --Homebrew usually installs LLVM in `/usr/local/opt/llvm/`,
whereas MacPorts installs it in `/opt/local/libexec/llvm-8.0/`. This means that
CMake will need to be told where to find LLVM when building.

### Configure and Build

To build the compiler, create a build directory and run cmake on the source
directory.

  ```bash
  mkdir build_bistra
  cd build_bistra
  cmake -G Ninja ../bistra -DLLVM_DIR=/usr/local/opt/llvm/lib/cmake/llvm/
  ninja
  ```

It's possible to configure and build the compiler with any CMake generator,
like GNU Makefiles, Ninja and Xcode build.

## Testing and Running

### Unit tests

The project has a few unit tests in the tests/unittests subdirectory. To run all
of them, simply run `ninja test`.

### C++ API examples

A few test programs that use the C++ API are found under the `tools/`
subdirectory. The unit tests also use the C++ API.

This is a small example that demonstrates how the C++ API can be used to
generate a matrix multiplication function.

```c++
Program *createMatMull(unsigned szI, unsigned szJ, unsigned szK) {
  // No debug location for the expression.
  auto loc = DebugLoc::npos();

  Program *p = new Program("gemm", loc);

  // Add the arguments (A, B, C) with the tensor sizes and named dimensions.
  auto *C = p->addArgument("C", {szI, szK}, {"I", "J"}, ElemKind::Float32Ty);
  auto *A = p->addArgument("A", {szI, szJ}, {"I", "K"}, ElemKind::Float32Ty);
  auto *B = p->addArgument("B", {szJ, szK}, {"K", "J"}, ElemKind::Float32Ty);

  // Create 3 loops with named indices.
  auto *I = new Loop("i", loc, szI, 1);
  auto *J = new Loop("j", loc, szJ, 1);
  auto *K = new Loop("k", loc, szK, 1);

  // Add the loop nest to the program.
  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(K);

  // Create the expression:
  // C[i, j] = A[i, k] * B[k, j];
  auto *ldA = new LoadExpr(A, {new IndexExpr(I), new IndexExpr(K)}, loc);
  auto *ldB = new LoadExpr(B, {new IndexExpr(K), new IndexExpr(J)}, loc);
  auto *mul = new BinaryExpr(ldA, ldB, BinaryExpr::BinOpKind::Mul, loc);
  auto *st =
      new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true, loc);

  // Insert the store statement into the innermost loop.
  K->addStmt(st);
  return p;
}
```


### Running the example programs

The following command will load the GEMM program, parse it, perform simple
clean-up optimizations such as LICM and promotion to registers, and finally dump
the program and measure the execution time of 10 iterations.

  ```bash
  ./bin/bistrac examples/transpose.m --opt --dump --time
  ```

The following command will diagnose the program and will print warnings about
buffer overflows, and performance suggestions, such as loops that blow the cache
and loads that need to be vectorized.

  ```bash
    ./bin/bistrac examples/batched_add.m --warn
  ```

The following command will auto-tune the program and save the best program in a textual llvm-ir file.

  ```bash
    ./bin/bistrac examples/gemm.m --tune --textual --out save.ll
  ```

The following commands will save the file as bytecode, and later load it and print it.
  ```bash
  ./bin/bistrac examples/gemm.m --bytecode --out 1.bc
  ./bin/bistrac 1.bc --dump
  ```


A typical optimization of a single program may look like this. First, auto-tune
some program, and save the best result to a bytecode file. Next, load the
bytecode file and evaluate it.

  ```bash
  ./bin/bistrac examples/gemm.m --bytecode --out 1.bc
  ./bin/bistrac 1.bc --dump --time
  ```

The following commands will generate code in binary, or assembly format.
The generated object will contain a function entry with the C signature:
`void batchnorm(float* out float* in, float* mean, float* var, float* gamma, float* beta); `

  ```bash
  ./bin/bistrac examples/batchnorm.m --opt --out file.o
  ./bin/bistrac examples/batchnorm.m --opt --out file.s --textual
  ```

