#include "native_pointer.h"
#include "native_process.h"
#include <cstring>
#include <sstream>


namespace chromatic::js {

// ---- Construction ----

NativePointer::NativePointer(const std::string &hex) {
  if (hex.empty()) {
    addr_ = 0;
    return;
  }
  addr_ = std::stoull(hex, nullptr, 16);
}

// ---- Conversion ----

std::string NativePointer::toString() const {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr_;
  return oss.str();
}

int NativePointer::toInt32() const {
  return static_cast<int>(static_cast<int32_t>(addr_));
}

uint32_t NativePointer::toUInt32() const {
  return static_cast<uint32_t>(addr_);
}

double NativePointer::toNumber() const { return static_cast<double>(addr_); }

// ---- Comparison ----

bool NativePointer::isNull() const { return addr_ == 0; }

bool NativePointer::equals(std::shared_ptr<NativePointer> other) const {
  if (!other)
    return addr_ == 0;
  return addr_ == other->addr_;
}

int NativePointer::compare(std::shared_ptr<NativePointer> other) const {
  uint64_t o = other ? other->addr_ : 0;
  if (addr_ < o)
    return -1;
  if (addr_ > o)
    return 1;
  return 0;
}

// ---- Arithmetic ----

std::shared_ptr<NativePointer> NativePointer::add(int64_t rhs) const {
  return std::make_shared<NativePointer>(addr_ + static_cast<uint64_t>(rhs));
}

std::shared_ptr<NativePointer> NativePointer::sub(int64_t rhs) const {
  return std::make_shared<NativePointer>(addr_ - static_cast<uint64_t>(rhs));
}

std::shared_ptr<NativePointer>
NativePointer::and_(std::shared_ptr<NativePointer> rhs) const {
  return std::make_shared<NativePointer>(addr_ & (rhs ? rhs->addr_ : 0));
}

std::shared_ptr<NativePointer>
NativePointer::or_(std::shared_ptr<NativePointer> rhs) const {
  return std::make_shared<NativePointer>(addr_ | (rhs ? rhs->addr_ : 0));
}

std::shared_ptr<NativePointer>
NativePointer::xor_(std::shared_ptr<NativePointer> rhs) const {
  return std::make_shared<NativePointer>(addr_ ^ (rhs ? rhs->addr_ : 0));
}

std::shared_ptr<NativePointer> NativePointer::shr(int n) const {
  return std::make_shared<NativePointer>(addr_ >> n);
}

std::shared_ptr<NativePointer> NativePointer::shl(int n) const {
  return std::make_shared<NativePointer>(addr_ << n);
}

std::shared_ptr<NativePointer> NativePointer::not_() const {
  int ptrSize = NativeProcess::getPointerSize();
  uint64_t mask =
      (ptrSize == 8) ? 0xFFFFFFFFFFFFFFFFULL : 0xFFFFFFFFULL;
  return std::make_shared<NativePointer>((~addr_) & mask);
}

// ---- Memory Read ----

int NativePointer::readU8() const {
  return *reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(addr_));
}

int NativePointer::readS8() const {
  return *reinterpret_cast<const int8_t *>(static_cast<uintptr_t>(addr_));
}

int NativePointer::readU16() const {
  uint16_t v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

int NativePointer::readS16() const {
  int16_t v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

uint32_t NativePointer::readU32() const {
  uint32_t v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

int32_t NativePointer::readS32() const {
  int32_t v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

uint64_t NativePointer::readU64() const {
  uint64_t v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

int64_t NativePointer::readS64() const {
  int64_t v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

double NativePointer::readFloat() const {
  float v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return static_cast<double>(v);
}

double NativePointer::readDouble() const {
  double v;
  std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(addr_)),
               sizeof(v));
  return v;
}

std::shared_ptr<NativePointer> NativePointer::readPointer() const {
  int ptrSize = NativeProcess::getPointerSize();
  if (ptrSize == 8) {
    return std::make_shared<NativePointer>(readU64());
  }
  return std::make_shared<NativePointer>(
      static_cast<uint64_t>(readU32()));
}

std::vector<uint8_t> NativePointer::readByteArray(int length) const {
  auto ptr = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(addr_));
  return std::vector<uint8_t>(ptr, ptr + length);
}

std::string NativePointer::readCString(int maxLength) const {
  auto ptr = reinterpret_cast<const char *>(static_cast<uintptr_t>(addr_));
  std::string result;
  for (int i = 0; i < maxLength; i++) {
    char ch = ptr[i];
    if (ch == 0)
      break;
    result += ch;
  }
  return result;
}

std::string NativePointer::readUtf8String(int maxLength) const {
  // UTF-8 is just a byte sequence; read until null terminator.
  auto ptr = reinterpret_cast<const char *>(static_cast<uintptr_t>(addr_));
  std::string result;
  for (int i = 0; i < maxLength; i++) {
    char ch = ptr[i];
    if (ch == 0)
      break;
    result += ch;
  }
  return result;
}

// ---- Memory Write ----

std::shared_ptr<NativePointer> NativePointer::writeU8(int v) {
  *reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(addr_)) =
      static_cast<uint8_t>(v);
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeS8(int v) {
  *reinterpret_cast<int8_t *>(static_cast<uintptr_t>(addr_)) =
      static_cast<int8_t>(v);
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeU16(int v) {
  uint16_t val = static_cast<uint16_t>(v);
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &val,
              sizeof(val));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeS16(int v) {
  int16_t val = static_cast<int16_t>(v);
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &val,
              sizeof(val));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeU32(uint32_t v) {
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &v,
              sizeof(v));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeS32(int32_t v) {
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &v,
              sizeof(v));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeU64(uint64_t v) {
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &v,
              sizeof(v));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeS64(int64_t v) {
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &v,
              sizeof(v));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeFloat(double v) {
  float val = static_cast<float>(v);
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &val,
              sizeof(val));
  return shared_from_this();
}

std::shared_ptr<NativePointer> NativePointer::writeDouble(double v) {
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)), &v,
              sizeof(v));
  return shared_from_this();
}

std::shared_ptr<NativePointer>
NativePointer::writePointer(std::shared_ptr<NativePointer> v) {
  int ptrSize = NativeProcess::getPointerSize();
  uint64_t val = v ? v->addr_ : 0;
  if (ptrSize == 8) {
    return writeU64(val);
  }
  return writeU32(static_cast<uint32_t>(val));
}

std::shared_ptr<NativePointer>
NativePointer::writeByteArray(std::vector<uint8_t> bytes) {
  std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(addr_)),
              bytes.data(), bytes.size());
  return shared_from_this();
}

std::shared_ptr<NativePointer>
NativePointer::writeUtf8String(const std::string &str) {
  auto dest = reinterpret_cast<char *>(static_cast<uintptr_t>(addr_));
  std::memcpy(dest, str.data(), str.size());
  dest[str.size()] = '\0'; // null terminator
  return shared_from_this();
}

} // namespace chromatic::js
