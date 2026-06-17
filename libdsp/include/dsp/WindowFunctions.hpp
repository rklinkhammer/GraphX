// SPDX-License-Identifier: MIT

/**
 * @file WindowFunctions.hpp
 * @brief GraphX source file.
 */

#pragma once

#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <optional>

namespace dsp {

/**
 * @brief Window function types for FFT processing
 *
 * Window functions reduce spectral leakage by tapering the signal at edges.
 * Each has different trade-offs between main lobe width and sidelobe attenuation.
 */
enum class WindowType {
    RECTANGULAR,   ///< No windowing (box car) - sharp main lobe, high sidelobes
    HANN,          ///< Cosine squared - balanced (default)
    HAMMING,       ///< Optimized cosine - slightly better sidelobes than Hann
    BLACKMAN,      ///< Triple cosine - excellent sidelobe suppression, wider main lobe
};

/**
 * @brief Generate window function coefficients
 *
 * Static helper class for generating window function coefficients.
 * Supports Rectangular, Hann, Hamming, and Blackman windows.
 *
 * Usage:
 * ```cpp
 * auto hann_window = WindowFunctions::Generate<float>(WindowType::HANN, 256);
 * ```
 */
/**
 * @class WindowFunctions
 * @brief WindowFunctions class.
 */
/**
 * @class WindowFunctions
 * @brief Window functions implementation for GraphX.
 */
class WindowFunctions {
public:
    /**
     * @brief Generate window coefficients for a given type and size
     *
     * @tparam SampleT Sample data type (float or double)
     * @param window_type Type of window to generate
     * @param N Window size (number of coefficients)
     * @return Vector of N window coefficients (normalized)
     */
    template<typename SampleT>
    static std::vector<SampleT> Generate(WindowType window_type, size_t N) {
        switch (window_type) {
            case WindowType::RECTANGULAR:
                return GenerateRectangular<SampleT>(N);
            case WindowType::HANN:
                return GenerateHann<SampleT>(N);
            case WindowType::HAMMING:
                return GenerateHamming<SampleT>(N);
            case WindowType::BLACKMAN:
                return GenerateBlackman<SampleT>(N);
        }
        return {};  // Should not reach
    }

    /**
     * @brief Generate window coefficients for fixed-size array
     *
     * @tparam SampleT Sample data type (float or double)
     * @tparam N Window/array size
     * @param window_type Type of window to generate
     * @return Array of N window coefficients
     */
    template<typename SampleT, size_t N>
    static std::array<SampleT, N> GenerateArray(WindowType window_type) {
        auto vec = Generate<SampleT>(window_type, N);
        std::array<SampleT, N> result;
        std::copy(vec.begin(), vec.end(), result.begin());
        return result;
    }

    /**
     * @brief Parse window type from string
     * @param name Window name ("rectangular", "hann", "hamming", "blackman")
     * @return WindowType, or std::nullopt if name unrecognized
     */
    static std::optional<WindowType> Parse(const std::string& name) {
        if (name == "rectangular") return WindowType::RECTANGULAR;
        if (name == "hann") return WindowType::HANN;
        if (name == "hamming") return WindowType::HAMMING;
        if (name == "blackman") return WindowType::BLACKMAN;
        return std::nullopt;
    }

    /**
     * @brief Get string name of window type
     * @param window_type Window type
     * @return Human-readable name
     */
    static std::string ToString(WindowType window_type) {
        switch (window_type) {
            case WindowType::RECTANGULAR:
                return "rectangular";
            case WindowType::HANN:
                return "hann";
            case WindowType::HAMMING:
                return "hamming";
            case WindowType::BLACKMAN:
                return "blackman";
        }
        return "unknown";
    }

    /**
     * @brief Get scallop loss in dB for a window type
     *
     * Scallop loss is the maximum loss when a tone falls between bin centers.
     * Smaller values are better (less loss).
     *
     * @param window_type Window type
     * @return Scallop loss in dB
     */
    static double GetScallopLoss_dB(WindowType window_type) {
        switch (window_type) {
            case WindowType::RECTANGULAR:
                return -3.92;   // Highest loss
            case WindowType::HANN:
                return -1.42;   // Balanced
            case WindowType::HAMMING:
                return -1.30;   // Slightly better than Hann
            case WindowType::BLACKMAN:
                return -0.60;   // Best scallop loss, but wider main lobe
        }
        return 0.0;
    }

