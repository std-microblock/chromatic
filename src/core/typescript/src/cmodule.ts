import { NativeCModule, NativePointer } from 'chromatic';
import { ptr } from './native-pointer';
import type { NativePointerValue } from './types';

/**
 * Built-in C preamble providing minimal standard library declarations.
 * This is prepended to user code so that common #include directives
 * become no-ops (the types/functions are already declared).
 *
 * To extend: add more declarations here. No need to recompile C++.
 */
const STD_PREAMBLE = `
/* === stdint.h / stddef.h / stdbool.h === */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int64_t ptrdiff_t;
#define NULL ((void*)0)
#define offsetof(type, member) __builtin_offsetof(type, member)
typedef int wchar_t;

/* === stdarg.h === */
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg
#define va_copy __builtin_va_copy

/* === stdbool.h === */
typedef _Bool bool;
#define true 1
#define false 0

/* === limits.h (subset) === */
#define CHAR_BIT 8
#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)
#define UINT_MAX 4294967295U
#define LONG_MAX 9223372036854775807LL
#define LONG_MIN (-9223372036854775807LL - 1)

/* === string.h === */
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

/* === stdio.h (subset) === */
typedef void FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int puts(const char *s);
int putchar(int c);

/* === stdlib.h (subset) === */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
double atof(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
void abort(void);
void exit(int status);
int abs(int j);
long labs(long j);

`;

/**
 * When user writes `#include <stdint.h>` etc., TCC will look for these.
 * Since we already declared everything in the preamble, we make these
 * includes resolve to empty stubs. The preamble approach means these
 * won't actually be used, but having them prevents "file not found" errors
 * if user code includes them explicitly.
 */
const STUB_HEADERS: Record<string, string> = {
  'stdint.h':   '/* provided by built-in preamble */',
  'stddef.h':   '/* provided by built-in preamble */',
  'stdbool.h':  '/* provided by built-in preamble */',
  'stdarg.h':   '/* provided by built-in preamble */',
  'string.h':   '/* provided by built-in preamble */',
  'stdlib.h':   '/* provided by built-in preamble */',
  'stdio.h':    '/* provided by built-in preamble */',
  'limits.h':   '/* provided by built-in preamble */',
};

/**
 * CModule — compile C source code at runtime using the internal TCC compiler.
 *
 * Compatible with Frida's CModule API:
 * - Global C functions are exposed as NativePointer properties on the instance
 * - Optional `symbols` parameter injects extern addresses
 * - `void init(void)` and `void finalize(void)` are called automatically
 * - `dispose()` eagerly unmaps the compiled code
 *
 * @example
 * ```js
 * const cm = new CModule(`
 *   int add(int a, int b) { return a + b; }
 * `);
 * const fn = new NativeFunction(cm.add, 'int', ['int', 'int']);
 * console.log(fn(3, 4)); // 7
 * cm.dispose();
 * ```
 */
export class CModule {
  private _native: NativeCModule | null;
  [key: string]: any;

  constructor(code: string, symbols?: Record<string, NativePointerValue>) {
    // Build the full source: preamble (all standard types/functions declared) + user code
    // The #line directive resets line numbers so error messages reference user code correctly
    const fullCode = STD_PREAMBLE + '\n#line 1 "input"\n' + code;

    // Extract symbol names and addresses from the symbols object
    const symNames: string[] = [];
    const symAddrs: number[] = [];
    if (symbols) {
      for (const [name, pointer] of Object.entries(symbols)) {
        symNames.push(name);
        symAddrs.push(ptr(pointer).value());
      }
    }

    // Compile via native binding
    this._native = new NativeCModule(fullCode, symNames, symAddrs);

    // Enumerate all global symbols and expose them as NativePointer properties
    const names = this._native.listSymbols();
    for (const name of names) {
      const sym = this._native.getSymbol(name);
      if (sym && !sym.isNull()) {
        this[name] = sym;
      }
    }
  }

  /**
   * Eagerly unmap the compiled module from memory.
   * Calls `void finalize(void)` if defined in the C source.
   */
  dispose(): void {
    if (this._native) {
      this._native.dispose();
      this._native = null;
    }
  }
}
