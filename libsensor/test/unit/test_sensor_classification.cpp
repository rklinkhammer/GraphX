#include "sensor/SensorClassificationType.hpp"

#include <array>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using sensors::SensorClassificationType;

struct ClassificationCase {
  SensorClassificationType type;
  std::string_view canonical;
  std::string_view alias;
};

constexpr std::array kCases{
    ClassificationCase{SensorClassificationType::ACCELEROMETER, "accel",
                       "accelerometer"},
    ClassificationCase{SensorClassificationType::GYROSCOPE, "gyro",
                       "gyroscope"},
    ClassificationCase{SensorClassificationType::MAGNETOMETER, "mag",
                       "magnetometer"},
    ClassificationCase{SensorClassificationType::BAROMETRIC, "baro",
                       "barometric"},
    ClassificationCase{SensorClassificationType::GPS_POSITION, "gps",
                       "gps_position"},
    ClassificationCase{SensorClassificationType::DSP_SENSOR_SINE, "dsp_sine",
                       "sine"},
};

TEST(SensorClassificationTest, CanonicalNamesRoundTrip) {
  for (const auto &[type, canonical, alias] : kCases) {
    EXPECT_EQ(sensors::SensorClassificationTypeToString(type), canonical);
    EXPECT_EQ(sensors::StringToSensorClassificationType(std::string{canonical}),
              type);
    EXPECT_EQ(sensors::StringToSensorClassificationType(std::string{alias}),
              type);
  }
}

TEST(SensorClassificationTest, ParsingIsCaseInsensitive) {
  EXPECT_EQ(sensors::StringToSensorClassificationType("AcCeLeRoMeTeR"),
            SensorClassificationType::ACCELEROMETER);
  EXPECT_EQ(sensors::StringToSensorClassificationType("GPS_POSITION"),
            SensorClassificationType::GPS_POSITION);
  EXPECT_EQ(sensors::StringToSensorClassificationType("DSP_SINE"),
            SensorClassificationType::DSP_SENSOR_SINE);
}

TEST(SensorClassificationTest, UnknownInputsRemainUnknown) {
  for (const std::string_view value :
       {"", " ", "accel ", "not-a-sensor", "123"}) {
    EXPECT_EQ(sensors::StringToSensorClassificationType(std::string{value}),
              SensorClassificationType::UNKNOWN);
  }
  EXPECT_EQ(sensors::SensorClassificationTypeToString(
                SensorClassificationType::UNKNOWN),
            "unknown");
  EXPECT_EQ(sensors::SensorClassificationTypeToString(
                static_cast<SensorClassificationType>(999)),
            "unknown");
}

} // namespace