    /**
     * @brief Get main lobe width in bins for a window type
     *
     * Main lobe width is measured at -3dB (half power points).
     * Wider main lobe reduces frequency resolution.
     *
     * @param window_type Window type
     * @return Main lobe width in bins
     */
    static double GetMainLobeWidth_bins(WindowType window_type) {
        switch (window_type) {
            case WindowType::RECTANGULAR:
                return 1.2;    // Narrowest main lobe
            case WindowType::HANN:
                return 4.0;    // Wider than rectangular
            case WindowType::HAMMING:
                return 4.0;    // Similar to Hann
            case WindowType::BLACKMAN:
                return 6.0;    // Widest main lobe, best sidelobes
        }
        return 0.0;
    }

    /**
     * @brief Get first sidelobe attenuation for a window type
     *
     * First sidelobe level relative to main lobe peak.
     * Higher (more negative) values are better.
     *
     * @param window_type Window type
     * @return First sidelobe level in dB
     */
    static double GetFirstSidelobe_dB(WindowType window_type) {
        switch (window_type) {
            case WindowType::RECTANGULAR:
                return -13.0;   // Worst sidelobes
            case WindowType::HANN:
                return -32.0;   // Good sidelobe suppression
            case WindowType::HAMMING:
                return -43.0;   // Better sidelobe suppression
            case WindowType::BLACKMAN:
                return -58.0;   // Excellent sidelobe suppression
        }
        return 0.0;
    }

private:
    static constexpr double PI = 3.141592653589793;

    /**
     * @brief No windowing (all ones)
     */
    template<typename SampleT>
    static std::vector<SampleT> GenerateRectangular(size_t N) {
        return std::vector<SampleT>(N, SampleT{1});
    }

    /**
     * @brief Hann window: w[n] = 0.5 * (1 - cos(2π*n/(N-1)))
     */
    template<typename SampleT>
    static std::vector<SampleT> GenerateHann(size_t N) {
/**
 * @brief Window.
 * @param N Parameter for window.
 * @return Result of the operation.
 */
        std::vector<SampleT> window(N);
        if (N == 1) {
            window[0] = SampleT{1};
            return window;
        }
        for (size_t n = 0; n < N; ++n) {
            SampleT factor = static_cast<SampleT>(2 * PI * n) / static_cast<SampleT>(N - 1);
            window[n] = SampleT{0.5} * (SampleT{1} - std::cos(factor));
        }
        return window;
    }

    /**
     * @brief Hamming window: w[n] = 0.54 - 0.46*cos(2π*n/(N-1))
     */
    template<typename SampleT>
    static std::vector<SampleT> GenerateHamming(size_t N) {
/**
 * @brief Window.
 * @param N Parameter for window.
 * @return Result of the operation.
 */
        std::vector<SampleT> window(N);
        if (N == 1) {
            window[0] = SampleT{0.54};
            return window;
        }
        for (size_t n = 0; n < N; ++n) {
            SampleT factor = static_cast<SampleT>(2 * PI * n) / static_cast<SampleT>(N - 1);
            window[n] = SampleT{0.54} - SampleT{0.46} * std::cos(factor);
        }
        return window;
    }

    /**
     * @brief Blackman window: w[n] = 0.42 - 0.5*cos(2π*n/(N-1)) + 0.08*cos(4π*n/(N-1))
     */
    template<typename SampleT>
    static std::vector<SampleT> GenerateBlackman(size_t N) {
/**
 * @brief Window.
 * @param N Parameter for window.
 * @return Result of the operation.
 */
        std::vector<SampleT> window(N);
        if (N == 1) {
            window[0] = SampleT{0.42};
            return window;
        }
        for (size_t n = 0; n < N; ++n) {
            SampleT factor1 = static_cast<SampleT>(2 * PI * n) / static_cast<SampleT>(N - 1);
            SampleT factor2 = 2 * factor1;
            window[n] = SampleT{0.42} - SampleT{0.5} * std::cos(factor1) +
                        SampleT{0.08} * std::cos(factor2);
        }
        return window;
    }
};

}  // namespace dsp
