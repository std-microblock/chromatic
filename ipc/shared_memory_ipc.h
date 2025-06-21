#pragma once

#include <print>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <aclapi.h>
#include <memoryapi.h>
#include <processthreadsapi.h>
#include <sddl.h>
#include <windows.h>
#include <winnt.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace ipc {

class Exception : public std::runtime_error {
public:
  explicit Exception(const std::string &message)
      : std::runtime_error(message) {}
  explicit Exception(const char *message) : std::runtime_error(message) {}
};

namespace detail {

inline void throw_windows_error(const std::string &message) {
  DWORD error_code = ::GetLastError();
  throw ipc::Exception(message +
                       " (Windows Error: " + std::to_string(error_code) + ")");
}

class SidHolder {
public:
  SidHolder() : sid_buffer_() {}
  ~SidHolder() = default;
  bool CreateEveryone() {
    DWORD sid_size = 0;
    CreateWellKnownSid(WinWorldSid, nullptr, nullptr, &sid_size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
      return false;
    sid_buffer_.resize(sid_size);
    return !!CreateWellKnownSid(WinWorldSid, nullptr, Get(), &sid_size);
  }
  bool CreateUntrusted() {
    SID_IDENTIFIER_AUTHORITY mandatory_label_authority =
        SECURITY_MANDATORY_LABEL_AUTHORITY;
    DWORD sid_size = GetSidLengthRequired(1);
    sid_buffer_.resize(sid_size);
    if (!InitializeSid(Get(), &mandatory_label_authority, 1))
      return false;
    *(GetSidSubAuthority(Get(), 0)) = SECURITY_MANDATORY_UNTRUSTED_RID;
    return true;
  }
  PSID Get() {
    return sid_buffer_.empty() ? nullptr
                               : reinterpret_cast<PSID>(sid_buffer_.data());
  }
  DWORD GetLength() const { return static_cast<DWORD>(sid_buffer_.size()); }

private:
  std::vector<BYTE> sid_buffer_;
  SidHolder(const SidHolder &) = delete;
  SidHolder &operator=(const SidHolder &) = delete;
};

inline LPSECURITY_ATTRIBUTES get_sa() {
  static struct initiator {
    SECURITY_ATTRIBUTES sa_{};
    std::vector<BYTE> sd_buffer_;
    bool succ_ = false;
    initiator() {
      SidHolder everyone_sid;
      if (!everyone_sid.CreateEveryone())
        return;
      SidHolder untrusted_il_sid;
      if (!untrusted_il_sid.CreateUntrusted())
        return;
      const DWORD dacl_size =
          sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + everyone_sid.GetLength();
      std::vector<BYTE> dacl_buffer(dacl_size);
      PACL dacl = reinterpret_cast<PACL>(dacl_buffer.data());
      if (!InitializeAcl(dacl, dacl_size, ACL_REVISION))
        return;
      if (!AddAccessAllowedAce(dacl, ACL_REVISION,
                               SYNCHRONIZE | SEMAPHORE_ALL_ACCESS |
                                   EVENT_ALL_ACCESS | FILE_MAP_ALL_ACCESS,
                               everyone_sid.Get()))
        return;
      const DWORD sacl_size = sizeof(ACL) + sizeof(SYSTEM_MANDATORY_LABEL_ACE) +
                              untrusted_il_sid.GetLength();
      std::vector<BYTE> sacl_buffer(sacl_size);
      PACL sacl = reinterpret_cast<PACL>(sacl_buffer.data());
      if (!InitializeAcl(sacl, sacl_size, ACL_REVISION))
        return;
      if (!AddMandatoryAce(sacl, ACL_REVISION, 0,
                           SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
                           untrusted_il_sid.Get()))
        return;
      SECURITY_DESCRIPTOR sd_absolute = {0};
      if (!InitializeSecurityDescriptor(&sd_absolute,
                                        SECURITY_DESCRIPTOR_REVISION))
        return;
      if (!SetSecurityDescriptorDacl(&sd_absolute, TRUE, dacl, FALSE))
        return;
      if (!SetSecurityDescriptorSacl(&sd_absolute, TRUE, sacl, FALSE))
        return;
      DWORD sd_buffer_size = 0;
      MakeSelfRelativeSD(&sd_absolute, nullptr, &sd_buffer_size);
      if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return;
      sd_buffer_.resize(sd_buffer_size);
      PSECURITY_DESCRIPTOR sd_relative =
          reinterpret_cast<PSECURITY_DESCRIPTOR>(sd_buffer_.data());
      if (!MakeSelfRelativeSD(&sd_absolute, sd_relative, &sd_buffer_size))
        return;
      sa_.nLength = sizeof(sa_);
      sa_.lpSecurityDescriptor = sd_relative;
      sa_.bInheritHandle = FALSE;
      succ_ = true;
    }
  } handle;
  return handle.succ_ ? &handle.sa_ : nullptr;
}

struct MessageHeader {
  std::atomic<uint64_t> sequence{0};
  uint32_t data_size;
  bool is_first_fragment;
  bool is_last_fragment;
};

struct MessageSlot {
  MessageHeader header;
  char data[0];
};

struct ReaderSlot {

