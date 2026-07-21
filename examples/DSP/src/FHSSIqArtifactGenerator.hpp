// SPDX-License-Identifier: MIT
#pragma once

#include "FHSSHash.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::examples::fhss {

struct IqArtifactBundle {
  dsp::fhss::FHSSSyntheticIqFixture fixture;
  std::vector<std::byte> iq_bytes;
  nlohmann::json truth;
  nlohmann::json sigmf;
  std::string input_sha256;
  std::string iq_sha256;
  std::string iq_sha512;
};

[[nodiscard]] std::vector<std::byte>
EncodeIq(const std::vector<std::complex<double>> &samples,
         std::string_view sample_format);
[[nodiscard]] IqArtifactBundle
GenerateIqArtifacts(const nlohmann::json &input, std::string_view sample_format,
                    std::size_t max_samples, std::size_t max_iq_bytes);
void WriteBytes(const std::filesystem::path &path,
                std::span<const std::byte> bytes);
void WriteJson(const std::filesystem::path &path, const nlohmann::json &json);

} // namespace graphx::examples::fhss
