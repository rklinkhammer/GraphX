#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sar::reference {

struct PointTarget {
    double x_m{};
    double y_m{};
    double reflectivity{1.0};
};

struct Geometry {
    std::uint32_t pulse_count{9};
    std::uint32_t range_bin_count{128};
    std::uint32_t image_width{9};
    std::uint32_t image_height{9};
    double platform_x_start_m{-4.0};
    double platform_x_end_m{4.0};
    double platform_y_m{-20.0};
    double range_origin_m{15.0};
    double range_spacing_m{0.25};
    double scene_center_x_m{0.0};
    double scene_center_y_m{0.0};
    double pixel_spacing_m{0.5};
    double wavelength_m{0.03};
};

struct Image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> pixels{};
};

struct PeakMetric {
    std::uint32_t x{};
    std::uint32_t y{};
    float value{};
};

struct ErrorMetrics {
    double l_inf{};
    double rms{};
    double relative_l2{};
};

inline void ValidateGeometry(const Geometry& geometry) {
    if (geometry.pulse_count == 0 || geometry.range_bin_count == 0 ||
        geometry.image_width == 0 || geometry.image_height == 0) {
        throw std::invalid_argument("SAR reference geometry dimensions must be non-zero");
    }
    if (geometry.range_spacing_m <= 0.0 || geometry.pixel_spacing_m <= 0.0 ||
        geometry.wavelength_m <= 0.0) {
        throw std::invalid_argument("SAR reference geometry spacings and wavelength must be positive");
    }
}

inline double PlatformX(const Geometry& geometry, std::uint32_t pulse) {
    if (geometry.pulse_count == 1) {
        return geometry.platform_x_start_m;
    }
    const double t = static_cast<double>(pulse) / static_cast<double>(geometry.pulse_count - 1u);
    return geometry.platform_x_start_m +
           ((geometry.platform_x_end_m - geometry.platform_x_start_m) * t);
}

inline double PixelX(const Geometry& geometry, std::uint32_t x) {
    const double center = (static_cast<double>(geometry.image_width) - 1.0) * 0.5;
    return geometry.scene_center_x_m + ((static_cast<double>(x) - center) * geometry.pixel_spacing_m);
}

inline double PixelY(const Geometry& geometry, std::uint32_t y) {
    const double center = (static_cast<double>(geometry.image_height) - 1.0) * 0.5;
    return geometry.scene_center_y_m + ((static_cast<double>(y) - center) * geometry.pixel_spacing_m);
}

inline double SlantRange(double platform_x_m,
                         double platform_y_m,
                         double target_x_m,
                         double target_y_m) {
    const double dx = target_x_m - platform_x_m;
    const double dy = target_y_m - platform_y_m;
    return std::sqrt((dx * dx) + (dy * dy));
}

inline std::vector<std::complex<double>> GeneratePointTargetPhaseHistory(
    const Geometry& geometry,
    const PointTarget& target) {
    ValidateGeometry(geometry);

    std::vector<std::complex<double>> phase_history(
        static_cast<std::size_t>(geometry.pulse_count) * geometry.range_bin_count,
        std::complex<double>{0.0, 0.0});

    constexpr double kPi = 3.141592653589793238462643383279502884;
    for (std::uint32_t pulse = 0; pulse < geometry.pulse_count; ++pulse) {
        const double platform_x = PlatformX(geometry, pulse);
        const double range = SlantRange(platform_x, geometry.platform_y_m, target.x_m, target.y_m);
        const auto range_bin = static_cast<long long>(
            std::llround((range - geometry.range_origin_m) / geometry.range_spacing_m));
        if (range_bin < 0 ||
            range_bin >= static_cast<long long>(geometry.range_bin_count)) {
            continue;
        }

        const double phase_rad = -4.0 * kPi * range / geometry.wavelength_m;
        phase_history[(static_cast<std::size_t>(pulse) * geometry.range_bin_count) +
                      static_cast<std::size_t>(range_bin)] +=
            target.reflectivity * std::complex<double>{std::cos(phase_rad), std::sin(phase_rad)};
    }

    return phase_history;
}

