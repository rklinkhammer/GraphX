#pragma once

#include "sar/io/NormalizedSarProduct.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
#include <hdf5.h>
#endif

namespace graphx::sar {

// Per-pulse data extracted from a GOTCHA MAT v7.3 (HDF5) file.
struct GotchaHdf5PulseData {
    std::vector<ComplexSample> samples{};
    std::array<double, 3> antenna_position_m{0.0, 0.0, 0.0};
    double reference_range_m{0.0};
    double pulse_time{0.0};
};

// Waveform metadata extracted from a GOTCHA MAT v7.3 (HDF5) file.
struct GotchaHdf5WaveformData {
    double carrier_hz{0.0};
    double bandwidth_hz{0.0};
    double sample_rate_hz{0.0};
    double min_f{0.0};
    double delta_f{0.0};
    int k_samples{0};
};

// Result of reading all pulses from a single GOTCHA MAT v7.3 (HDF5) file.
struct GotchaHdf5ReadResult {
    bool success{false};
    std::string message{"not_attempted"};
    GotchaHdf5WaveformData waveform{};
    std::vector<GotchaHdf5PulseData> pulses{};
};

// Reads the full phase-history array (phdata) and associated per-pulse metadata
// directly from a GOTCHA MAT v7.3 (HDF5) file, bypassing the sidecar JSON.
//
// The GOTCHA MAT v7.3 format stores the subData struct as an HDF5 group at /subData.
// Within that group:
//   phdata  – complex double [K × Np] in MATLAB (column-major) → [Np × K] in HDF5 (row-major)
//   AntX, AntY, AntZ – per-pulse antenna positions [Np]
//   R0      – per-pulse range to scene centre [Np]
//   Np      – per-pulse slow-time / pulse numbers [Np]
//   K       – scalar: number of fast-time frequency samples
//   deltaF  – scalar: frequency step (Hz)
//   minF    – scalar: start frequency (Hz)
//
// MATLAB stores complex arrays as a compound HDF5 type. The member names changed
// across MATLAB releases: older versions use "real"/"imag", newer use "r"/"i".
// This reader tries both.
class GotchaHdf5PhdataReader {
public:
    [[nodiscard]] static bool IsAvailable() noexcept {
#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] static GotchaHdf5ReadResult Read(const std::filesystem::path& mat_path) {
#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
        return ReadImpl(mat_path);
#else
        (void)mat_path;
        return GotchaHdf5ReadResult{.message = "hdf5_support_not_compiled"};
#endif
    }

private:
#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5

    // Internal storage for a double-precision complex sample (matches HDF5 compound layout).
    struct Complex128 {
        double real{0.0};
        double imag{0.0};
    };

    // Read the first element of any double-typed dataset as a scalar.
    [[nodiscard]] static double ReadScalar(hid_t group, const char* name) noexcept {
        const hid_t dset = H5Dopen2(group, name, H5P_DEFAULT);
        if (dset < 0) return 0.0;
        double value = 0.0;
        const hid_t space = H5Dget_space(dset);
        if (space >= 0) {
            const hssize_t n = H5Sget_simple_extent_npoints(space);
            if (n > 0) {
                std::vector<double> buf(static_cast<std::size_t>(n), 0.0);
                if (H5Dread(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                            buf.data()) >= 0) {
                    value = buf[0];
                }
            }
            H5Sclose(space);
        }
        H5Dclose(dset);
        return value;
    }

    // Read an entire dataset as a vector of doubles.
    [[nodiscard]] static std::vector<double> ReadDoubleVector(hid_t group, const char* name) {
        std::vector<double> result{};
        const hid_t dset = H5Dopen2(group, name, H5P_DEFAULT);
        if (dset < 0) return result;
        const hid_t space = H5Dget_space(dset);
        if (space >= 0) {
            const hssize_t n = H5Sget_simple_extent_npoints(space);
            if (n > 0) {
                result.resize(static_cast<std::size_t>(n), 0.0);
                if (H5Dread(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                            result.data()) < 0) {
                    result.clear();
                }
            }
            H5Sclose(space);
        }
        H5Dclose(dset);
        return result;
    }

    // Try reading phdata as a compound complex type using the supplied member names.
    // Returns true on success.
    [[nodiscard]] static bool TryReadComplexCompound(
        hid_t dset,
        std::size_t total_elements,
        const char* real_name,
        const char* imag_name,
        std::vector<Complex128>& out) {
        const hid_t mem_type = H5Tcreate(H5T_COMPOUND, sizeof(Complex128));
        if (mem_type < 0) return false;
        H5Tinsert(mem_type, real_name, HOFFSET(Complex128, real), H5T_NATIVE_DOUBLE);
        H5Tinsert(mem_type, imag_name, HOFFSET(Complex128, imag), H5T_NATIVE_DOUBLE);
        out.resize(total_elements);
        const herr_t status =
            H5Dread(dset, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, out.data());
        H5Tclose(mem_type);
        if (status < 0) {
            out.clear();
            return false;
        }
        return true;
    }

