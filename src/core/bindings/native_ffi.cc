#include "native_ffi.h"
#include "native_pointer.h"
#include <cstdint>
#include <cstring>
#include <ffi.h>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

uint64_t parseHexAddr(const std::string &s) {
  return std::stoull(s, nullptr, 16);
}

std::string toHexAddr(uint64_t addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr;
  return oss.str();
}

ffi_type *getFFIType(const std::string &typeName) {
  if (typeName == "void")
    return &ffi_type_void;
  if (typeName == "int" || typeName == "int32" || typeName == "sint32")
    return &ffi_type_sint32;
  if (typeName == "uint" || typeName == "uint32")
    return &ffi_type_uint32;
  if (typeName == "long" || typeName == "int64" || typeName == "sint64")
    return &ffi_type_sint64;
  if (typeName == "ulong" || typeName == "uint64")
    return &ffi_type_uint64;
  if (typeName == "int8" || typeName == "sint8" || typeName == "char")
    return &ffi_type_sint8;
  if (typeName == "uint8" || typeName == "uchar")
    return &ffi_type_uint8;
  if (typeName == "int16" || typeName == "sint16" || typeName == "short")
    return &ffi_type_sint16;
  if (typeName == "uint16" || typeName == "ushort")
    return &ffi_type_uint16;
  if (typeName == "float")
    return &ffi_type_float;
  if (typeName == "double")
    return &ffi_type_double;
  if (typeName == "pointer" || typeName == "bool")
    return &ffi_type_pointer;
  return &ffi_type_void;
}

ffi_abi getFFIAbi(const std::string &abi) {
  if (abi == "default" || abi.empty())
    return FFI_DEFAULT_ABI;
#ifdef CHROMATIC_WINDOWS
#ifdef CHROMATIC_X64
  if (abi == "win64")
    return FFI_WIN64;
#else
  if (abi == "stdcall")
    return FFI_STDCALL;
#endif
#endif
  return FFI_DEFAULT_ABI;
}

// Callback management
struct CallbackInfo {
  std::function<std::string(std::vector<std::string>)> handler;
  ffi_closure *closure;
  void *codeAddr;
  ffi_cif cif;
  std::vector<ffi_type *> argTypes;
  ffi_type *retType;
};

std::mutex callbackMutex;
std::unordered_map<uint64_t, CallbackInfo *> callbacks;

void callbackHandler(ffi_cif *cif, void *ret, void **args, void *userData) {
  auto *info = static_cast<CallbackInfo *>(userData);

  // Serialize arguments to string vector
  std::vector<std::string> argStrs;
  for (unsigned i = 0; i < cif->nargs; i++) {
    if (cif->arg_types[i] == &ffi_type_pointer) {
      void *ptr = *static_cast<void **>(args[i]);
      argStrs.push_back(toHexAddr(reinterpret_cast<uint64_t>(ptr)));
    } else if (cif->arg_types[i] == &ffi_type_float) {
      argStrs.push_back(std::to_string(*static_cast<float *>(args[i])));
    } else if (cif->arg_types[i] == &ffi_type_double) {
      argStrs.push_back(std::to_string(*static_cast<double *>(args[i])));
    } else if (cif->arg_types[i] == &ffi_type_sint64) {
      argStrs.push_back(std::to_string(*static_cast<int64_t *>(args[i])));
    } else if (cif->arg_types[i] == &ffi_type_uint64) {
      argStrs.push_back(std::to_string(*static_cast<uint64_t *>(args[i])));
    } else {
      argStrs.push_back(
          std::to_string(static_cast<int64_t>(*static_cast<int *>(args[i]))));
    }
  }

  // Call the JS handler directly via std::function
  std::string result = info->handler(argStrs);

  // Parse result back into return value
  if (info->retType == &ffi_type_void) {
    return;
  } else if (info->retType == &ffi_type_pointer) {
    *static_cast<void **>(ret) = reinterpret_cast<void *>(parseHexAddr(result));
  } else if (info->retType == &ffi_type_float) {
    *static_cast<float *>(ret) = std::stof(result);
  } else if (info->retType == &ffi_type_double) {
    *static_cast<double *>(ret) = std::stod(result);
  } else if (info->retType == &ffi_type_sint64) {
    *static_cast<int64_t *>(ret) = std::stoll(result);
  } else if (info->retType == &ffi_type_uint64) {
    *static_cast<uint64_t *>(ret) = std::stoull(result);
  } else {
    // Default: int
    *static_cast<int *>(ret) = std::stoi(result);
  }
}

} // namespace

