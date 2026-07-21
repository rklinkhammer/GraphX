// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace graphx::examples::fhss {

class Sha256Hasher {
public:
  void Update(std::span<const std::byte> bytes);
  [[nodiscard]] std::string Finish();

private:
  void Transform(const std::array<std::uint8_t, 64> &block);
  std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u,
                                      0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu,
                                      0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_size_ = 0;
  std::uint64_t bit_count_ = 0;
  bool finished_ = false;
};

class Sha512Hasher {
public:
  void Update(std::span<const std::byte> bytes);
  [[nodiscard]] std::string Finish();

private:
  void Transform(const std::array<std::uint8_t, 128> &block);
  std::array<std::uint64_t, 8> state_{
      0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
      0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
      0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
      0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  std::array<std::uint8_t, 128> buffer_{};
  std::size_t buffer_size_ = 0;
  std::uint64_t bit_count_low_ = 0;
  std::uint64_t bit_count_high_ = 0;
  bool finished_ = false;
};

[[nodiscard]] std::string Sha256(std::span<const std::byte> bytes);
[[nodiscard]] std::string Sha512(std::span<const std::byte> bytes);

} // namespace graphx::examples::fhss
