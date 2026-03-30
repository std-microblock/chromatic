#include "native_process.h"
#include <cstdint>
#include <cstring>
#include <sstream>

#ifdef CHROMATIC_WINDOWS
#include <windows.h>

#include <dbghelp.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#endif

#ifdef CHROMATIC_DARWIN
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#endif

#if defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
#include <elf.h>
#include <fstream>
#include <link.h>
#endif

namespace {

std::string toHexAddr(uint64_t addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr;
  return oss.str();
}

uint64_t parseHexAddr(const std::string &s) {
  return std::stoull(s, nullptr, 16);
}

} // namespace

namespace chromatic::js {

std::string NativeProcess::getArchitecture() {
#ifdef CHROMATIC_ARM64
  return "arm64";
#elif defined(CHROMATIC_X64)
  return "x64";
#else
  return "unknown";
#endif
}

std::string NativeProcess::getPlatform() {
#ifdef CHROMATIC_WINDOWS
  return "windows";
#elif defined(CHROMATIC_ANDROID)
  return "android";
#elif defined(CHROMATIC_LINUX)
  return "linux";
#elif defined(CHROMATIC_DARWIN)
  return "darwin";
#else
  return "unknown";
#endif
}

int NativeProcess::getPointerSize() { return sizeof(void *); }

int NativeProcess::getPageSize() {
#ifdef CHROMATIC_WINDOWS
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return static_cast<int>(si.dwPageSize);
#else
  return static_cast<int>(sysconf(_SC_PAGESIZE));
#endif
}

int NativeProcess::getProcessId() {
#ifdef CHROMATIC_WINDOWS
  return static_cast<int>(GetCurrentProcessId());
#else
  return static_cast<int>(getpid());
#endif
}

std::string NativeProcess::getCurrentThreadId() {
#ifdef CHROMATIC_WINDOWS
  return toHexAddr(static_cast<uint64_t>(GetCurrentThreadId()));
#elif defined(CHROMATIC_DARWIN)
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return toHexAddr(tid);
#else
  return toHexAddr(static_cast<uint64_t>(
      reinterpret_cast<uintptr_t>(reinterpret_cast<void *>(pthread_self()))));
#endif
}

std::vector<ModuleInfo> NativeProcess::enumerateModules() {
  std::vector<ModuleInfo> result;

#ifdef CHROMATIC_WINDOWS
  HMODULE hMods[1024];
  DWORD cbNeeded;
  HANDLE hProcess = GetCurrentProcess();

  if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
    int count = cbNeeded / sizeof(HMODULE);
    for (int i = 0; i < count; i++) {
      MODULEINFO modInfo;
      char modName[MAX_PATH];

      if (GetModuleInformation(hProcess, hMods[i], &modInfo, sizeof(modInfo)) &&
          GetModuleFileNameExA(hProcess, hMods[i], modName, sizeof(modName))) {
        std::string fullPath = modName;
        std::string name = fullPath;
        auto pos = name.find_last_of("\\/");
        if (pos != std::string::npos)
          name = name.substr(pos + 1);

        result.push_back(
            {name, toHexAddr(reinterpret_cast<uint64_t>(modInfo.lpBaseOfDll)),
             static_cast<int>(modInfo.SizeOfImage), fullPath});
      }
    }
  }

#elif defined(CHROMATIC_DARWIN)
  uint32_t count = _dyld_image_count();
  for (uint32_t i = 0; i < count; i++) {
    const char *imageName = _dyld_get_image_name(i);
    const struct mach_header *header = _dyld_get_image_header(i);

    if (!imageName || !header)
      continue;

    // Get the size from segments
    uint64_t imageSize = 0;
    if (header->magic == MH_MAGIC_64) {
      auto header64 = reinterpret_cast<const struct mach_header_64 *>(header);
      auto cmd = reinterpret_cast<const struct load_command *>(header64 + 1);
      for (uint32_t j = 0; j < header64->ncmds; j++) {
        if (cmd->cmd == LC_SEGMENT_64) {
          auto seg = reinterpret_cast<const struct segment_command_64 *>(cmd);
          if (seg->vmaddr + seg->vmsize > imageSize)
            imageSize = seg->vmaddr + seg->vmsize;
        }
        cmd = reinterpret_cast<const struct load_command *>(
            reinterpret_cast<const uint8_t *>(cmd) + cmd->cmdsize);
      }
    }

    std::string fullPath = imageName;
    std::string name = fullPath;
    auto pos = name.find_last_of('/');
    if (pos != std::string::npos)
      name = name.substr(pos + 1);

    result.push_back({name, toHexAddr(reinterpret_cast<uint64_t>(header)),
                      static_cast<int>(imageSize), fullPath});
  }

#elif defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  // Parse /proc/self/maps to find loaded shared objects
  std::ifstream maps("/proc/self/maps");
  std::string line;