namespace chromatic::js {

std::string NativeFFI::callFunction(std::shared_ptr<NativePointer> address,
                                    const std::string &retType,
                                    const std::vector<std::string> &argTypes,
                                    const std::vector<std::string> &args,
                                    const std::string &abi) {
  uint64_t funcAddr = address->value();

  size_t nargs = argTypes.size();
  std::vector<ffi_type *> ffiArgTypes(nargs);
  for (size_t i = 0; i < nargs; i++) {
    ffiArgTypes[i] = getFFIType(argTypes[i]);
  }

  ffi_type *retFfiType = getFFIType(retType);
  ffi_cif cif;
  ffi_abi ffiAbi = getFFIAbi(abi);

  if (ffi_prep_cif(&cif, ffiAbi, static_cast<unsigned>(nargs), retFfiType,
                   ffiArgTypes.data()) != FFI_OK) {
    throw std::runtime_error("ffi_prep_cif failed");
  }

  // Prepare argument storage
  std::vector<uint64_t> argStorage(nargs);
  std::vector<double> floatStorage(nargs);
  std::vector<void *> argPtrs(nargs);

  for (size_t i = 0; i < nargs; i++) {
    if (ffiArgTypes[i] == &ffi_type_pointer) {
      argStorage[i] = parseHexAddr(args[i]);
      argPtrs[i] = &argStorage[i];
    } else if (ffiArgTypes[i] == &ffi_type_float) {
      float val = std::stof(args[i]);
      std::memcpy(&floatStorage[i], &val, sizeof(float));
      argPtrs[i] = &floatStorage[i];
    } else if (ffiArgTypes[i] == &ffi_type_double) {
      floatStorage[i] = std::stod(args[i]);
      argPtrs[i] = &floatStorage[i];
    } else if (ffiArgTypes[i] == &ffi_type_sint64 ||
               ffiArgTypes[i] == &ffi_type_uint64) {
      if (args[i].find("0x") == 0 || args[i].find("0X") == 0) {
        argStorage[i] = parseHexAddr(args[i]);
      } else {
        argStorage[i] = static_cast<uint64_t>(std::stoll(args[i]));
      }
      argPtrs[i] = &argStorage[i];
    } else {
      int64_t val = 0;
      if (args[i].find("0x") == 0 || args[i].find("0X") == 0) {
        val = static_cast<int64_t>(parseHexAddr(args[i]));
      } else {
        val = std::stoll(args[i]);
      }
      argStorage[i] = static_cast<uint64_t>(val);
      argPtrs[i] = &argStorage[i];
    }
  }

  // Call the function
  union {
    uint64_t u64;
    int64_t s64;
    double d;
    float f;
    void *ptr;
  } retVal;
  retVal.u64 = 0;

  ffi_call(&cif, reinterpret_cast<void (*)()>(funcAddr), &retVal,
           nargs > 0 ? argPtrs.data() : nullptr);

  // Format return value
  if (retType == "void")
    return "0";
  if (retType == "pointer")
    return toHexAddr(reinterpret_cast<uint64_t>(retVal.ptr));
  if (retType == "float")
    return std::to_string(retVal.f);
  if (retType == "double")
    return std::to_string(retVal.d);
  if (retType == "int64" || retType == "sint64" || retType == "long")
    return std::to_string(retVal.s64);
  if (retType == "uint64" || retType == "ulong")
    return std::to_string(retVal.u64);
  return std::to_string(
      static_cast<int64_t>(static_cast<int32_t>(retVal.u64 & 0xFFFFFFFF)));
}

std::shared_ptr<NativePointer> NativeFFI::createCallback(
    std::function<std::string(std::vector<std::string>)> handler,
    const std::string &retType, const std::vector<std::string> &argTypes,
    const std::string &abi) {
  size_t nargs = argTypes.size();

  auto *info = new CallbackInfo();
  info->handler = std::move(handler);
  info->retType = getFFIType(retType);
  info->argTypes.resize(nargs);
  for (size_t i = 0; i < nargs; i++) {
    info->argTypes[i] = getFFIType(argTypes[i]);
  }

  ffi_abi ffiAbi = getFFIAbi(abi);
  if (ffi_prep_cif(&info->cif, ffiAbi, static_cast<unsigned>(nargs),
                   info->retType, info->argTypes.data()) != FFI_OK) {
    delete info;
    throw std::runtime_error("ffi_prep_cif failed for callback");
  }

  info->closure = static_cast<ffi_closure *>(
      ffi_closure_alloc(sizeof(ffi_closure), &info->codeAddr));
  if (!info->closure) {
    delete info;
    throw std::runtime_error("ffi_closure_alloc failed");
  }

  if (ffi_prep_closure_loc(info->closure, &info->cif, callbackHandler, info,
                           info->codeAddr) != FFI_OK) {
    ffi_closure_free(info->closure);
    delete info;
    throw std::runtime_error("ffi_prep_closure_loc failed");
  }

  uint64_t addr = reinterpret_cast<uint64_t>(info->codeAddr);
  {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks[addr] = info;
  }

  return std::make_shared<NativePointer>(addr);
}

void NativeFFI::destroyCallback(std::shared_ptr<NativePointer> address) {
  uint64_t addr = address->value();

  std::lock_guard<std::mutex> lock(callbackMutex);
  auto it = callbacks.find(addr);
  if (it != callbacks.end()) {
    ffi_closure_free(it->second->closure);
    delete it->second;
    callbacks.erase(it);
  }
}

} // namespace chromatic::js
