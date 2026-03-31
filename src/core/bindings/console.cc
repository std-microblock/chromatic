#include "console.h"
#include <chrono>
#include <cstdio>
#include <fmt/color.h>
#include <fmt/core.h>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
thread_local int current_group_depth = 0;
thread_local std::unordered_map<
    std::string, std::chrono::time_point<std::chrono::high_resolution_clock>>
    timers;
thread_local std::unordered_map<std::string, unsigned long long> counters;

std::string get_indent() { return std::string(current_group_depth * 2, ' '); }

void print_styled(bool is_stderr, const std::string &prefix,
                  const std::string &message,
                  fmt::text_style style = fmt::text_style()) {
  auto res = fmt::format(style, "{}{}{}\n", get_indent(), prefix, message);
#ifdef _WIN32
  OutputDebugStringA(res.c_str());
  fmt::print(is_stderr ? stderr : stdout, "{}", res);
#elif defined(__ANDROID__)
  __android_log_write(is_stderr ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO,
                      "chromatic", res.c_str());
#else
  fmt::print(is_stderr ? stderr : stdout, "{}", res);
#endif
}
} // namespace

namespace chromatic::js {

void console::log(const std::string &message) {
  print_styled(false, "", message);
}

void console::error(const std::string &message) {
  print_styled(true, "✖ ", message,
               fmt::fg(fmt::color::red) | fmt::emphasis::bold);
}

void console::warn(const std::string &message) {
  print_styled(true, "⚠ ", message,
               fmt::fg(fmt::color::yellow) | fmt::emphasis::bold);
}

void console::info(const std::string &message) {
  print_styled(false, "ℹ ", message, fmt::fg(fmt::color::cyan));
}

void console::debug(const std::string &message) {
  print_styled(false, "▶ ", message, fmt::fg(fmt::color::gray));
}

void console::trace(const std::string &message) {
  print_styled(false, "Trace: ", message, fmt::fg(fmt::color::magenta));
}

void console::group(const std::string &message) {
  print_styled(false, "▼ ", message, fmt::emphasis::bold);
  current_group_depth++;
}

void console::groupEnd() {
  if (current_group_depth > 0) {
    current_group_depth--;
  }
}

void console::table(const std::string &message) {
  // 简化版：由于签名限制为 string，直接降级为 log
  log(message);
}

void console::time(const std::string &message) {
  const std::string label = message.empty() ? "default" : message;
  if (timers.find(label) != timers.end()) {
    warn("Timer '" + label + "' already exists");
    return;
  }
  timers[label] = std::chrono::high_resolution_clock::now();
}

void console::timeEnd(const std::string &message) {
  const std::string label = message.empty() ? "default" : message;
  auto it = timers.find(label);
  if (it == timers.end()) {
    warn("Timer '" + label + "' does not exist");
    return;
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration<double, std::milli>(end - it->second).count();
  print_styled(false, "", fmt::format("{}: {} ms", label, duration));
  timers.erase(it);
}

void console::timeLog(const std::string &message) {
  const std::string label = message.empty() ? "default" : message;
  auto it = timers.find(label);
  if (it == timers.end()) {
    warn("Timer '" + label + "' does not exist");
    return;
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration<double, std::milli>(end - it->second).count();
  print_styled(false, "", fmt::format("{}: {} ms", label, duration));
}

void console::count(const std::string &message) {
  const std::string label = message.empty() ? "default" : message;
  auto val = ++counters[label];
  print_styled(false, "", fmt::format("{}: {}", label, val));
}

void console::countReset(const std::string &message) {
  const std::string label = message.empty() ? "default" : message;
  if (counters.erase(label) == 0) {
    warn("Count for '" + label + "' does not exist");
  }
}

void console::dir(const std::string &message) { log(message); }

void console::dirxml(const std::string &message) { log(message); }

void console::profile(const std::string &message) {
  print_styled(false, "Profile: ", message + " started",
               fmt::fg(fmt::color::light_green));
}

void console::profileEnd(const std::string &message) {
  print_styled(false, "Profile: ", message + " ended",
               fmt::fg(fmt::color::light_green));
}

void console::timeStamp(const std::string &message) {
  print_styled(false, "TimeStamp: ", message, fmt::fg(fmt::color::light_blue));
}

void console::timeline(const std::string &message) {
  print_styled(false, "Timeline: ", message + " started",
               fmt::fg(fmt::color::light_blue));
}

void console::timelineEnd(const std::string &message) {
  print_styled(false, "Timeline: ", message + " ended",
               fmt::fg(fmt::color::light_blue));
}

void console::timeLine(const std::string &message) { timeline(message); }

void console::timeLineEnd(const std::string &message) { timelineEnd(message); }

} // namespace chromatic::js