  struct LinuxModInfo {
    std::string name;
    std::string path;
    uint64_t base;
    uint64_t end;
    std::vector<SegmentInfo> segments;
  };
  std::vector<LinuxModInfo> modules;

  while (std::getline(maps, line)) {
    uint64_t start, end;
    char perms[5];
    uint64_t offset;
    unsigned int devMaj, devMin;
    unsigned long inode;
    char pathname[512] = {0};

    if (sscanf(line.c_str(), "%lx-%lx %4s %lx %x:%x %lu %511s", &start, &end,
               perms, &offset, &devMaj, &devMin, &inode, pathname) >= 7) {
      if (pathname[0] == '/' || pathname[0] == '[') {
        std::string pathStr = pathname;
        bool found = false;
        for (auto &m : modules) {
          if (m.path == pathStr) {
            if (start < m.base)
              m.base = start;
            if (end > m.end)
              m.end = end;
            m.segments.push_back({start, end - start});
            found = true;
            break;
          }
        }
        if (!found && pathname[0] == '/') {
          std::string name = pathStr;
          auto pos = name.find_last_of('/');
          if (pos != std::string::npos)
            name = name.substr(pos + 1);
          modules.push_back(
              {name, pathStr, start, end, {{start, end - start}}});
        }
      }
    }
  }

  for (const auto &m : modules) {
    result.push_back({m.name, toHexAddr(m.base),
                      static_cast<int>(m.end - m.base), m.path, m.segments});
  }
#endif

  return result;
}

std::vector<RangeInfo>
NativeProcess::enumerateRanges(const std::string &protection) {
  std::vector<RangeInfo> result;

  auto matchesProt = [&](const std::string &rangeProt) -> bool {
    if (protection.empty())
      return true;
    for (char c : protection) {
      if (c != '-' && rangeProt.find(c) == std::string::npos)
        return false;
    }
    return true;
  };

#ifdef CHROMATIC_WINDOWS
  MEMORY_BASIC_INFORMATION mbi;
  auto addr = reinterpret_cast<const uint8_t *>(0);

  while (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
    if (mbi.State == MEM_COMMIT) {
      std::string prot;
      switch (mbi.Protect & 0xFF) {
      case PAGE_EXECUTE_READWRITE:
        prot = "rwx";
        break;
      case PAGE_EXECUTE_READ:
        prot = "r-x";
        break;
      case PAGE_READWRITE:
        prot = "rw-";
        break;
      case PAGE_READONLY:
        prot = "r--";
        break;
      case PAGE_EXECUTE:
        prot = "--x";
        break;
      default:
        prot = "---";
      }

      if (matchesProt(prot)) {
        result.push_back(
            {toHexAddr(reinterpret_cast<uint64_t>(mbi.BaseAddress)),
             static_cast<int>(mbi.RegionSize), prot, ""});
      }
    }
    addr += mbi.RegionSize;
    if (reinterpret_cast<uintptr_t>(addr) == 0)
      break;
  }

#elif defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  std::ifstream maps("/proc/self/maps");
  std::string line;

  while (std::getline(maps, line)) {
    uint64_t start, end;
    char perms[5];
    char pathname[512] = {0};

    if (sscanf(line.c_str(), "%lx-%lx %4s %*x %*x:%*x %*lu %511s", &start, &end,
               perms, pathname) >= 3) {
      std::string prot;
      prot += (perms[0] == 'r') ? 'r' : '-';
      prot += (perms[1] == 'w') ? 'w' : '-';
      prot += (perms[2] == 'x') ? 'x' : '-';

      if (matchesProt(prot)) {
        std::string filePath;
        if (pathname[0] == '/')
          filePath = pathname;
        result.push_back(
            {toHexAddr(start), static_cast<int>(end - start), prot, filePath});
      }
    }
  }

#elif defined(CHROMATIC_DARWIN)
  mach_port_t task = mach_task_self();
  vm_address_t address = 0;
  vm_size_t vmsize = 0;

  while (true) {
    struct vm_region_basic_info_64 info;
    mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName;

    kern_return_t kr = vm_region_64(
        task, &address, &vmsize, VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info), &infoCount, &objectName);

    if (kr != KERN_SUCCESS)
      break;

    std::string prot;
    prot += (info.protection & VM_PROT_READ) ? 'r' : '-';
    prot += (info.protection & VM_PROT_WRITE) ? 'w' : '-';
    prot += (info.protection & VM_PROT_EXECUTE) ? 'x' : '-';

    if (matchesProt(prot)) {
      result.push_back(
          {toHexAddr(address), static_cast<int>(vmsize), prot, ""});
    }

    address += vmsize;
  }
