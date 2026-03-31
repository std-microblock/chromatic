#include "native_cmodule.h"
#include "native_pointer.h"
#include <libtcc.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// libc symbols we inject so TCC doesn't need libtcc1.a or any external lib
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace chromatic::js {

// ── Inject host process libc symbols into TCC ──────────────────────
// TCC in TCC_OUTPUT_MEMORY + nostdlib mode has no symbol resolver.
// We manually provide every C runtime symbol that compiled code might
// reference. This list is easily extensible — just add more entries.
static void injectRuntimeSymbols(TCCState *s,
                                  std::unordered_set<std::string> &injected) {
  // SYM: inject a symbol by name. For overloaded C++ functions we cast to
  // the C signature to disambiguate.
  // On macOS, TCC prefixes C symbol names with '_', so we track both forms.
#define SYM(fn)                                                                \
  do {                                                                         \
    tcc_add_symbol(s, #fn, (const void *)(uintptr_t) & ::fn);                 \
    injected.insert(#fn);                                                      \
    injected.insert("_" #fn);                                                  \
  } while (0)
#define SYMC(name, fptr)                                                       \
  do {                                                                         \
    tcc_add_symbol(s, name, (const void *)(fptr));                             \
    injected.insert(name);                                                     \
    injected.insert(std::string("_") + name);                                  \
  } while (0)

  // ── string.h ───────────────────────────────────────────────────
  SYM(memcpy);
  SYM(memmove);
  SYM(memset);
  SYM(memcmp);
  SYMC("memchr", (void *(*)(void *, int, size_t))::memchr);
  SYM(strlen);
  SYM(strcpy);
  SYM(strncpy);
  SYM(strcmp);
  SYM(strncmp);
  SYM(strcat);
  SYM(strncat);
  SYMC("strchr", (char *(*)(char *, int))::strchr);
  SYMC("strrchr", (char *(*)(char *, int))::strrchr);
  SYMC("strstr", (char *(*)(char *, const char *))::strstr);

  // ── stdio.h ────────────────────────────────────────────────────
  SYM(printf);
  SYM(fprintf);
  SYM(snprintf);
  SYM(sprintf);
  SYM(puts);
  SYM(putchar);
  SYM(fopen);
  SYM(fclose);
  SYM(fread);
  SYM(fwrite);
  SYM(fflush);
  SYM(fgets);
  SYM(fputs);

  // stdin/stdout/stderr are pointers, not functions
#ifdef __APPLE__
  // macOS: __stdinp/__stdoutp/__stderrp
  tcc_add_symbol(s, "___stdinp", (const void *)&__stdinp);
  tcc_add_symbol(s, "___stdoutp", (const void *)&__stdoutp);
  tcc_add_symbol(s, "___stderrp", (const void *)&__stderrp);
  injected.insert("___stdinp");
  injected.insert("____stdinp");
  injected.insert("___stdoutp");
  injected.insert("____stdoutp");
  injected.insert("___stderrp");
  injected.insert("____stderrp");
#elif defined(__ANDROID__) || defined(_WIN32)
  // Android Bionic and Windows UCRT: stdin/stdout/stderr expand to rvalues.
  // Capture them into static lvalues so we can take their address.
  {
    static FILE *s_stdin  = stdin;
    static FILE *s_stdout = stdout;
    static FILE *s_stderr = stderr;
    tcc_add_symbol(s, "stdin",  (const void *)&s_stdin);
    tcc_add_symbol(s, "stdout", (const void *)&s_stdout);
    tcc_add_symbol(s, "stderr", (const void *)&s_stderr);
  }
  injected.insert("stdin");
  injected.insert("_stdin");
  injected.insert("stdout");
  injected.insert("_stdout");
  injected.insert("stderr");
  injected.insert("_stderr");
#else
  tcc_add_symbol(s, "stdin", (const void *)&stdin);
  tcc_add_symbol(s, "stdout", (const void *)&stdout);
  tcc_add_symbol(s, "stderr", (const void *)&stderr);
  injected.insert("stdin");
  injected.insert("_stdin");
  injected.insert("stdout");
  injected.insert("_stdout");
  injected.insert("stderr");
  injected.insert("_stderr");
#endif

  // ── stdlib.h ───────────────────────────────────────────────────
  SYM(malloc);
  SYM(calloc);
  SYM(realloc);
  SYM(free);
  SYM(atoi);
  SYM(atol);
  SYM(atof);
  SYM(strtol);
  SYM(strtoul);
  SYM(strtod);
  SYM(abort);
  SYM(exit);
  SYMC("abs", (int (*)(int))::abs);
  SYM(labs);
  SYM(qsort);

  // ── math.h (subset) ───────────────────────────────────────────
  SYMC("sin", (double (*)(double))::sin);
  SYMC("cos", (double (*)(double))::cos);
  SYMC("tan", (double (*)(double))::tan);
  SYMC("sqrt", (double (*)(double))::sqrt);
  SYMC("fabs", (double (*)(double))::fabs);
  SYMC("floor", (double (*)(double))::floor);
  SYMC("ceil", (double (*)(double))::ceil);
  SYMC("pow", (double (*)(double, double))::pow);
  SYMC("log", (double (*)(double))::log);
  SYMC("log10", (double (*)(double))::log10);
  SYMC("exp", (double (*)(double))::exp);

#undef SYM
#undef SYMC
}

// ── Private implementation ─────────────────────────────────────────
struct NativeCModule::Impl {
  TCCState *tcc = nullptr;
  bool disposed = false;
  // Track injected runtime symbol names (with platform prefix) to filter them
  // out from listSymbols.
  std::unordered_set<std::string> injectedSymbols;

  ~Impl() {
    if (tcc && !disposed) {
      callFinalize();
      tcc_delete(tcc);
      tcc = nullptr;
    }
  }

  void callFinalize() {
    if (!tcc)
      return;
    auto *fn = (void (*)(void))tcc_get_symbol(tcc, "finalize");
    if (fn)
      fn();
  }
};

// ── Default constructor (for binding system) ───────────────────────
NativeCModule::NativeCModule() = default;

// ── Constructor: compile C source with TCC ─────────────────────────
NativeCModule::NativeCModule(const std::string &code,
                             const std::vector<std::string> &symbolNames,
                             const std::vector<uint64_t> &symbolAddresses)
    : $impl(std::make_unique<Impl>()) {

  if (symbolNames.size() != symbolAddresses.size()) {
    throw std::runtime_error(
        "CModule: symbolNames and symbolAddresses must have same length");
  }

  $impl->tcc = tcc_new();
  if (!$impl->tcc) {
    throw std::runtime_error("CModule: tcc_new() failed");
  }

  // Collect compilation errors
  std::string errors;
  tcc_set_error_func($impl->tcc, &errors,
                     [](void *opaque, const char *msg) {
                       auto *errs = static_cast<std::string *>(opaque);
                       if (!errs->empty())
                         *errs += '\n';
                       *errs += msg;
                     });

  // Don't look for libtcc1.a or any system libraries —
  // we link everything ourselves via tcc_add_symbol.
  tcc_set_options($impl->tcc, "-nostdlib");
  tcc_set_output_type($impl->tcc, TCC_OUTPUT_MEMORY);

  // Inject host process libc symbols
  injectRuntimeSymbols($impl->tcc, $impl->injectedSymbols);

  // Inject user-provided external symbols
  for (size_t i = 0; i < symbolNames.size(); i++) {
    tcc_add_symbol($impl->tcc, symbolNames[i].c_str(),
                   reinterpret_cast<const void *>(symbolAddresses[i]));
  }

  // Compile
  if (tcc_compile_string($impl->tcc, code.c_str()) < 0) {
    std::string msg = "CModule: compilation failed";
    if (!errors.empty())
      msg += ":\n" + errors;
    tcc_delete($impl->tcc);
    $impl->tcc = nullptr;
    throw std::runtime_error(msg);
  }

  // Relocate (link in memory)
  if (tcc_relocate($impl->tcc) < 0) {
    std::string msg = "CModule: relocation failed";
    if (!errors.empty())
      msg += ":\n" + errors;
    tcc_delete($impl->tcc);
    $impl->tcc = nullptr;
    throw std::runtime_error(msg);
  }

  // Call init() if present
  auto *initFn = (void (*)(void))tcc_get_symbol($impl->tcc, "init");
  if (initFn)
    initFn();
}

NativeCModule::~NativeCModule() = default;
NativeCModule::NativeCModule(NativeCModule &&) noexcept = default;
NativeCModule &NativeCModule::operator=(NativeCModule &&) noexcept = default;

// ── getSymbol ──────────────────────────────────────────────────────
std::shared_ptr<NativePointer>
NativeCModule::getSymbol(const std::string &name) {
  if (!$impl || !$impl->tcc || $impl->disposed)
    return std::make_shared<NativePointer>(uint64_t(0));

  void *sym = tcc_get_symbol($impl->tcc, name.c_str());
  if (!sym)
    return std::make_shared<NativePointer>(uint64_t(0));

  return std::make_shared<NativePointer>(reinterpret_cast<uint64_t>(sym));
}

// ── listSymbols ────────────────────────────────────────────────────
// Returns user-defined global symbol names (stripped of platform prefix).
// Filters out: injected runtime symbols, linker internal symbols.
std::vector<std::string> NativeCModule::listSymbols() {
  std::vector<std::string> names;
  if (!$impl || !$impl->tcc || $impl->disposed)
    return names;

  // Linker-internal symbol prefixes to filter out
  static const char *internalPrefixes[] = {
      "_etext",  "_edata",  "_end",
      "__preinit_array", "__init_array", "__fini_array",
      "__start_", "__stop_",
      nullptr
  };

  struct Ctx {
    std::vector<std::string> *out;
    const std::unordered_set<std::string> *injected;
  } ctx{&names, &$impl->injectedSymbols};

  tcc_list_symbols(
      $impl->tcc, &ctx,
      [](void *opaque, const char *rawName, const void * /*val*/) {
        auto *c = static_cast<Ctx *>(opaque);
        std::string name = rawName;

        // Skip if it's an injected runtime symbol
        if (c->injected->count(name))
          return;

        // Strip leading underscore (macOS ABI prefix)
        std::string stripped = name;
        if (!stripped.empty() && stripped[0] == '_')
          stripped = stripped.substr(1);

        // Skip linker internal symbols
        for (auto *p = internalPrefixes; *p; ++p) {
          if (stripped.find(*p) == 0 || name.find(*p) == 0)
            return;
        }

        c->out->push_back(stripped);
      });

  return names;
}

// ── dispose ────────────────────────────────────────────────────────
void NativeCModule::dispose() {
  if (!$impl || !$impl->tcc || $impl->disposed)
    return;

  $impl->callFinalize();
  tcc_delete($impl->tcc);
  $impl->tcc = nullptr;
  $impl->disposed = true;
}

} // namespace chromatic::js
