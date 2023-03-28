//=============================================================================
// FILE:
//      input_for_hello.c
//
// DESCRIPTION:
//      Sample input file for HelloWorld and InjectFuncCall
//
// License: MIT
//=============================================================================
int foo(int a) { return a * 2; }

int bar(int a, int b) { return (a + foo(b) * 2); }

int fez(int a, int b, int c) { return (a + bar(a, b) * 2 + c * 3); }

int main(int argc, char *argv[]) {
  int a = 123;
  int ret = 0;

  int b = a;
  int c = b;
  int d = c;

  int e = b + 1;
  int f = e - 1;

  ret += d;
  ret += f;
  ret += foo(a);
  ret += bar(a, ret);
  ret += fez(a, ret, 123);

  return ret;
}