#endif

  return result;
}

std::string NativeProcess::findExportByName(const std::string &moduleName,
                                            const std::string &exportName) {
#ifdef CHROMATIC_WINDOWS
  if (moduleName.empty()) {
    HMODULE hMods[1024];
    DWORD cbNeeded = 0;
    HANDLE hProcess = GetCurrentProcess();

    if (!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
      return "0x0";

    const auto count = static_cast<size_t>(cbNeeded / sizeof(HMODULE));
    for (size_t i = 0; i < count; i++) {
      if (FARPROC proc = GetProcAddress(hMods[i], exportName.c_str()))
        return toHexAddr(reinterpret_cast<uint64_t>(proc));
    }
    return "0x0";
  }

  HMODULE hMod = GetModuleHandleA(moduleName.c_str());
  if (!hMod)
    return "0x0";
  if (FARPROC proc = GetProcAddress(hMod, exportName.c_str()))
    return toHexAddr(reinterpret_cast<uint64_t>(proc));
  return "0x0";
#else
  void *handle = nullptr;
  if (moduleName.empty()) {
    handle = RTLD_DEFAULT;
  } else {
    handle = dlopen(moduleName.c_str(), RTLD_NOLOAD | RTLD_LAZY);
    if (!handle) {
      handle = RTLD_DEFAULT;
    }
  }
  void *sym = dlsym(handle, exportName.c_str());
  if (!sym)
    return "0x0";
  return toHexAddr(reinterpret_cast<uint64_t>(sym));
#endif
}

std::optional<ModuleInfo>
NativeProcess::findModuleByAddress(const std::string &address) {
  uint64_t addr = parseHexAddr(address);

#ifdef CHROMATIC_WINDOWS
  HMODULE hMod;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(addr), &hMod))
    return std::nullopt;

  MODULEINFO modInfo;
  char modName[MAX_PATH];
  HANDLE hProcess = GetCurrentProcess();

  if (!GetModuleInformation(hProcess, hMod, &modInfo, sizeof(modInfo)) ||
      !GetModuleFileNameExA(hProcess, hMod, modName, sizeof(modName)))
    return std::nullopt;

  std::string fullPath = modName;
  std::string name = fullPath;
  auto pos = name.find_last_of("\\/");
  if (pos != std::string::npos)
    name = name.substr(pos + 1);

  return ModuleInfo{name,
                    toHexAddr(reinterpret_cast<uint64_t>(modInfo.lpBaseOfDll)),
                    static_cast<int>(modInfo.SizeOfImage), fullPath};

#elif defined(CHROMATIC_DARWIN)
  uint32_t count = _dyld_image_count();
  for (uint32_t i = 0; i < count; i++) {
    const struct mach_header *header = _dyld_get_image_header(i);
    if (!header)
      continue;

    uint64_t base = reinterpret_cast<uint64_t>(header);
    uint64_t imageSize = 0;

    if (header->magic == MH_MAGIC_64) {
      auto header64 = reinterpret_cast<const struct mach_header_64 *>(header);
      auto cmd = reinterpret_cast<const struct load_command *>(header64 + 1);
      for (uint32_t j = 0; j < header64->ncmds; j++) {
        if (cmd->cmd == LC_SEGMENT_64) {
          auto seg = reinterpret_cast<const struct segment_command_64 *>(cmd);
          if (seg->vmaddr + seg->vmsize > imageSize)
            imageSize = seg->vmaddr + seg->vmsize;
        }
        cmd = reinterpret_cast<const struct load_command *>(
            reinterpret_cast<const uint8_t *>(cmd) + cmd->cmdsize);
      }
    }

    if (addr >= base && addr < base + imageSize) {
      const char *imageName = _dyld_get_image_name(i);
      std::string fullPath = imageName ? imageName : "";
      std::string name = fullPath;
      auto pos = name.find_last_of('/');
      if (pos != std::string::npos)
        name = name.substr(pos + 1);

      return ModuleInfo{name, toHexAddr(base), static_cast<int>(imageSize),
                        fullPath};
    }
  }
  return std::nullopt;

