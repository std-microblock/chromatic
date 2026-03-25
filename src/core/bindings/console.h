#pragma once
#include <string>

namespace chromatic::js {
struct console {
  static void log(const std::string &message);
  static void error(const std::string &message);
  static void warn(const std::string &message);
  static void info(const std::string &message);
  static void debug(const std::string &message);
  static void trace(const std::string &message);
  static void group(const std::string &message);
  static void groupEnd();
  static void table(const std::string &message);
  static void time(const std::string &message);
  static void timeEnd(const std::string &message);
  static void count(const std::string &message);
  static void countReset(const std::string &message);
  static void dir(const std::string &message);
  static void dirxml(const std::string &message);
  static void profile(const std::string &message);
  static void profileEnd(const std::string &message);
  static void timeStamp(const std::string &message);
  static void timeline(const std::string &message);
  static void timelineEnd(const std::string &message);
  static void timeLog(const std::string &message);
  static void timeLine(const std::string &message);
  static void timeLineEnd(const std::string &message);
};
} // namespace chromatic::js