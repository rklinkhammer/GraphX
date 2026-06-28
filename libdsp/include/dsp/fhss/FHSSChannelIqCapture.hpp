#pragma once

#include "dsp/fhss/FHSSPackets.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dsp::fhss {

struct FHSSChannelIqCaptureConfig {
  bool enabled = false;
  std::filesystem::path output_directory;
  std::vector<std::uint32_t> frequency_indices;
  bool overwrite = true;
};

[[nodiscard]] bool
FHSSShouldCaptureChannel(const FHSSChannelIqCaptureConfig &config,
                         std::uint32_t frequency_index);

[[nodiscard]] bool
WriteFHSSChannelIqSigMf(const FHSSChannelizedIqPacket &packet,
                        const FHSSChannelIqCaptureConfig &config,
                        std::string *error_message = nullptr);

} // namespace dsp::fhss