#elif defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  Dl_info info;
  if (dladdr(reinterpret_cast<void *>(addr), &info) && info.dli_fname) {
    std::string fullPath = info.dli_fname;
    std::string name = fullPath;
    auto pos = name.find_last_of('/');
    if (pos != std::string::npos)
      name = name.substr(pos + 1);

    uint64_t base = reinterpret_cast<uint64_t>(info.dli_fbase);
    return ModuleInfo{name, toHexAddr(base), 0, fullPath};
  }
  return std::nullopt;
#else
  return std::nullopt;
#endif
}

std::optional<ModuleInfo>
NativeProcess::findModuleByName(const std::string &name) {
  auto modules = enumerateModules();
  for (const auto &m : modules) {
    if (m.name == name)
      return m;
  }
  return std::nullopt;
}

std::vector<ExportInfo>
NativeProcess::enumerateExports(const std::string &moduleName) {
  std::vector<ExportInfo> result;

#ifdef CHROMATIC_WINDOWS
  HMODULE hMod =
      GetModuleHandleA(moduleName.empty() ? nullptr : moduleName.c_str());
  if (!hMod)
    return result;

  auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hMod);
  if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    return result;

  auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
      reinterpret_cast<uint8_t *>(hMod) + dosHeader->e_lfanew);
  auto &exportDir =
      ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

  if (exportDir.VirtualAddress == 0)
    return result;

  auto exports = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
      reinterpret_cast<uint8_t *>(hMod) + exportDir.VirtualAddress);
  auto names = reinterpret_cast<DWORD *>(reinterpret_cast<uint8_t *>(hMod) +
                                         exports->AddressOfNames);
  auto functions = reinterpret_cast<DWORD *>(reinterpret_cast<uint8_t *>(hMod) +
                                             exports->AddressOfFunctions);
  auto ordinals = reinterpret_cast<WORD *>(reinterpret_cast<uint8_t *>(hMod) +
                                           exports->AddressOfNameOrdinals);

  for (DWORD i = 0; i < exports->NumberOfNames; i++) {
    auto expName = reinterpret_cast<const char *>(
        reinterpret_cast<uint8_t *>(hMod) + names[i]);
    auto funcAddr = reinterpret_cast<uint64_t>(
        reinterpret_cast<uint8_t *>(hMod) + functions[ordinals[i]]);

    result.push_back({"function", expName, toHexAddr(funcAddr)});
  }

#elif defined(CHROMATIC_DARWIN)
  void *handle = dlopen(moduleName.c_str(), RTLD_NOLOAD | RTLD_LAZY);
  if (!handle && !moduleName.empty()) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
      const char *imgName = _dyld_get_image_name(i);
      if (imgName) {
        std::string path = imgName;
        std::string shortName = path;
        auto pos = shortName.find_last_of('/');
        if (pos != std::string::npos)
          shortName = shortName.substr(pos + 1);
        if (shortName == moduleName || path == moduleName) {
          const struct mach_header *header = _dyld_get_image_header(i);
          intptr_t slide = _dyld_get_image_vmaddr_slide(i);

          if (header && header->magic == MH_MAGIC_64) {
            auto header64 =
                reinterpret_cast<const struct mach_header_64 *>(header);
            auto cmd =
                reinterpret_cast<const struct load_command *>(header64 + 1);

            const struct symtab_command *symtab = nullptr;
            const struct segment_command_64 *linkedit = nullptr;
            const struct segment_command_64 *text = nullptr;

            for (uint32_t j = 0; j < header64->ncmds; j++) {
              if (cmd->cmd == LC_SYMTAB) {
                symtab = reinterpret_cast<const struct symtab_command *>(cmd);
              } else if (cmd->cmd == LC_SEGMENT_64) {
                auto seg =
                    reinterpret_cast<const struct segment_command_64 *>(cmd);
                if (strcmp(seg->segname, SEG_LINKEDIT) == 0)
                  linkedit = seg;
                else if (strcmp(seg->segname, SEG_TEXT) == 0)
                  text = seg;
              }
              cmd = reinterpret_cast<const struct load_command *>(
                  reinterpret_cast<const uint8_t *>(cmd) + cmd->cmdsize);
            }

            if (symtab && linkedit && text) {
              uint64_t fileOff =
                  linkedit->vmaddr - text->vmaddr - linkedit->fileoff;
              auto syms = reinterpret_cast<const struct nlist_64 *>(
                  reinterpret_cast<uintptr_t>(header) + symtab->symoff +
                  fileOff);
              auto strs = reinterpret_cast<const char *>(
                  reinterpret_cast<uintptr_t>(header) + symtab->stroff +
                  fileOff);

              for (uint32_t s = 0; s < symtab->nsyms; s++) {
                if ((syms[s].n_type & N_EXT) &&
                    (syms[s].n_type & N_TYPE) == N_SECT) {
                  const char *symName = strs + syms[s].n_un.n_strx;
                  if (symName[0] == '_')
                    symName++;
                  uint64_t symAddr = syms[s].n_value + slide;

                  result.push_back({"function", symName, toHexAddr(symAddr)});
                }
              }
            }
          }
          break;
        }
      }
    }
  }
  if (handle)
    dlclose(handle);