  std::atomic<DWORD> pid{0};

  std::atomic<uint64_t> sequence{0};
};

struct SharedMemoryHeader {

  std::atomic_flag write_lock = ATOMIC_FLAG_INIT;
  uint32_t capacity;
  uint32_t max_payload_size;
  uint32_t max_readers;

  std::atomic<uint64_t> write_index{0};

  std::atomic<uint64_t> sequence_generator{0};
};

} // namespace detail

template <size_t MaxReaders = 16> class Channel {
public:
  struct Config {
    size_t capacity = 128;
    size_t max_message_payload_size = 4096;
  };

  Channel() = default;
  ~Channel() { disconnect(); }
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  Channel(Channel &&) = delete;
  Channel &operator=(Channel &&) = delete;

  void connect(const std::string &name, const Config &config = {}) {
    if (is_connected())
      throw Exception("Channel is already connected.");

    config_ = config;
    name_ = name;
    std::wstring shm_name =
        L"IPC_Channel_SHM_" + std::wstring(name.begin(), name.end());
    std::wstring event_name =
        L"IPC_Channel_EVT_" + std::wstring(name.begin(), name.end());
    LPSECURITY_ATTRIBUTES sa = detail::get_sa();
    if (!sa)
      throw Exception("Failed to get security attributes.");

    const size_t header_size = get_header_size();
    const size_t slot_size =
        sizeof(detail::MessageSlot) + config_.max_message_payload_size;
    const size_t total_shm_size = header_size + config_.capacity * slot_size;

    bool is_creator = false;
    h_map_file_ = ::CreateFileMappingW(
        INVALID_HANDLE_VALUE, sa, PAGE_READWRITE,
        static_cast<DWORD>(total_shm_size >> 32),
        static_cast<DWORD>(total_shm_size & 0xFFFFFFFF), shm_name.c_str());

    if (h_map_file_ == NULL)
      detail::throw_windows_error("CreateFileMappingW failed for " + name);

    if (::GetLastError() != ERROR_ALREADY_EXISTS)
      is_creator = true;

    p_shared_mem_ =
        ::MapViewOfFile(h_map_file_, FILE_MAP_ALL_ACCESS, 0, 0, total_shm_size);
    if (p_shared_mem_ == NULL) {
      disconnect();
      detail::throw_windows_error("MapViewOfFile failed for " + name);
    }

    p_header_ = static_cast<detail::SharedMemoryHeader *>(p_shared_mem_);

    p_buffer_start_ = reinterpret_cast<char *>(p_shared_mem_) + header_size;

    if (is_creator) {

      new (p_header_) detail::SharedMemoryHeader();
      p_header_->capacity = static_cast<uint32_t>(config_.capacity);
      p_header_->max_payload_size =
          static_cast<uint32_t>(config_.max_message_payload_size);
      p_header_->max_readers = static_cast<uint32_t>(MaxReaders);

      for (size_t i = 0; i < MaxReaders; ++i) {
        new (get_reader_slot(i)) detail::ReaderSlot();
      }

    } else {

      if (p_header_->max_readers != MaxReaders) {
        disconnect();
        throw Exception(
            "Connection failed: Mismatched MaxReaders configuration.");
      }
      if (p_header_->capacity != config_.capacity ||
          p_header_->max_payload_size != config_.max_message_payload_size) {

        disconnect();
        throw Exception("Connection failed: Mismatched capacity or "
                        "max_payload_size configuration.");
      }
    }

    if (is_creator) {
      h_data_ready_event_ = ::CreateEventW(sa, TRUE, FALSE, event_name.c_str());
      if (h_data_ready_event_ == NULL) {
        disconnect();
        detail::throw_windows_error("CreateEventW failed for " + name);
      }
    } else {
      h_data_ready_event_ = ::OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE,
                                         FALSE, event_name.c_str());
      if (h_data_ready_event_ == NULL) {
        disconnect();
        detail::throw_windows_error("OpenEventW failed for " + name);
      }
    }

    register_reader();
  }

  void disconnect() {

    if (is_connected()) {
      deregister_reader();
    }

    if (p_shared_mem_ != nullptr) {
      ::UnmapViewOfFile(p_shared_mem_);
      p_shared_mem_ = nullptr;
    }
    if (h_map_file_ != NULL) {
      ::CloseHandle(h_map_file_);
      h_map_file_ = NULL;
    }
    if (h_data_ready_event_ != NULL) {
      ::CloseHandle(h_data_ready_event_);
      h_data_ready_event_ = NULL;
    }
    p_header_ = nullptr;
    p_buffer_start_ = nullptr;
    my_reader_slot_index_ = -1;
    name_.clear();
  }