inline Image BackprojectNearestRange(const Geometry& geometry,
                                     const std::vector<std::complex<double>>& phase_history) {
    ValidateGeometry(geometry);
    const auto expected_samples =
        static_cast<std::size_t>(geometry.pulse_count) * geometry.range_bin_count;
    if (phase_history.size() != expected_samples) {
        throw std::invalid_argument("SAR phase-history size does not match geometry");
    }

    Image image{};
    image.width = geometry.image_width;
    image.height = geometry.image_height;
    image.pixels.assign(
        static_cast<std::size_t>(geometry.image_width) * geometry.image_height,
        0.0f);

    constexpr double kPi = 3.141592653589793238462643383279502884;
    for (std::uint32_t y = 0; y < geometry.image_height; ++y) {
        const double pixel_y = PixelY(geometry, y);
        for (std::uint32_t x = 0; x < geometry.image_width; ++x) {
            const double pixel_x = PixelX(geometry, x);
            std::complex<double> accum{0.0, 0.0};

            for (std::uint32_t pulse = 0; pulse < geometry.pulse_count; ++pulse) {
                const double platform_x = PlatformX(geometry, pulse);
                const double range =
                    SlantRange(platform_x, geometry.platform_y_m, pixel_x, pixel_y);
                const auto range_bin = static_cast<long long>(
                    std::llround((range - geometry.range_origin_m) / geometry.range_spacing_m));
                if (range_bin < 0 ||
                    range_bin >= static_cast<long long>(geometry.range_bin_count)) {
                    continue;
                }

                const auto index =
                    (static_cast<std::size_t>(pulse) * geometry.range_bin_count) +
                    static_cast<std::size_t>(range_bin);
                const double phase_rad = 4.0 * kPi * range / geometry.wavelength_m;
                const std::complex<double> phase_correction{
                    std::cos(phase_rad),
                    std::sin(phase_rad)};
                accum += phase_history[index] * phase_correction;
            }

            const auto image_index =
                (static_cast<std::size_t>(y) * geometry.image_width) + x;
            image.pixels[image_index] =
                static_cast<float>(std::abs(accum) / static_cast<double>(geometry.pulse_count));
        }
    }

    return image;
}

inline PeakMetric FindPeak(const Image& image) {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        throw std::invalid_argument("SAR image must be non-empty");
    }

    std::size_t peak_index = 0;
    float peak_value = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        if (image.pixels[i] > peak_value) {
            peak_value = image.pixels[i];
            peak_index = i;
        }
    }

    return PeakMetric{
        .x = static_cast<std::uint32_t>(peak_index % image.width),
        .y = static_cast<std::uint32_t>(peak_index / image.width),
        .value = peak_value,
    };
}

inline ErrorMetrics CompareImages(const Image& actual, const Image& expected) {
    if (actual.width != expected.width || actual.height != expected.height ||
        actual.pixels.size() != expected.pixels.size()) {
        throw std::invalid_argument("SAR image dimensions must match for comparison");
    }

    double l_inf = 0.0;
    double sum_sq = 0.0;
    double expected_sum_sq = 0.0;
    for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
        const double diff =
            static_cast<double>(actual.pixels[i]) - static_cast<double>(expected.pixels[i]);
        l_inf = std::max(l_inf, std::abs(diff));
        sum_sq += diff * diff;
        expected_sum_sq += static_cast<double>(expected.pixels[i]) *
                           static_cast<double>(expected.pixels[i]);
    }

    const double count = static_cast<double>(actual.pixels.size());
    return ErrorMetrics{
        .l_inf = l_inf,
        .rms = std::sqrt(sum_sq / count),
        .relative_l2 = expected_sum_sq == 0.0 ? 0.0 : std::sqrt(sum_sq / expected_sum_sq),
    };
}

inline std::uint64_t QuantizedImageHash(const Image& image, double scale = 1.0e6) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(image.width);
    mix(image.height);
    for (float pixel : image.pixels) {
        const auto quantized = static_cast<std::int64_t>(std::llround(
            static_cast<double>(pixel) * scale));
        mix(static_cast<std::uint64_t>(quantized));
    }
    return hash;
}

} // namespace sar::reference
