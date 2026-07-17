#include "dsp/fhss/FHSSGraphXConfig.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

nlohmann::json MinimalPreambleConfig() {
  nlohmann::json pulses = nlohmann::json::array();
  constexpr std::uint32_t frequencies[]{1, 7, 12, 62};
  constexpr std::uint32_t words[]{0x11111111u, 0x77777777u, 0x12121212u,
                                  0x62626262u};
  for (std::size_t i = 0;
       i < dsp::fhss::FHSSProtocolConstants::kPreamblePulseCount; ++i) {
    pulses.push_back({{"frequency_index", frequencies[i % 4]},
                      {"word_value", words[i % 4]}});
  }
  return {{"preamble_pulses", std::move(pulses)}};
}

TEST(FHSSGraphXConfigTest, ReceiverParsersAcceptOnlyPreamblePulses) {
  const auto json = MinimalPreambleConfig();
  ASSERT_FALSE(json.contains("active_frequency_indices"));
  ASSERT_FALSE(json.contains("messages"));

  const auto preamble =
      dsp::fhss::FHSSPreamblePulseSpecsFromJson(graph::JsonView(json));
  const auto assembler =
      dsp::fhss::FHSSMessageAssemblerConfigFromJson(graph::JsonView(json));

  EXPECT_EQ(preamble.size(), 16u);
  ASSERT_EQ(assembler.preamble_pulses.size(), preamble.size());
  for (std::size_t i = 0; i < preamble.size(); ++i) {
    EXPECT_EQ(assembler.preamble_pulses[i].frequency_index,
              preamble[i].frequency_index);
    EXPECT_EQ(assembler.preamble_pulses[i].word_value, preamble[i].word_value);
  }
  EXPECT_EQ(dsp::fhss::DeriveActiveFrequenciesFromPreamble(preamble),
            (std::vector<std::uint32_t>{1, 7, 12, 62}));
}

TEST(FHSSGraphXConfigTest, ReceiverParsersRejectMissingPreamble) {
  const nlohmann::json json = nlohmann::json::object();
  EXPECT_THROW(dsp::fhss::FHSSPreamblePulseSpecsFromJson(graph::JsonView(json)),
               graph::ConfigError);
  EXPECT_THROW(
      dsp::fhss::FHSSMessageAssemblerConfigFromJson(graph::JsonView(json)),
      graph::ConfigError);
}

TEST(FHSSGraphXConfigTest, ReceiverParsersRejectMalformedPreamble) {
  const std::vector<nlohmann::json> invalid{
      {{"preamble_pulses", "not-an-array"}},
      {{"preamble_pulses", nlohmann::json::array({17})}},
      {{"preamble_pulses", nlohmann::json::array({{{"frequency_index", 1}}})}},
      {{"preamble_pulses", nlohmann::json::array({{{"frequency_index", "one"},
                                                   {"word_value", 1}}})}}};
  for (const auto &json : invalid) {
    EXPECT_THROW(
        dsp::fhss::FHSSPreamblePulseSpecsFromJson(graph::JsonView(json)),
        graph::ConfigError);
    EXPECT_THROW(
        dsp::fhss::FHSSMessageAssemblerConfigFromJson(graph::JsonView(json)),
        graph::ConfigError);
  }
}

} // namespace