#elif defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  // Walk loaded shared objects via dl_iterate_phdr to find the target module,
  // then parse its ELF dynamic symbol table (.dynsym / .dynstr).
  struct IterCtx {
    const std::string *moduleName;
    std::vector<ExportInfo> *result;
  };
  IterCtx ctx{&moduleName, &result};

  dl_iterate_phdr(
      [](struct dl_phdr_info *info, size_t, void *data) -> int {
        auto &ctx = *static_cast<IterCtx *>(data);
        std::string path = info->dlpi_name ? info->dlpi_name : "";
        std::string shortName = path;
        auto pos = shortName.find_last_of('/');
        if (pos != std::string::npos)
          shortName = shortName.substr(pos + 1);

        if (shortName != *ctx.moduleName && path != *ctx.moduleName)
          return 0; // continue iteration

        // Find PT_DYNAMIC
        const ElfW(Dyn) *dyn = nullptr;
        for (int i = 0; i < info->dlpi_phnum; i++) {
          if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const ElfW(Dyn) *>(
                info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
          }
        }
        if (!dyn)
          return 1; // stop

        const ElfW(Sym) *symtab = nullptr;
        const char *strtab = nullptr;
        size_t nchain = 0; // from DT_HASH
        const uint32_t *gnuBuckets = nullptr;
        size_t gnuNbuckets = 0;
        uint32_t gnuSymndx = 0; // from DT_GNU_HASH

        for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
          switch (d->d_tag) {
          case DT_SYMTAB:
            symtab = reinterpret_cast<const ElfW(Sym) *>(d->d_un.d_ptr);
            break;
          case DT_STRTAB:
            strtab = reinterpret_cast<const char *>(d->d_un.d_ptr);
            break;
          case DT_HASH: {
            auto hash = reinterpret_cast<const uint32_t *>(d->d_un.d_ptr);
            nchain = hash[1]; // nchain == number of symbols
            break;
          }
          case DT_GNU_HASH: {
            auto gh = reinterpret_cast<const uint32_t *>(d->d_un.d_ptr);
            gnuNbuckets = gh[0];
            gnuSymndx = gh[1];
            // bloom filter: gh[2] words starting at gh[4]
            // buckets follow bloom
            uint32_t bloomWords = gh[2];
            gnuBuckets = gh + 4 + bloomWords;
            break;
          }
          }
        }

        if (!symtab || !strtab)
          return 1; // stop

        // Determine symbol count: prefer DT_HASH, fall back to DT_GNU_HASH
        size_t nsyms = nchain;
        if (nsyms == 0 && gnuBuckets) {
          // Find max bucket value, then walk chain from there
          uint32_t maxSym = 0;
          for (size_t i = 0; i < gnuNbuckets; i++) {
            if (gnuBuckets[i] > maxSym)
              maxSym = gnuBuckets[i];
          }
          if (maxSym >= gnuSymndx) {
            const uint32_t *chains = gnuBuckets + gnuNbuckets;
            uint32_t idx = maxSym - gnuSymndx;
            while (!(chains[idx] & 1))
              idx++;
            nsyms = gnuSymndx + idx + 1;
          }
        }

        for (size_t i = 0; i < nsyms; i++) {
          const auto &sym = symtab[i];
          if (sym.st_shndx == SHN_UNDEF || sym.st_value == 0)
            continue;
          unsigned char bind = ELF64_ST_BIND(sym.st_info);
          if (bind != STB_GLOBAL && bind != STB_WEAK)
            continue;
          const char *name = strtab + sym.st_name;
          if (!name[0])
            continue;
          unsigned char type = ELF64_ST_TYPE(sym.st_info);
          const char *typeStr = (type == STT_FUNC) ? "function" : "variable";
          uint64_t addr = info->dlpi_addr + sym.st_value;
          ctx.result->push_back({typeStr, name, toHexAddr(addr)});
        }
        return 1; // stop — found our module
      },
      &ctx);
#endif

  return result;
}

} // namespace chromatic::js
