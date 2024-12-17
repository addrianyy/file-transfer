#pragma once
#include <span>
#include <string>
#include <vector>

class FileListing {
 public:
  enum class Type {
    File,
    Directory,
  };
  struct Entry {
    Type type{};
    std::string relative_path;
    std::string absolute_path;
  };

 private:
  std::vector<Entry> entries_;

 public:
  void add(const std::string& path);

  std::vector<Entry> finalize() { return std::move(entries_); }
};