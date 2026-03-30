#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chromatic::js {

class NativePointer : public std::enable_shared_from_this<NativePointer> {
  uint64_t addr_;

public:
  NativePointer() : addr_(0) {}
  explicit NativePointer(uint64_t addr) : addr_(addr) {}
  explicit NativePointer(const std::string &hex);

  /// Raw address value.
  uint64_t value() const { return addr_; }

  // ---- Conversion ----

  /// Return "0x" + hex representation.
  std::string toString() const;
  /// Truncate to signed 32-bit integer.
  int toInt32() const;
  /// Truncate to unsigned 32-bit integer.
  uint32_t toUInt32() const;
  /// Convert to double (may lose precision for large addresses).
  double toNumber() const;

  // ---- Comparison ----

  bool isNull() const;
  bool equals(std::shared_ptr<NativePointer> other) const;
  int compare(std::shared_ptr<NativePointer> other) const;

  // ---- Arithmetic ----
  // All return a new NativePointer.

  std::shared_ptr<NativePointer> add(int64_t rhs) const;
  std::shared_ptr<NativePointer> sub(int64_t rhs) const;
  std::shared_ptr<NativePointer> and_(std::shared_ptr<NativePointer> rhs) const;
  std::shared_ptr<NativePointer> or_(std::shared_ptr<NativePointer> rhs) const;
  std::shared_ptr<NativePointer> xor_(std::shared_ptr<NativePointer> rhs) const;
  std::shared_ptr<NativePointer> shr(int n) const;
  std::shared_ptr<NativePointer> shl(int n) const;
  std::shared_ptr<NativePointer> not_() const;

  // ---- Memory Read ----

  int readU8() const;
  int readS8() const;
  int readU16() const;
  int readS16() const;
  uint32_t readU32() const;
  int32_t readS32() const;
  uint64_t readU64() const;
  int64_t readS64() const;
  double readFloat() const;
  double readDouble() const;
  std::shared_ptr<NativePointer> readPointer() const;
  /// Read `length` bytes → JS ArrayBuffer.
  std::vector<uint8_t> readByteArray(int length) const;
  std::string readCString(int maxLength) const;
  std::string readUtf8String(int maxLength) const;

  // ---- Memory Write ----
  // All return shared_ptr<this> for chaining.

  std::shared_ptr<NativePointer> writeU8(int v);
  std::shared_ptr<NativePointer> writeS8(int v);
  std::shared_ptr<NativePointer> writeU16(int v);
  std::shared_ptr<NativePointer> writeS16(int v);
  std::shared_ptr<NativePointer> writeU32(uint32_t v);
  std::shared_ptr<NativePointer> writeS32(int32_t v);
  std::shared_ptr<NativePointer> writeU64(uint64_t v);
  std::shared_ptr<NativePointer> writeS64(int64_t v);
  std::shared_ptr<NativePointer> writeFloat(double v);
  std::shared_ptr<NativePointer> writeDouble(double v);
  std::shared_ptr<NativePointer> writePointer(std::shared_ptr<NativePointer> v);
  /// Write from JS ArrayBuffer.
  std::shared_ptr<NativePointer> writeByteArray(std::vector<uint8_t> bytes);
  std::shared_ptr<NativePointer> writeUtf8String(const std::string &str);
};

} // namespace chromatic::js