  bool is_connected() const { return p_shared_mem_ != nullptr; }

  void send(const std::string &message) {
    if (!is_connected()) {
      throw Exception("Channel is not connected.");
    }

    const size_t max_payload = p_header_->max_payload_size;
    const size_t num_fragments =
        message.empty() ? 1
                        : (message.length() + max_payload - 1) / max_payload;
    const uint32_t capacity = p_header_->capacity;

    if (num_fragments > capacity) {
      throw Exception(
          "Message is too large to fit in the channel buffer capacity.");
    }

    std::string_view message_view(message);

    while (true) {

      while (p_header_->write_lock.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      const uint64_t write_idx =
          p_header_->write_index.load(std::memory_order_relaxed);

      uint64_t slowest_reader_seq = get_slowest_reader_sequence();

      if ((write_idx - slowest_reader_seq) + num_fragments > capacity) {

        p_header_->write_lock.clear(std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      try {
        const uint64_t seq_gen_at_write_time =
            p_header_->sequence_generator.load(std::memory_order_relaxed);

        for (size_t i = 0; i < num_fragments; ++i) {
          uint64_t current_write_slot_idx = (write_idx + i) % capacity;
          detail::MessageSlot *slot = get_message_slot(current_write_slot_idx);

          size_t offset = i * max_payload;
          size_t chunk_size =
              message.empty()
                  ? 0
                  : std::min(message_view.length() - offset, max_payload);
          uint64_t new_sequence = seq_gen_at_write_time + i + 1;

          slot->header.data_size = static_cast<uint32_t>(chunk_size);
          slot->header.is_first_fragment = (i == 0);
          slot->header.is_last_fragment = (i == num_fragments - 1);
          if (chunk_size > 0) {
            memcpy(&slot->data, message_view.data() + offset, chunk_size);
          }

          slot->header.sequence.store(new_sequence, std::memory_order_release);
        }

        p_header_->write_index.fetch_add(num_fragments,
                                         std::memory_order_relaxed);
        p_header_->sequence_generator.fetch_add(num_fragments,
                                                std::memory_order_relaxed);

      } catch (...) {
        p_header_->write_lock.clear(std::memory_order_release);
        throw;
      }

      p_header_->write_lock.clear(std::memory_order_release);
      ::SetEvent(h_data_ready_event_);
      return;
    }
  }

  void receive(std::string &message) {
    if (!internal_try_receive(message, INFINITE)) {
      throw Exception(
          "receive failed. The wait handle may be invalid or closed.");
    }
  }

  bool try_receive(std::string &message) {
    return internal_try_receive(message, 0);
  }

private:
  bool internal_try_receive(std::string &message, DWORD timeout_ms) {
    if (!is_connected())
      throw Exception("Channel is not connected.");
    if (my_reader_slot_index_ < 0)
      throw Exception("Reader is not registered.");

    std::string reassembly_buffer;
    bool in_reassembly = false;

    while (true) {

      while (true) {
        uint64_t next_expected_seq = local_read_sequence_ + 1;
        detail::MessageSlot *found_slot = nullptr;

        uint64_t probable_idx =
            (p_header_->write_index.load(std::memory_order_relaxed) - 1) %
            p_header_->capacity;
        probable_idx = (local_read_sequence_) % p_header_->capacity;

        detail::MessageSlot *candidate_slot = get_message_slot(probable_idx);
        if (candidate_slot->header.sequence.load(std::memory_order_acquire) ==
            next_expected_seq) {
          found_slot = candidate_slot;
        } else {

          for (uint32_t i = 0; i < p_header_->capacity; ++i) {
            detail::MessageSlot *current_slot = get_message_slot(i);
            uint64_t slot_sequence =
                current_slot->header.sequence.load(std::memory_order_relaxed);
            if (slot_sequence == next_expected_seq) {
              found_slot = current_slot;
              break;
            }
          }
        }

        if (found_slot) {

          if (found_slot->header.is_first_fragment) {
            if (in_reassembly)
              reassembly_buffer.clear();
            in_reassembly = true;
          } else if (!in_reassembly) {
            local_read_sequence_++;
            update_reader_progress();
            continue;
          }

          reassembly_buffer.append(found_slot->data,
                                   found_slot->header.data_size);
          local_read_sequence_++;

          if (found_slot->header.is_last_fragment) {
            message = std::move(reassembly_buffer);
            in_reassembly = false;
            update_reader_progress();
            return true;
          }
        } else {

          break;
        }
      }

      ::ResetEvent(h_data_ready_event_);

      if (p_header_->sequence_generator.load(std::memory_order_acquire) >
          local_read_sequence_) {
        continue;
      }

      DWORD wait_result =
          ::WaitForSingleObject(h_data_ready_event_, timeout_ms);
      if (wait_result == WAIT_OBJECT_0) {
        continue;
      } else if (wait_result == WAIT_TIMEOUT) {
        return false;
      } else {
        detail::throw_windows_error(
            "WaitForSingleObject failed on ipc::Channel '" + name_ + "'");
      }
    }
  }

  static constexpr size_t get_header_size() {
    return sizeof(detail::SharedMemoryHeader) +
           MaxReaders * sizeof(detail::ReaderSlot);
  }

  detail::ReaderSlot *get_reader_slot(size_t index) const {
    char *base = static_cast<char *>(p_shared_mem_);
    return reinterpret_cast<detail::ReaderSlot *>(
        base + sizeof(detail::SharedMemoryHeader) +
        index * sizeof(detail::ReaderSlot));
  }

  detail::MessageSlot *get_message_slot(uint64_t index) const {
    const size_t slot_size =
        sizeof(detail::MessageSlot) + config_.max_message_payload_size;
    return reinterpret_cast<detail::MessageSlot *>(p_buffer_start_ +
                                                   index * slot_size);
  }

  inline static std::atomic_int instance_count;
  int current_instance_index_ = instance_count++;
  int current_instance_id_ =
      (::GetCurrentProcessId() << 16) + current_instance_index_;

  static bool is_pid_alive(DWORD pid) {
    HANDLE h_process = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (h_process == NULL) {
      return false;
    }
    ::CloseHandle(h_process);
    return true;
  }

  void register_reader() {
    uint64_t current_global_seq =
        p_header_->sequence_generator.load(std::memory_order_relaxed);

    for (size_t i = 0; i < MaxReaders; ++i) {
      detail::ReaderSlot *slot = get_reader_slot(i);
      DWORD expected_pid = 0;

      if (slot->pid.compare_exchange_strong(expected_pid, current_instance_id_,
                                            std::memory_order_acq_rel)) {
        my_reader_slot_index_ = static_cast<int>(i);

        local_read_sequence_ = current_global_seq;
        slot->sequence.store(current_global_seq, std::memory_order_release);
        return;
      }
    }

    disconnect();
    throw Exception("Failed to register reader: Maximum number of concurrent "
                    "readers reached for channel '" +
                    name_ + "'.");
  }

  void deregister_reader() {
    if (my_reader_slot_index_ >= 0) {
      detail::ReaderSlot *slot = get_reader_slot(my_reader_slot_index_);
      slot->pid.store(0, std::memory_order_release);
      my_reader_slot_index_ = -1;
    }
  }

  void update_reader_progress() {
    if (my_reader_slot_index_ >= 0) {
      get_reader_slot(my_reader_slot_index_)
          ->sequence.store(local_read_sequence_, std::memory_order_release);
    }

    uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
    if (now - last_clean_time_ > clean_interval_ms) {
      clean_dead_readers();
      last_clean_time_ = now;
    }
  }

  uint64_t get_slowest_reader_sequence() const {
    uint64_t slowest_seq =
        p_header_->write_index.load(std::memory_order_relaxed);
    bool reader_found = false;

    for (size_t i = 0; i < MaxReaders; ++i) {
      const detail::ReaderSlot *slot = get_reader_slot(i);
      auto pid = slot->pid.load(std::memory_order_acquire);
      if (pid != 0 && pid != current_instance_id_) {
        uint64_t reader_seq = slot->sequence.load(std::memory_order_acquire);
        if (!reader_found) {
          slowest_seq = reader_seq;
          reader_found = true;
        } else {
          slowest_seq = std::min(slowest_seq, reader_seq);
        }
      }
    }

    return slowest_seq;
  }

  void clean_dead_readers() {
    for (size_t i = 0; i < MaxReaders; ++i) {
      detail::ReaderSlot *slot = get_reader_slot(i);
      DWORD pid = slot->pid.load(std::memory_order_acquire);
      if (pid != 0 && !is_pid_alive(pid)) {

        slot->pid.store(0, std::memory_order_release);
        slot->sequence.store(0, std::memory_order_release);
      }
    }
  }

  uint64_t last_clean_time_ = 0;
  static constexpr uint64_t clean_interval_ms = 100;
  Config config_;
  std::string name_;
  HANDLE h_map_file_ = NULL;
  HANDLE h_data_ready_event_ = NULL;
  void *p_shared_mem_ = nullptr;
  detail::SharedMemoryHeader *p_header_ = nullptr;
  char *p_buffer_start_ = nullptr;

  uint64_t local_read_sequence_ = 0;
  int my_reader_slot_index_ = -1;
};

} // namespace ipc

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
