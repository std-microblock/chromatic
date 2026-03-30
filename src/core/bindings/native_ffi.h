#pragma once
#include "native_pointer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chromatic::js {
struct NativeFFI {
  /// Call a native function at `address`.
  /// retType:
  /// "void","int","uint","long","ulong","int8","uint8",...,"float","double","pointer"
  /// argTypes: vector of type strings
  /// args: vector of argument values (numbers or hex strings for pointers)
  /// abi: "default","sysv","stdcall","win64"
  /// Returns: result as string (number or hex address)
  static std::string callFunction(std::shared_ptr<NativePointer> address,
                                  const std::string &retType,
                                  const std::vector<std::string> &argTypes,
                                  const std::vector<std::string> &args,
                                  const std::string &abi);

  /// Create a native callback closure.
  /// handler: JS function that receives args as vector of strings, returns
  /// result string Returns: address of the native closure
  static std::shared_ptr<NativePointer>
  createCallback(std::function<std::string(std::vector<std::string>)> handler,
                 const std::string &retType,
                 const std::vector<std::string> &argTypes,
                 const std::string &abi);

  /// Destroy a previously created callback closure
  static void destroyCallback(std::shared_ptr<NativePointer> address);
};
} // namespace chromatic::js
