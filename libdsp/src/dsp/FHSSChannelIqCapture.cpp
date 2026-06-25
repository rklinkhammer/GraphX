#include "dsp/fhss/FHSSChannelIqCapture.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace dsp::fhss {
namespace {

std::string CaptureStem(const FHSSChannelizedIqPacket &packet) {
  std::ostringstream stream;
  stream << "channel_" << std::setw(2) << std::setfill('0')
         << packet.channel.channel_id << "_frequency_" << std::setw(2)
         << packet.channel.frequency_index;
  return stream.str();
}

bool Fail(std::string *error_message, std::string message) {
  if (error_message != nullptr) {
    *error_message = std::move(message);
  }
  return false;
}

void WriteFloatLittleEndian(std::ofstream &output, float value) {
  auto bits = std::bit_cast<std::uint32_t>(value);
  if constexpr (std::endian::native == std::endian::big) {
    bits = std::byteswap(bits);
  }
  output.write(reinterpret_cast<const char *>(&bits), sizeof(bits));
}

} // namespace

bool FHSSShouldCaptureChannel(const FHSSChannelIqCaptureConfig &config,
                              std::uint32_t frequency_index) {
  return config.enabled &&
         (config.frequency_indices.empty() ||
          std::ranges::find(config.frequency_indices, frequency_index) !=
              config.frequency_indices.end());
}

bool WriteFHSSChannelIqSigMf(const FHSSChannelizedIqPacket &packet,
                             const FHSSChannelIqCaptureConfig &config,
                             std::string *error_message) {
  if (!FHSSShouldCaptureChannel(config, packet.channel.frequency_index)) {
    return true;
  }
  if (config.output_directory.empty()) {
    return Fail(error_message,
                "channel IQ capture output directory must not be empty");
  }
  if (!FHSSGraphXEvidenceHasHostComplexIq(packet.iq)) {
    return Fail(error_message,
                "channel IQ capture requires host-resident complex samples");
  }

  std::error_code filesystem_error;
  std::filesystem::create_directories(config.output_directory,
                                      filesystem_error);
  if (filesystem_error) {
    return Fail(error_message,
                "failed to create channel IQ capture directory: " +
                    filesystem_error.message());
  }

  const auto stem = CaptureStem(packet);
  const auto data_path = config.output_directory / (stem + ".sigmf-data");
  const auto metadata_path =
      config.output_directory / (stem + ".sigmf-meta");
  if (!config.overwrite &&
      (std::filesystem::exists(data_path) ||
       std::filesystem::exists(metadata_path))) {
    return Fail(error_message, "channel IQ capture already exists: " + stem);
  }

  std::ofstream data(data_path, std::ios::binary | std::ios::trunc);
  if (!data.good()) {
    return Fail(error_message,
                "failed to open channel IQ data file: " + data_path.string());
  }

  const auto &samples = *packet.iq.host_complex64_samples;
  const auto begin = packet.iq.sample_offset;
  const auto end = begin + packet.iq.sample_count;
  for (std::uint64_t index = begin; index < end; ++index) {
    const auto real = static_cast<float>(samples[index].real());
    const auto imag = static_cast<float>(samples[index].imag());
    WriteFloatLittleEndian(data, real);
    WriteFloatLittleEndian(data, imag);
  }
  if (!data.good()) {
    return Fail(error_message,
                "failed while writing channel IQ data file: " +
                    data_path.string());
  }

  const nlohmann::json metadata{
      {"global",
       {{"core:datatype", "cf32_le"},
        {"core:sample_rate", packet.channel.channel_sample_rate_hz},
        {"core:version", "1.2.0"},
        {"core:description",
         "GraphX FHSS deterministic channelizer output"},
        {"graphx:channel_id", packet.channel.channel_id},
        {"graphx:frequency_index", packet.channel.frequency_index},
        {"graphx:rf_frequency_hz", packet.channel.rf_frequency_hz},
        {"graphx:iq_offset_frequency_hz",
         packet.channel.iq_offset_frequency_hz},
        {"graphx:decimation_factor", packet.channel.decimation_factor},
        {"graphx:filter_group_delay_input_samples",
         packet.channel.filter_group_delay_input_samples},
        {"graphx:input_global_start_sample",
         packet.channel.input_global_start_sample},
        {"graphx:channel_global_start_sample",
         packet.channel.channel_global_start_sample}}},
      {"captures",
       nlohmann::json::array(
           {{{"core:sample_start", 0},
             {"core:frequency", packet.channel.rf_frequency_hz}}})},
      {"annotations", nlohmann::json::array()}};

  std::ofstream metadata_output(metadata_path, std::ios::trunc);
  if (!metadata_output.good()) {
    return Fail(error_message, "failed to open channel IQ metadata file: " +
                                   metadata_path.string());
  }
  metadata_output << std::setw(2) << metadata << '\n';
  return metadata_output.good() ||
         Fail(error_message, "failed while writing channel IQ metadata file: " +
                                 metadata_path.string());
}

} // namespace dsp::fhss
