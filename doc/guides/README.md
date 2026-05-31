# Guides & Quick Start

User-friendly guides, tutorials, and debugging references for GraphX.

## Overview

This directory contains quick start guides, implementation tutorials, and debugging references to help you get started with GraphX and solve common problems.

## Quick Start Documents

### Getting Started
- **TestGraphTopologies_Quick_Start.md** - Start here for a 5-minute introduction
  - Basic concepts
  - Setting up your first topology
  - Running simple examples
  - Common patterns

### Detailed Implementation
- **TestGraphTopologies_Implementation_Report.md** - Comprehensive implementation guide
  - Step-by-step topology construction
  - Node creation and configuration
  - Edge connection patterns
  - Real-world examples

### Executive Summary
- **TestGraphTopologies_Executive_Summary.md** - High-level overview
  - Architecture summary
  - Key features
  - When to use GraphX
  - Design rationale

## Debugging & Troubleshooting

- **TOPOLOGY5_DEBUG_GUIDE.md** - Debugging guide for topology issues
  - Common problems and solutions
  - Debugging techniques
  - Performance profiling
  - Error interpretation

## Reading Order

### For Complete Beginners
1. **TestGraphTopologies_Quick_Start.md** (5 min)
2. **TestGraphTopologies_Executive_Summary.md** (10 min)
3. **TestGraphTopologies_Implementation_Report.md** (30 min)
4. Then: [../architecture/NODEFACTORY_QUICK_REFERENCE.md](../architecture/NODEFACTORY_QUICK_REFERENCE.md)

### For Experienced Developers
1. **TestGraphTopologies_Executive_Summary.md** (10 min)
2. **TestGraphTopologies_Implementation_Report.md** (20 min)
3. Then: [../architecture/](../architecture/) for specific components

### For Debugging Issues
1. **TOPOLOGY5_DEBUG_GUIDE.md** - Start here
2. [../tests/](../tests/) - Check what tests cover your scenario
3. [../architecture/](../architecture/) - Deep dive into design

## Quick Reference

### Common Tasks

**Creating a node:**
→ See [STATICNODEADAPTER_USAGE_GUIDE.md](../architecture/STATICNODEADAPTER_USAGE_GUIDE.md)

**Building a topology:**
→ See [TestGraphTopologies_Implementation_Report.md](./TestGraphTopologies_Implementation_Report.md)

**Using plugins:**
→ See [PLUGIN_SYSTEM_TEST_ANALYSIS.md](../architecture/PLUGIN_SYSTEM_TEST_ANALYSIS.md)

**Understanding messages:**
→ See [PRODUCER_MESSAGE_ENCAPSULATION_ANALYSIS.md](../architecture/PRODUCER_MESSAGE_ENCAPSULATION_ANALYSIS.md)

**Working with capabilities:**
→ See [CAPABILITY_ABSTRACTION_ANALYSIS.md](../architecture/CAPABILITY_ABSTRACTION_ANALYSIS.md)

## Concept Glossary

These guides explain key GraphX concepts:

| Concept | File | Time |
|---------|------|------|
| Nodes | TestGraphTopologies_Quick_Start.md | 2 min |
| Edges | TestGraphTopologies_Quick_Start.md | 2 min |
| Topologies | TestGraphTopologies_Implementation_Report.md | 5 min |
| Producers | PRODUCER_GRAPH_TOPOLOGIES_IMPLEMENTATION.md | 10 min |
| Messages | PRODUCER_MESSAGE_ENCAPSULATION_ANALYSIS.md | 10 min |
| Plugins | PLUGIN_SYSTEM_TEST_ANALYSIS.md | 10 min |
| Capabilities | CAPABILITY_ABSTRACTION_ANALYSIS.md | 10 min |

## Tutorial Examples

### Example 1: Create a Simple Topology
1. Open [TestGraphTopologies_Quick_Start.md](./TestGraphTopologies_Quick_Start.md)
2. Follow "Creating Your First Topology" section
3. Build and run the example
4. Expected result: Clean compilation, successful execution

### Example 2: Add a Producer Node
1. Read [PRODUCER_GRAPH_TOPOLOGIES_IMPLEMENTATION.md](../architecture/PRODUCER_GRAPH_TOPOLOGIES_IMPLEMENTATION.md)
2. Follow [TestGraphTopologies_Implementation_Report.md](./TestGraphTopologies_Implementation_Report.md)
3. Create a custom producer
4. Integrate into topology

### Example 3: Load a Plugin
1. Understand [PLUGIN_SYSTEM_TEST_ANALYSIS.md](../architecture/PLUGIN_SYSTEM_TEST_ANALYSIS.md)
2. See [MULTI_PLUGIN_DIRECTORY_ANALYSIS.md](../architecture/MULTI_PLUGIN_DIRECTORY_ANALYSIS.md)
3. Use NodeFactory to load dynamic nodes
4. Verify with test suites

## Performance Tips

From [PRODUCER_TOPOLOGY_PARAMETERIZATION_ANALYSIS.md](../architecture/PRODUCER_TOPOLOGY_PARAMETERIZATION_ANALYSIS.md):
- Use shared_ptr for message passing
- Minimize copying with type erasure
- Profile with CMAKE flags

From [THREADPOOL_TEST_SUITE_ANALYSIS.md](../tests/../architecture/THREADPOOL_TEST_SUITE_ANALYSIS.md):
- Configure worker threads appropriately
- Monitor queue depth
- Balance load across nodes

## Troubleshooting

### Compilation Issues
→ Check [PLUGIN_COMPILATION_FIX_SUMMARY.md](../phase-reports/PLUGIN_COMPILATION_FIX_SUMMARY.md)

### Runtime Problems
→ See [TOPOLOGY5_DEBUG_GUIDE.md](./TOPOLOGY5_DEBUG_GUIDE.md)

### Performance Concerns
→ Review [THREADPOOL_ENHANCEMENTS_APPLIED.md](../phase-reports/THREADPOOL_ENHANCEMENTS_APPLIED.md)

### Message System Issues
→ Read [MESSAGE_TEST_SUITE_FINAL.md](../tests/MESSAGE_TEST_SUITE_FINAL.md)

## Video Tutorials

(When available, video recordings of these guides will be linked here)

## Community & Support

- **Architecture Questions:** See [../architecture/ANALYSIS_EXECUTIVE_SUMMARY.md](../architecture/ANALYSIS_EXECUTIVE_SUMMARY.md)
- **Test Examples:** Browse [../tests/](../tests/)
- **Phase History:** Check [../phase-reports/DELIVERABLES_INDEX.md](../phase-reports/DELIVERABLES_INDEX.md)

## Estimated Reading Time

| Document | Level | Time |
|----------|-------|------|
| Quick_Start | Beginner | 5 min |
| Executive_Summary | Beginner | 10 min |
| Implementation_Report | Intermediate | 30 min |
| Debug_Guide | Intermediate | 20 min |

## For Contributions

If you're contributing to GraphX:
1. Start with [../architecture/](../architecture/) to understand design
2. Review [../tests/](../tests/) for similar patterns
3. Check [../phase-reports/DELIVERABLES_INDEX.md](../phase-reports/DELIVERABLES_INDEX.md) for current status
4. Update documentation as you add features

## See Also

- **Full Documentation Index:** [../README.md](../README.md)
- **Architecture Details:** [../architecture/](../architecture/)
- **Test Coverage:** [../tests/](../tests/)
- **Development History:** [../phase-reports/](../phase-reports/)
