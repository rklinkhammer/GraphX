# GraphX Documentation

**Status: Archived**

This documentation directory contains archived exploratory analysis and planning documents from the GraphX development baseline. These documents were consolidated and archived during PR8 (Documentation Surface Reduction) to consolidate the active documentation set.

## Archived Content

The following subdirectories were moved to `docs/archive/2026-06-baseline/doc/` during PR8:

- **architecture/** - Architectural analysis and design patterns (archived)
- **guides/** - Implementation guides and quick references (archived)
- **phase-reports/** - Phase completion and progress reports (archived)
- **tests/** - Test analysis and coverage reports (archived)

## Active Documentation

The active GraphX documentation is now consolidated in:

- **Top-level README.md** - User guide for building, running, and testing
- **plan/BASELINE.md** - Current architecture and next steps
- **plan/roadmap/GRAPHX_PR_ROADMAP.md** - Active cleanup roadmap
- **plan/agents/GRAPHX_PR_AGENTS.md** - Active PR prompts and templates
- **plan/agents/GRAPHX_AGENT_ROLES.md** - Active agent role definitions

## Archive Location

Historical documentation is preserved at:

```text
docs/archive/2026-06-baseline/doc/
```

This includes all exploratory analysis, design decisions, phase reports, and test documentation from the baseline phase.
- **THREADPOOL_TEST_SUITE_ANALYSIS.md** - ThreadPool test suite analysis
- **THREADPOOL_UNIT_TEST_ANALYSIS.md** - ThreadPool unit test analysis

### 📊 [phase-reports/](./phase-reports/)
**Development phase completion reports and progress tracking**

Phase-based progress reports and completion milestones:
- **CSVPARSER_PHASE1_COMPLETE.md** - Phase 1: CSV parser completion
- **DEBUGGING_INFRASTRUCTURE_ADDED.md** - Debugging infrastructure setup
- **DELIVERABLES_INDEX.md** - Master deliverables index
- **JSONVIEW_COMPLETE_TRANSFORMATION.md** - JSONView transformation complete
- **PHASE1_ACTIVEQUEUE_TESTS_COMPLETE.md** - Phase 1: ActiveQueue tests
- **PHASE1_MESSAGE_TESTS_COMPLETE.md** - Phase 1: Message tests complete
- **PHASE2_ACTIVEQUEUE_COMPLETION.md** - Phase 2: ActiveQueue completion
- **PHASE2_CSV_INTEGRATION_COMPLETE.md** - Phase 2: CSV integration complete
- **PHASE2_MESSAGE_TESTS_COMPLETE.md** - Phase 2: Message tests complete
- **PHASE3_MESSAGE_TESTS_COMPLETE.md** - Phase 3: Message tests complete
- **PHASE3_SYSTEM_INTEGRATION_COMPLETE.md** - Phase 3: System integration complete
- **PHASE5_PRIORITY1_COMPLETION.md** - Phase 5: Priority 1 items complete
- **CXX26_MIGRATION_PLAN.md** - C++26-first migration strategy
- **PLUGIN_COMPILATION_FIX_SUMMARY.md** - Plugin compilation fixes
- **STAGE_5_5B_COMPLETION_SUMMARY.md** - Stage 5.5b completion summary
- **STAGE_5_5B_MESSAGE_FLOW_VALIDATION.md** - Stage 5.5b message flow validation
- **THREADPOOL_ENHANCEMENTS_APPLIED.md** - ThreadPool enhancements applied
- **THREADPOOL_OPTIONAL_ENHANCEMENTS.md** - Optional ThreadPool enhancements

### 📖 [guides/](./guides/)
**Quick start guides, tutorials, and debug references**

User-friendly guides and quick reference documentation:
- **TestGraphTopologies_Executive_Summary.md** - Executive summary of test topologies
- **TestGraphTopologies_Implementation_Report.md** - Implementation report for test topologies
- **TestGraphTopologies_Quick_Start.md** - Quick start guide for test topologies
- **metal-cpp-native-runtime.md** - Native Metal runtime setup via metal-cpp
- **../scripts/install_metal_cpp.sh** - Helper script to install metal-cpp headers locally
- **TOPOLOGY5_DEBUG_GUIDE.md** - Debugging guide for topology features

## Organization Principles

Documentation is organized by **purpose** rather than by module:

1. **architecture/** - *Why* and *how* things work
   - Design decisions and rationale
   - System architecture and patterns
   - API specifications

2. **tests/** - *What was tested* and *how well*
   - Test analysis and coverage
   - Test results and metrics
   - Validation documentation

3. **phase-reports/** - *Progress and milestones*
   - Phase completion status
   - Feature delivery tracking
   - Historical progress records

4. **guides/** - *How to use* and *how to debug*
   - Quick start references
   - User guides and tutorials
   - Debugging and troubleshooting

## Quick Navigation

- **New to GraphX?** → Start with [guides/TestGraphTopologies_Quick_Start.md](./guides/TestGraphTopologies_Quick_Start.md)
- **Want architecture details?** → See [architecture/](./architecture/)
- **Looking for test coverage?** → Check [tests/](./tests/)
- **Tracking progress?** → Review [phase-reports/](./phase-reports/)

## Key Statistics

- **Total Documentation Files:** 64
- **Architecture Documents:** 24
- **Test Documents:** 15
- **Phase Reports:** 19
- **Guides & References:** 5

## Last Updated

May 31, 2026 - Documentation reorganized into logical categories for better navigation and maintainability.
