// SPDX-License-Identifier: MIT
#include "FHSSHash.hpp"

#include <bit>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace graphx::examples::fhss {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256Rounds{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u,
    0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::array<std::uint64_t, 80> kSha512Rounds{
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

template <typename Word>
std::string Hex(const std::array<Word, 8> &words) {
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto word : words)
    result << std::setw(sizeof(Word) * 2) << word;
  return result.str();
}

} // namespace

void Sha256Hasher::Update(std::span<const std::byte> bytes) {
  if (finished_)
    throw std::logic_error("SHA-256 hasher already finalized");
  if (bytes.size() > (UINT64_MAX - bit_count_) / 8u)
    throw std::overflow_error("SHA-256 input length overflow");
  bit_count_ += static_cast<std::uint64_t>(bytes.size()) * 8u;
  for (const auto byte : bytes) {
    buffer_[buffer_size_++] = std::to_integer<std::uint8_t>(byte);
    if (buffer_size_ == buffer_.size()) {
      Transform(buffer_);
      buffer_size_ = 0;
    }
  }
}

void Sha256Hasher::Transform(const std::array<std::uint8_t, 64> &block) {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t i = 0; i < 16; ++i)
    words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24u) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16u) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8u) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
  for (std::size_t i = 16; i < words.size(); ++i) {
    const auto s0 = std::rotr(words[i - 15], 7) ^
                    std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3u);
    const auto s1 = std::rotr(words[i - 2], 17) ^
                    std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10u);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }
  auto [a, b, c, d, e, f, g, h] = state_;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const auto choose = (e & f) ^ ((~e) & g);
    const auto temp1 = h + s1 + choose + kSha256Rounds[i] + words[i];
    const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temp2 = s0 + majority;
    h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a;
    a = temp1 + temp2;
  }
  state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
  state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

std::string Sha256Hasher::Finish() {
  if (finished_)
    throw std::logic_error("SHA-256 hasher already finalized");
  finished_ = true;
  const auto message_bits = bit_count_;
  buffer_[buffer_size_++] = 0x80u;
  if (buffer_size_ > 56u) {
    while (buffer_size_ < buffer_.size()) buffer_[buffer_size_++] = 0u;
    Transform(buffer_);
    buffer_size_ = 0;
  }
  while (buffer_size_ < 56u) buffer_[buffer_size_++] = 0u;
  for (int shift = 56; shift >= 0; shift -= 8)
    buffer_[buffer_size_++] = static_cast<std::uint8_t>(message_bits >> shift);
  Transform(buffer_);
  return Hex(state_);
}

void Sha512Hasher::Update(std::span<const std::byte> bytes) {
  if (finished_)
    throw std::logic_error("SHA-512 hasher already finalized");
  const auto add_low = static_cast<std::uint64_t>(bytes.size()) << 3u;
  const auto add_high = static_cast<std::uint64_t>(bytes.size()) >> 61u;
  const auto previous = bit_count_low_;
  bit_count_low_ += add_low;
  bit_count_high_ += add_high + (bit_count_low_ < previous ? 1u : 0u);
  for (const auto byte : bytes) {
    buffer_[buffer_size_++] = std::to_integer<std::uint8_t>(byte);
    if (buffer_size_ == buffer_.size()) {
      Transform(buffer_);
      buffer_size_ = 0;
    }
  }
}

void Sha512Hasher::Transform(const std::array<std::uint8_t, 128> &block) {
  std::array<std::uint64_t, 80> words{};
  for (std::size_t i = 0; i < 16; ++i) {
    for (std::size_t j = 0; j < 8; ++j)
      words[i] = (words[i] << 8u) | block[i * 8 + j];
  }
  for (std::size_t i = 16; i < words.size(); ++i) {
    const auto s0 = std::rotr(words[i - 15], 1) ^
                    std::rotr(words[i - 15], 8) ^ (words[i - 15] >> 7u);
    const auto s1 = std::rotr(words[i - 2], 19) ^
                    std::rotr(words[i - 2], 61) ^ (words[i - 2] >> 6u);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }
  auto [a, b, c, d, e, f, g, h] = state_;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const auto s1 = std::rotr(e, 14) ^ std::rotr(e, 18) ^ std::rotr(e, 41);
    const auto choose = (e & f) ^ ((~e) & g);
    const auto temp1 = h + s1 + choose + kSha512Rounds[i] + words[i];
    const auto s0 = std::rotr(a, 28) ^ std::rotr(a, 34) ^ std::rotr(a, 39);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temp2 = s0 + majority;
    h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a;
    a = temp1 + temp2;
  }
  state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
  state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

std::string Sha512Hasher::Finish() {
  if (finished_)
    throw std::logic_error("SHA-512 hasher already finalized");
  finished_ = true;
  const auto high = bit_count_high_;
  const auto low = bit_count_low_;
  buffer_[buffer_size_++] = 0x80u;
  if (buffer_size_ > 112u) {
    while (buffer_size_ < buffer_.size()) buffer_[buffer_size_++] = 0u;
    Transform(buffer_);
    buffer_size_ = 0;
  }
  while (buffer_size_ < 112u) buffer_[buffer_size_++] = 0u;
  for (int shift = 56; shift >= 0; shift -= 8)
    buffer_[buffer_size_++] = static_cast<std::uint8_t>(high >> shift);
  for (int shift = 56; shift >= 0; shift -= 8)
    buffer_[buffer_size_++] = static_cast<std::uint8_t>(low >> shift);
  Transform(buffer_);
  return Hex(state_);
}

std::string Sha256(std::span<const std::byte> bytes) {
  Sha256Hasher hasher;
  hasher.Update(bytes);
  return hasher.Finish();
}

std::string Sha512(std::span<const std::byte> bytes) {
  Sha512Hasher hasher;
  hasher.Update(bytes);
  return hasher.Finish();
}

} // namespace graphx::examples::fhss
