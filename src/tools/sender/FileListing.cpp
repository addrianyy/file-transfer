#include "FileListing.hpp"

#include <base/Panic.hpp>

#include <filesystem>

static void process_file(const std::string& relative_path,
                         const std::string& absolute_path,
                         std::vector<FileListing::Entry>& entries) {
  verify(std::filesystem::exists(absolute_path), "path `{}` does not exist", absolute_path);

  const auto is_directory = std::filesystem::is_directory(absolute_path);

  entries.push_back(FileListing::Entry{
    .type = is_directory ? FileListing::Type::Directory : FileListing::Type::File,
    .relative_path = relative_path,
    .absolute_path = absolute_path,
  });

  if (is_directory) {
    for (const auto& sub_entry : std::filesystem::directory_iterator(absolute_path)) {
      const auto filename = sub_entry.path().filename().string();
      process_file(relative_path + std::string("/") + filename, sub_entry.path().string(), entries);
    }
  }
}

void FileListing::add(const std::string& path) {
  verify(std::filesystem::exists(path), "path `{}` does not exist", path);

  const auto full_path = std::filesystem::canonical(path);
  process_file(std::filesystem::path(full_path).filename().string(), full_path, entries_);
}