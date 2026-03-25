#pragma once
#include <string>

namespace chromatic::js {
struct console {
  void log(const std::string &message);
  void error(const std::string &message);
  void warn(const std::string &message);
  void info(const std::string &message);
  void debug(const std::string &message);
  void trace(const std::string &message);
  void group(const std::string &message);
  void groupEnd();
  void table(const std::string &message);
  void time(const std::string &message);
  void timeEnd(const std::string &message);
  void count(const std::string &message);
  void countReset(const std::string &message);
  void dir(const std::string &message);
  void dirxml(const std::string &message);
  void profile(const std::string &message);
  void profileEnd(const std::string &message);
  void timeStamp(const std::string &message);
  void timeline(const std::string &message);
  void timelineEnd(const std::string &message);
  void timeLog(const std::string &message);
  void timeLine(const std::string &message);
  void timeLineEnd(const std::string &message);
};
} // namespace chromatic::js