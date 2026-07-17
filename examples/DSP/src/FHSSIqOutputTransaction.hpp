// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace graphx::examples::fhss {

using OutputFileTransaction =
    std::pair<std::filesystem::path, std::filesystem::path>;

[[nodiscard]] inline std::filesystem::path
BackupPath(const std::filesystem::path &target) {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return target.string() + ".backup." + std::to_string(nonce);
}

inline void
CommitOutputFiles(const std::vector<OutputFileTransaction> &files, bool force,
                  const std::function<void(std::size_t)> &after_commit = {}) {
  std::vector<OutputFileTransaction> backups;
  std::vector<std::filesystem::path> committed;
  try {
    if (force) {
      for (const auto &file : files) {
        const auto &target = file.second;
        if (std::filesystem::exists(target)) {
          if (!std::filesystem::is_regular_file(target)) {
            throw std::runtime_error(
                "io: existing output is not a regular file: " +
                target.string());
          }
          const auto backup = BackupPath(target);
          std::filesystem::rename(target, backup);
          backups.emplace_back(backup, target);
        }
      }
    }
    for (std::size_t index = 0; index < files.size(); ++index) {
      const auto &[temporary, target] = files[index];
      std::filesystem::rename(temporary, target);
      committed.push_back(target);
      if (after_commit)
        after_commit(index);
    }
    for (const auto &backup_file : backups) {
      std::error_code ignored;
      std::filesystem::remove(backup_file.first, ignored);
    }
  } catch (...) {
    for (const auto &target : committed) {
      std::error_code ignored;
      std::filesystem::remove(target, ignored);
    }
    for (const auto &[backup, target] : backups) {
      std::error_code ignored;
      std::filesystem::rename(backup, target, ignored);
    }
    for (const auto &file : files) {
      std::error_code ignored;
      std::filesystem::remove(file.first, ignored);
    }
    throw;
  }
}

} // namespace graphx::examples::fhss
