// SPDX-License-Identifier: MIT

/**
 * @file test_graphx_gotcha_to_crsd_cli.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/io/CrsdIO.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
#include <hdf5.h>
#endif

#ifndef SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH
#define SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH "./graphx-gotcha-to-crsd"
#endif

namespace {

std::string ShellQuote(const std::filesystem::path& path) {
    std::string raw = path.string();
    std::string quoted{"'"};
    for (const char ch : raw) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

struct CommandResult {
    int exit_code{};
    std::string output{};
};

CommandResult RunCommand(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return CommandResult{.exit_code = -1, .output = "popen failed"};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    return CommandResult{.exit_code = pclose(pipe), .output = output};
}

class GraphxGotchaToCrsdCliTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_to_crsd_cli_" + std::to_string(ticks));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    void WriteMatStub(const std::string& relative, bool hdf5_signature = true) const {
        const auto path = Path(relative);
        std::ofstream stream{path, std::ios::binary};
        ASSERT_TRUE(stream.good());
        if (hdf5_signature) {
            const std::array<unsigned char, 8> signature{
                0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
            stream.write(reinterpret_cast<const char*>(signature.data()),
                         static_cast<std::streamsize>(signature.size()));
        } else {
            stream << "MATLAB 5.0 MAT-file";
        }
        stream << " stub";
    }

    void WriteHdf5Mat(const std::string& relative, int base) const {
#if defined(GRAPHX_SAR_HAS_HDF5) && GRAPHX_SAR_HAS_HDF5
        const auto path = Path(relative);
        const hid_t file_id = H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        ASSERT_GE(file_id, 0);

        const hid_t group_id = H5Gcreate2(file_id, "subData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        ASSERT_GE(group_id, 0);

        struct Complex128 {
            double real;
            double imag;
        };

        const hid_t complex_type = H5Tcreate(H5T_COMPOUND, sizeof(Complex128));
        ASSERT_GE(complex_type, 0);
        ASSERT_GE(H5Tinsert(complex_type, "real", HOFFSET(Complex128, real), H5T_NATIVE_DOUBLE), 0);
        ASSERT_GE(H5Tinsert(complex_type, "imag", HOFFSET(Complex128, imag), H5T_NATIVE_DOUBLE), 0);

        const hsize_t ph_dims[2]{1, 2};
        const hid_t ph_space = H5Screate_simple(2, ph_dims, nullptr);
        ASSERT_GE(ph_space, 0);
        const hid_t ph_dset = H5Dcreate2(group_id, "phdata", complex_type, ph_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        ASSERT_GE(ph_dset, 0);
        const std::array<Complex128, 2> phdata{{
            {static_cast<double>(base + 1), -1.0},
            {static_cast<double>(base + 2), -2.0},
        }};
        ASSERT_GE(H5Dwrite(ph_dset, complex_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, phdata.data()), 0);
        H5Dclose(ph_dset);
        H5Sclose(ph_space);
        H5Tclose(complex_type);

        const hsize_t vec_dims[1]{1};
        const hid_t vec_space = H5Screate_simple(1, vec_dims, nullptr);
        ASSERT_GE(vec_space, 0);
        const auto write_vec = [&](const char* name, double value) {
            const hid_t dset = H5Dcreate2(group_id, name, H5T_NATIVE_DOUBLE, vec_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            ASSERT_GE(dset, 0);
            ASSERT_GE(H5Dwrite(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, &value), 0);
            H5Dclose(dset);
        };

        write_vec("AntX", 1.0);
        write_vec("AntY", 2.0);
        write_vec("AntZ", 3.0);
        write_vec("R0", 1000.0 + static_cast<double>(base));
        write_vec("Np", static_cast<double>(base));
        write_vec("K", 2.0);
        write_vec("deltaF", 1.0e6);
        write_vec("minF", 9.599e9);

        H5Sclose(vec_space);
        H5Gclose(group_id);
        H5Fclose(file_id);
#else
        (void)base;
        WriteMatStub(relative, true);
#endif
    }

    void WriteManifest(const std::string& relative, const nlohmann::json& manifest) const {
        std::ofstream stream{Path(relative)};
        ASSERT_TRUE(stream.good());
        stream << manifest.dump(2) << '\n';
    }

    [[nodiscard]] std::string CliBase() const {
        return ShellQuote(std::filesystem::path{SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH});
    }

    std::filesystem::path root_{};
};

TEST_F(GraphxGotchaToCrsdCliTest, HelpDocumentsRequiredOptions) {
    const auto result = RunCommand(CliBase() + " --help 2>&1");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("--input-dir"), std::string::npos);
    EXPECT_NE(result.output.find("--output-dir"), std::string::npos);
    EXPECT_NE(result.output.find("--collection-id"), std::string::npos);
    EXPECT_NE(result.output.find("--max-output-size-mb"), std::string::npos);
    EXPECT_NE(result.output.find("--sort"), std::string::npos);
    EXPECT_NE(result.output.find("--manifest"), std::string::npos);
    EXPECT_NE(result.output.find("--mode"), std::string::npos);
    EXPECT_NE(result.output.find("Output mode (supported: crsd)"), std::string::npos);
    EXPECT_NE(result.output.find("--validate"), std::string::npos);
    EXPECT_NE(result.output.find("--emit-index"), std::string::npos);
}

TEST_F(GraphxGotchaToCrsdCliTest, CrsdModeWorksOnTinyFixture) {
#if !defined(GRAPHX_SAR_HAS_HDF5) || !GRAPHX_SAR_HAS_HDF5
    GTEST_SKIP() << "HDF5 support is required for CRSD conversion fixtures.";
#endif
    const auto input_dir = Path("input");
    const auto output_dir = Path("output");
    ASSERT_TRUE(std::filesystem::create_directories(input_dir));

    WriteHdf5Mat("input/pulse_01.mat", 1);

    const auto command = CliBase() +
                         " --input-dir " + ShellQuote(input_dir) +
                         " --output-dir " + ShellQuote(output_dir) +
                         " --collection-id tiny-collection" +
                         " --max-output-size-mb 1" +
                         " --sort lexical" +
                         " --mode crsd" +
                         " --validate" +
                         " --emit-index 2>&1";

    const auto result = RunCommand(command);
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("conversion_successful"), std::string::npos);
    EXPECT_NE(result.output.find("info: converter: discovered 1 input MAT file(s)"), std::string::npos);
    EXPECT_NE(result.output.find("info: gotcha_reader: reading file 1/1"), std::string::npos);
    EXPECT_NE(result.output.find("info: hdf5_reader: phdata dimensions"), std::string::npos);
    EXPECT_NE(result.output.find("info: crsd_writer: invoking SarPy writer"), std::string::npos);

    EXPECT_TRUE(std::filesystem::exists(output_dir / "gotcha_crsd_chunk_0000.crsd" / graphx::sar::CrsdWriter::kSignalFile));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "gotcha_crsd_chunk_0000.crsd" / graphx::sar::CrsdWriter::kMetadataFile));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "gotcha_crsd_chunk_0000.crsd" / graphx::sar::CrsdWriter::kChunkIndexFile));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "gotcha_crsd_index.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "conversion_report.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "conversion_warnings.log"));
}

TEST_F(GraphxGotchaToCrsdCliTest, InvalidInputAndEmptyInputAndMalformedManifestFailDeterministically) {
    const auto missing = RunCommand(
        CliBase() +
        " --input-dir " + ShellQuote(Path("no-such-dir")) +
        " --output-dir " + ShellQuote(Path("out-a")) +
        " --collection-id c1 --max-output-size-mb 1 --sort lexical --mode crsd 2>&1");
    EXPECT_NE(missing.exit_code, 0);
    EXPECT_NE(missing.output.find("input_directory_not_found"), std::string::npos);

    const auto empty_dir = Path("empty");
    ASSERT_TRUE(std::filesystem::create_directories(empty_dir));
    const auto empty = RunCommand(
        CliBase() +
        " --input-dir " + ShellQuote(empty_dir) +
        " --output-dir " + ShellQuote(Path("out-b")) +
        " --collection-id c2 --max-output-size-mb 1 --sort lexical --mode crsd 2>&1");
    EXPECT_NE(empty.exit_code, 0);
    EXPECT_NE(empty.output.find("empty_input_directory"), std::string::npos);

    const auto manifest_dir = Path("manifest-case");
    ASSERT_TRUE(std::filesystem::create_directories(manifest_dir));
    WriteMatStub("manifest-case/pulse_00.mat");
    WriteManifest("bad_manifest.json", nlohmann::json{{"schema", "bad"}, {"files", nlohmann::json::array()}});

    const auto malformed_manifest = RunCommand(
        CliBase() +
        " --input-dir " + ShellQuote(manifest_dir) +
        " --output-dir " + ShellQuote(Path("out-c")) +
        " --collection-id c3 --max-output-size-mb 1 --sort manifest --manifest " +
        ShellQuote(Path("bad_manifest.json")) +
        " --mode crsd 2>&1");
    EXPECT_NE(malformed_manifest.exit_code, 0);
    EXPECT_TRUE(
        malformed_manifest.output.find("manifest_schema_error") != std::string::npos ||
        malformed_manifest.output.find("empty_manifest") != std::string::npos);
}

TEST_F(GraphxGotchaToCrsdCliTest, GotchaApertureOrderingErrorsFailBeforeReaderInCliPath) {
    const auto input_dir = Path("gapped-aperture");
    ASSERT_TRUE(std::filesystem::create_directories(input_dir));
    WriteMatStub("gapped-aperture/subData01.mat");
    WriteMatStub("gapped-aperture/subData03.mat");

    const auto result = RunCommand(
        CliBase() +
        " --input-dir " + ShellQuote(input_dir) +
        " --output-dir " + ShellQuote(Path("out-gap")) +
        " --collection-id gap-case --max-output-size-mb 1 --sort lexical --mode crsd 2>&1");

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.output.find("aperture_sequence_gap"), std::string::npos);
}

TEST_F(GraphxGotchaToCrsdCliTest, UnsupportedMatFailsClearlyAndCrsdModeProducesSarpyOpenableOutput) {
#if !defined(GRAPHX_SAR_HAS_HDF5) || !GRAPHX_SAR_HAS_HDF5
    GTEST_SKIP() << "HDF5 support is required for CRSD conversion fixtures.";
#endif
    const auto input_dir = Path("unsupported");
    ASSERT_TRUE(std::filesystem::create_directories(input_dir));
    WriteMatStub("unsupported/classic.mat", false);

    const auto unsupported = RunCommand(
        CliBase() +
        " --input-dir " + ShellQuote(input_dir) +
        " --output-dir " + ShellQuote(Path("out-d")) +
        " --collection-id c4 --max-output-size-mb 1 --sort lexical --mode crsd 2>&1");
    EXPECT_NE(unsupported.exit_code, 0);
    EXPECT_NE(unsupported.output.find("unsupported_mat_format"), std::string::npos);

    const auto crsd_input = Path("crsd-input");
    const auto crsd_output = Path("crsd-output");
    ASSERT_TRUE(std::filesystem::create_directories(crsd_input));
    WriteHdf5Mat("crsd-input/pulse_01.mat", 11);

    const auto crsd_mode = RunCommand(
        CliBase() +
        " --input-dir " + ShellQuote(crsd_input) +
        " --output-dir " + ShellQuote(crsd_output) +
        " --collection-id c5 --max-output-size-mb 1 --sort lexical --mode crsd --validate --emit-index 2>&1");

    EXPECT_EQ(crsd_mode.exit_code, 0) << crsd_mode.output;
    EXPECT_NE(crsd_mode.output.find("conversion_successful"), std::string::npos);
    const auto chunk_dir = crsd_output / "gotcha_crsd_chunk_0000.crsd";
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::CrsdWriter::kSignalFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::CrsdWriter::kMetadataFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::CrsdWriter::kPvpFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::CrsdWriter::kProvenanceFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::CrsdWriter::kChunkIndexFile));
    EXPECT_TRUE(std::filesystem::exists(crsd_output / "gotcha_crsd_index.json"));
    EXPECT_TRUE(std::filesystem::exists(crsd_output / "conversion_report.json"));
    EXPECT_TRUE(std::filesystem::exists(crsd_output / "conversion_warnings.log"));
}

} // namespace
