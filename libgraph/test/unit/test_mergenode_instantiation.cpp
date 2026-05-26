#include <gtest/gtest.h>
#include <memory>
#include "graph/Nodes.hpp"
#include "graph/Message.hpp"

using namespace graph;
using namespace message;

/// @test Test MergeNode<1> direct instantiation to see if it crashes
class MergeNode1TestNode : public graph::MergeNode<1, Message, Message, MergeNode1TestNode> {
public:
    std::optional<Message> Process(
        const Message& input,
        std::integral_constant<std::size_t, 0>) override {
        return input;
    }
    
    std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
        return graph::MergeNode<1, Message, Message, MergeNode1TestNode>::GetInputPortMetadata();
    }
    
    std::vector<graph::PortMetadata> GetOutputPortMetadata() const override {
        return graph::MergeNode<1, Message, Message, MergeNode1TestNode>::GetOutputPortMetadata();
    }
};

TEST(MergeNodeInstantiationTest, DirectMergeNode1Creation) {
    // This should NOT crash
    auto node = std::make_shared<MergeNode1TestNode>();
    EXPECT_NE(node, nullptr);
}

TEST(MergeNodeInstantiationTest, DirectMergeNode2Creation) {
    // Test with MergeNode<2> as well
    class MergeNode2TestNode : public graph::MergeNode<2, Message, Message, MergeNode2TestNode> {
    public:
        std::optional<Message> Process(
            const Message& input,
            std::integral_constant<std::size_t, 0>) override {
            return input;
        }
        
        std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
            return graph::MergeNode<2, Message, Message, MergeNode2TestNode>::GetInputPortMetadata();
        }
        
        std::vector<graph::PortMetadata> GetOutputPortMetadata() const override {
            return graph::MergeNode<2, Message, Message, MergeNode2TestNode>::GetOutputPortMetadata();
        }
    };
    
    auto node = std::make_shared<MergeNode2TestNode>();
    EXPECT_NE(node, nullptr);
}