    // Read phdata complex values; handles both "real"/"imag" and "r"/"i" member names.
    // Returns an empty string on success, or an error code string on failure.
    [[nodiscard]] static std::string ReadPhdataComplex(
        hid_t group,
        std::size_t np,
        std::size_t k,
        std::vector<Complex128>& data) {
        const hid_t dset = H5Dopen2(group, "phdata", H5P_DEFAULT);
        if (dset < 0) return "phdata_dataset_not_found";

        const std::size_t total = np * k;
        // Older MATLAB (pre-2015b) uses "real"/"imag"; newer uses "r"/"i".
        bool ok = TryReadComplexCompound(dset, total, "real", "imag", data);
        if (!ok) {
            ok = TryReadComplexCompound(dset, total, "r", "i", data);
        }
        H5Dclose(dset);
        if (!ok) return "phdata_compound_read_failed:tried_real_imag_and_r_i";
        return {};
    }

    [[nodiscard]] static GotchaHdf5ReadResult ReadImpl(const std::filesystem::path& mat_path) {
        GotchaHdf5ReadResult result{};

        const hid_t file_id =
            H5Fopen(mat_path.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
        if (file_id < 0) {
            result.message = "hdf5_open_failed:" + mat_path.filename().string();
            return result;
        }

        // GOTCHA MAT v7.3 stores the subData struct as HDF5 group /subData.
        const hid_t group_id = H5Gopen2(file_id, "subData", H5P_DEFAULT);
        if (group_id < 0) {
            H5Fclose(file_id);
            result.message = "hdf5_subData_group_not_found";
            return result;
        }

        // Waveform scalars.
        const double min_f   = ReadScalar(group_id, "minF");
        const double delta_f = ReadScalar(group_id, "deltaF");
        const double k_val   = ReadScalar(group_id, "K");
        const int k_int      = static_cast<int>(std::round(k_val));

        result.waveform.min_f      = min_f;
        result.waveform.delta_f    = delta_f;
        result.waveform.k_samples  = k_int;

        const double bw = delta_f * static_cast<double>(std::max(k_int, 1));
        result.waveform.bandwidth_hz   = std::max(bw, 1.0);
        result.waveform.carrier_hz     = min_f + bw * 0.5;
        result.waveform.sample_rate_hz = std::max(bw, 1.0);

        // Determine phdata dimensions from the HDF5 dataspace.
        // MATLAB column-major [K, Np] is transposed to HDF5 row-major [Np, K].
        std::size_t np = 0;
        std::size_t k  = 0;
        {
            const hid_t dset = H5Dopen2(group_id, "phdata", H5P_DEFAULT);
            if (dset < 0) {
                H5Gclose(group_id);
                H5Fclose(file_id);
                result.message = "phdata_dataset_not_found";
                return result;
            }
            const hid_t space = H5Dget_space(dset);
            const int rank    = H5Sget_simple_extent_ndims(space);
            hsize_t dims[2]   = {1, 1};
            H5Sget_simple_extent_dims(space, dims, nullptr);
            H5Sclose(space);
            H5Dclose(dset);

            if (rank == 1) {
                // Single-pulse file stored as 1-D array [K].
                np = 1;
                k  = static_cast<std::size_t>(dims[0]);
            } else {
                // Multi-pulse: HDF5 dims are [Np, K].
                np = static_cast<std::size_t>(dims[0]);
                k  = static_cast<std::size_t>(dims[1]);
            }
        }

        if (np == 0 || k == 0) {
            H5Gclose(group_id);
            H5Fclose(file_id);
            result.message = "phdata_zero_dimensions";
            return result;
        }

        // Per-pulse geometry/timing vectors.
        const auto ant_x       = ReadDoubleVector(group_id, "AntX");
        const auto ant_y       = ReadDoubleVector(group_id, "AntY");
        const auto ant_z       = ReadDoubleVector(group_id, "AntZ");
        const auto r0          = ReadDoubleVector(group_id, "R0");
        const auto pulse_times = ReadDoubleVector(group_id, "Np");

        // Read all complex phase-history samples.
        std::vector<Complex128> phdata_buf{};
        const auto read_err = ReadPhdataComplex(group_id, np, k, phdata_buf);
        H5Gclose(group_id);
        H5Fclose(file_id);

        if (!read_err.empty()) {
            result.message = read_err;
            return result;
        }

        const auto get_vec = [](const std::vector<double>& v, std::size_t i) -> double {
            if (v.empty()) return 0.0;
            return v[std::min(i, v.size() - 1)];
        };

        result.pulses.reserve(np);
        for (std::size_t p = 0; p < np; ++p) {
            GotchaHdf5PulseData pulse{};
            pulse.samples.reserve(k);
            for (std::size_t s = 0; s < k; ++s) {
                const auto& c = phdata_buf[p * k + s];
                pulse.samples.push_back(ComplexSample{
                    .real = static_cast<float>(c.real),
                    .imag = static_cast<float>(c.imag),
                });
            }
            pulse.antenna_position_m = {
                get_vec(ant_x, p),
                get_vec(ant_y, p),
                get_vec(ant_z, p),
            };
            pulse.reference_range_m = get_vec(r0, p);
            pulse.pulse_time        = get_vec(pulse_times, p);
            result.pulses.push_back(std::move(pulse));
        }

        result.success = true;
        result.message = "ok";
        return result;
    }

#endif  // GRAPHX_SAR_HAS_HDF5
};

}  // namespace graphx::sar
