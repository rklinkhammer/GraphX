// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include "graph/InputFunction.hpp"
#include "graph/Lifecycle.hpp"
#include "graph/MergeFunction.hpp"
#include "graph/OutputFunction.hpp"
#include "graph/PortSpec.hpp"
#include "graph/PortTypes.hpp"
#include "graph/ThreadMetrics.hpp"
#include "graph/TransferFunction.hpp"

namespace {

namespace fs = std::filesystem;

constexpr char kPr10PortName[] = "pr10_port";

class Pr10LifecycleNode final : public graph::NodeLifecycleMixin<Pr10LifecycleNode> {
public:
    std::string GetDerivedClassName() const { return "Pr10LifecycleNode"; }

    bool InitPortsImpl() {
        ++init_calls_;
        return true;
    }

    bool StartPortsImpl() {
        ++start_calls_;
        return true;
    }

    void StopPortsImpl() { ++stop_calls_; }

    void JoinPortsImpl() { ++join_calls_; }

    bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds) {
        ++join_with_timeout_calls_;
        return true;
    }

    int init_calls() const { return init_calls_; }
    int start_calls() const { return start_calls_; }
    int stop_calls() const { return stop_calls_; }
    int join_calls() const { return join_calls_; }
    int join_with_timeout_calls() const { return join_with_timeout_calls_; }

private:
    int init_calls_{0};
    int start_calls_{0};
    int stop_calls_{0};
    int join_calls_{0};
    int join_with_timeout_calls_{0};
};

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to read " << path;
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

TEST(PR10HeaderGuardrailTest, FocusedHeaderFilesExist) {
    const fs::path root{GRAPHX_SOURCE_ROOT};
    const fs::path graph_include = root / "libgraph" / "include" / "graph";

    const std::vector<fs::path> required_headers{
        graph_include / "Lifecycle.hpp",
        graph_include / "PortSpec.hpp",
        graph_include / "PortTypes.hpp",
        graph_include / "InputFunction.hpp",
        graph_include / "OutputFunction.hpp",
        graph_include / "TransferFunction.hpp",
        graph_include / "MergeFunction.hpp",
        graph_include / "ThreadMetrics.hpp",
    };

    for (const auto& header : required_headers) {
        EXPECT_TRUE(fs::exists(header)) << header;
    }
}

TEST(PR10HeaderGuardrailTest, FocusedHeadersExposeCoreCompileTimeTypes) {
    using Payloads = graph::PayloadList<int, double>;
    static_assert(Payloads::size == 2);

    using PortSpecType = graph::PortSpec<0, int, graph::PortDirection::Input, kPr10PortName, Payloads>;
    static_assert(PortSpecType::index == 0);
    static_assert(PortSpecType::direction == graph::PortDirection::Input);

    using Ports = typename graph::MakePorts<graph::TypeList<int, float>>::type;
    static_assert(std::is_same_v<
        Ports,
        graph::TypeList<graph::Port<int, 0>, graph::Port<float, 1>>>);

    graph::ThreadMetrics metrics{};
    EXPECT_EQ(metrics.total_iterations.load(), 0ULL);
}

TEST(PR10HeaderGuardrailTest, LifecycleMixinMaintainsExpectedStateTransitions) {
    Pr10LifecycleNode node;

    EXPECT_EQ(node.GetLifecycleState(), graph::LifecycleState::Uninitialized);
    EXPECT_TRUE(node.Init());
    EXPECT_EQ(node.GetLifecycleState(), graph::LifecycleState::Initialized);

    EXPECT_TRUE(node.Start());
    EXPECT_EQ(node.GetLifecycleState(), graph::LifecycleState::Started);

    node.Stop();
    EXPECT_EQ(node.GetLifecycleState(), graph::LifecycleState::Stopped);

    EXPECT_TRUE(node.JoinWithTimeout(std::chrono::milliseconds(1)));
    EXPECT_EQ(node.GetLifecycleState(), graph::LifecycleState::Joined);

    node.Join();
    EXPECT_EQ(node.GetLifecycleState(), graph::LifecycleState::Joined);

    EXPECT_EQ(node.init_calls(), 1);
    EXPECT_EQ(node.start_calls(), 1);
    EXPECT_EQ(node.stop_calls(), 1);
    EXPECT_EQ(node.join_calls(), 1);
    EXPECT_EQ(node.join_with_timeout_calls(), 1);
}

TEST(PR10HeaderGuardrailTest, NoHeaderDeclaresDeprecatedIncludeShimMarker) {
    const fs::path root{GRAPHX_SOURCE_ROOT};
    const fs::path graph_include = root / "libgraph" / "include" / "graph";

    for (const auto& entry : fs::directory_iterator(graph_include)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
            continue;
        }

        const auto text = ReadFile(entry.path());
        EXPECT_EQ(text.find("deprecated include shim"), std::string::npos)
            << entry.path();
        EXPECT_EQ(text.find("DEPRECATED_INCLUDE_SHIM"), std::string::npos)
            << entry.path();
    }
}
