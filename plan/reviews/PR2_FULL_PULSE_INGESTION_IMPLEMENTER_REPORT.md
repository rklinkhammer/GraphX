# PR2 Implementation Report: Extend GotchaMatReader To Support Full-Pulse Ingestion

**Date:** 2026-06-14  
**Status:** ✅ COMPLETE  
**Tests:** 10/10 new + 4/4 existing passing  
**Build:** SUCCESS

---

## Executive Summary

PR2 has been successfully implemented. GotchaMatReader now supports full-pulse ingestion by reading all `Np` pulses from each input file instead of a hardcoded single pulse per file. The implementation maintains backward compatibility with existing code while enabling the full-aperture conversion pathway required for complete GOTCHA dataset processing.

---

## Scope Completed ✅

### 1. Full-Pulse Ingestion From Sidecar Np Field

**Changed:** `examples/SAR/include/sar/io/GotchaMatReader.hpp`

**Implementation:**
- Modified `ReadDetailed()` method to parse `Np` (number of pulses) from each file's sidecar
- Changed from reading 1 pulse per file to reading all `Np` pulses per file
- Default behavior: Files without `Np` field default to 1 pulse (backward compatible)

**Key Code Change:**
```cpp
// Full-aperture mode: iterate through each file and read all Np pulses per file
std::uint64_t global_pulse_index = 0;

for (std::size_t file_index = 0; file_index < ordering.files.size(); ++file_index) {
    const auto sidecar = LoadSidecar(sidecar_path, result.diagnostics.issues);
    if (!sidecar.has_value()) continue;

    // Read Np (number of pulses) from sidecar; default to 1 for backward compatibility
    const auto np = ParseOptionalUnsigned(*sidecar, "Np").value_or(1u);

    // Process each pulse within this file
    for (std::uint64_t pulse_within_file = 0; pulse_within_file < np; ++pulse_within_file) {
        // ... create and push pulse with global_pulse_index ...
        global_pulse_index++;
    }
}
```

### 2. Sequential Global Pulse Indexing

**Behavior:**
- Pulse indices are sequential across all files
- First pulse from first file: `vector_index = 0`
- First pulse from second file: `vector_index = Np[0]`
- Example: Files with Np=[2,3,4] produce global indices 0-8 (sequential)

**Verified by Tests:**
- ✅ Two files with 2+3 pulses → indices 0,1,2,3,4
- ✅ Five files with 10+15+8+12+5 → indices 0-49 (50 total)
- ✅ Single file with 1000 pulses → indices 0-999

### 3. Per-Pulse Ordering Within Each File

**Behavior:**
- Each file's pulses are processed in order (0 to Np-1)
- File order is preserved (lexical or manifest mode unchanged)
- Deterministic across repeated reads

**Verified by Tests:**
- ✅ Multi-pulse file produces ordered pulses
- ✅ Three files with 2,4,3 pulses maintain file order
- ✅ Repeated reads produce identical ordering

### 4. Normalized Product Contains One PulseVector Per Pulse

**Behavior:**
- Total `PulseVector` count = sum(Np) across all files
- Example: 2-file aperture with Np=[100, 120] produces 220 PulseVectors

**Verified by Tests:**
- ✅ Single file with Np=3 → 3 PulseVectors
- ✅ Two files with Np=[2,3] → 5 PulseVectors (not 2)
- ✅ Five files with sum=50 → 50 PulseVectors
- ✅ One file with Np=1000 → 1000 PulseVectors

### 5. Comprehensive Test Coverage

**New Test File:** `examples/SAR/test/test_gotcha_full_pulse_ingestion.cpp`
**Test Class:** `GotchaFullPulseIngestionTest`
**Test Count:** 10 new tests, all passing

| Test | Purpose | Status |
|------|---------|--------|
| SingleFileWithMultiplePulsesProducesSequentialVectors | Verify single file with 3 pulses | ✅ |
| TwoFilesWithMultiplePulsesProduceTotalCount | Verify 2+3 pulses from 2 files | ✅ |
| FilesWithoutNpFieldDefaultToSinglePulse | Backward compatibility | ✅ |
| PulseOrderingWithinFileIsPreserved | Sequential indexing | ✅ |
| MultiFileScenarioWithVaryingPulseCounts | 3 files with 2,4,3 pulses | ✅ |
| TotalPulseCountEqualsSum | 5 files with sum=50 pulses | ✅ |
| ChannelMetadataPreservedWithMultiplePulses | Waveform metadata per-file | ✅ |
| PlatformParametersAccessibleForAllPulses | Platform pos/vel accessibility | ✅ |
| RepeatedReadsAreIdentical | Deterministic behavior | ✅ |
| LargeNpValueProducesExpectedPulseCount | Np=1000 produces 1000 vectors | ✅ |

**Test Results:**
```
[==========] Running 10 tests from GotchaFullPulseIngestionTest
[  PASSED  ] 10 tests (15 ms total)
```

---

## Backward Compatibility

**Existing Tests:** All 4 existing `GotchaMatReaderTest` tests pass without modification

```
[==========] Running 4 tests from GotchaMatReaderTest
[  PASSED  ] 4 tests (3 ms total)
```

**Key Compatibility Features:**
- Files without `Np` field default to 1 pulse
- Lexical and manifest ordering modes unchanged
- Sidecar loading logic unchanged
- Metadata extraction unchanged
- Error handling unchanged

---

## Files Changed

### Implementation
- **`examples/SAR/include/sar/io/GotchaMatReader.hpp`**
  - Modified `ReadDetailed()` method: Changed from single-pulse-per-file to full-pulse-per-file
  - Added `global_pulse_index` tracking across all files
  - Added nested loop: file iteration → pulse-within-file iteration
  - Updated `vector_index` assignment to use global indices

### Tests
- **`examples/SAR/test/test_gotcha_full_pulse_ingestion.cpp`** (NEW)
  - 10 comprehensive test cases
  - Multi-pulse, multi-file scenarios
  - Deterministic behavior verification
  - Large Np value handling
  
- **`examples/SAR/test/CMakeLists.txt`**
  - Added `test_gotcha_full_pulse_ingestion.cpp` to test sources

### Files NOT Changed
- GotchaMatReader options/configuration unchanged
- NormalizedSarProduct model unchanged
- Sidecar parsing unchanged
- Report schemas unchanged (per scope)
- No metadata mapper work
- No MATLAB dependencies

---

## Build & Test Status

✅ **CMake Configuration:** SUCCESS (Ninja generator)  
✅ **Compilation:** SUCCESS (0 errors, 0 new warnings)  
✅ **New Tests:** 10/10 passing (15 ms)  
✅ **Existing Tests:** 4/4 passing (3 ms)  
✅ **CLI Binary:** Builds successfully with new reader logic  

---

## Key Design Decisions

### 1. Global Pulse Index Across Files
- Decision: Use sequential global index, not per-file reset
- Rationale: Simplifies downstream processing, enables accurate pulse counting for full-aperture reporting
- Alternative: Per-file pulse index (rejected as it would break PR7 accounting)

### 2. Default Np = 1 for Backward Compatibility
- Decision: Files without Np field are treated as single-pulse
- Rationale: Existing tests and workflows assume one pulse per file
- Impact: Old code continues to work; new code can specify Np > 1

### 3. Frequency Axis Set Once Per File
- Decision: `channel.waveform.frequency_axis_hz` set from first pulse of each file
- Rationale: Frequency axis is per-file metadata, not per-pulse
- Impact: All pulses from the same file share frequency axis

### 4. Metadata Collection Once Per File
- Decision: `CollectFieldDiagnostics()` called once after processing all pulses in each file
- Rationale: Source field names are per-file, not per-pulse
- Impact: Avoids redundant diagnostic data

---

## Scope Honored

### ✅ DO Requirements (All Met)
- Update GotchaMatReader to ingest every pulse described by Np ✅
- Remove hardcoded single pulse_index behavior ✅
- Preserve per-pulse ordering within each file ✅
- Ensure normalized product has one PulseVector per pulse ✅
- Add focused tests with synthetic multi-pulse-per-file fixture ✅

### ✅ DO NOT Requirements (All Honored)
- No multi-file aperture validation ✅
- No metadata mapper work ✅
- No report schema changes (only test additions) ✅
- No MATLAB dependencies ✅
- No new external dependencies ✅

---

## Validation Checklist

- ✅ All Np pulses per file are ingested
- ✅ Pulse ordering within each file is sequential
- ✅ Global pulse indices are sequential across all files
- ✅ Total pulse count = sum(Np) across input files
- ✅ Backward compatibility maintained (files without Np default to 1)
- ✅ Repeated reads produce deterministic results
- ✅ Channel metadata correctly set
- ✅ Platform parameters accessible for all pulses
- ✅ 10 focused tests cover all required scenarios
- ✅ 4 existing tests continue to pass
- ✅ No MATLAB or new external dependencies added

---

## Ready for Code Review

PR2 is **complete and ready for code review**. All acceptance criteria have been met:
1. ✅ GotchaMatReader reads all Np pulses per file
2. ✅ Hardcoded pulse_index behavior removed
3. ✅ Per-pulse ordering preserved within files
4. ✅ Normalized product contains one PulseVector per pulse
5. ✅ Total pulse count = sum(Np)
6. ✅ Comprehensive test coverage (10 new tests)
7. ✅ No scope violations

---

## Next Steps

PR3 is ready to proceed with: **Add Multi-File Aperture Ordering And Validation**
- Validate aperture completeness (no missing/duplicate/out-of-order files)
- Apply ordering before reader ingestion in CLI path
- Add synthetic multi-file aperture tests

---

## Build & Test Commands

```bash
# Build test executable
cd /Users/rklinkhammer/workspace/GraphX/build
ninja test_sar_example_unit

# Run full-pulse ingestion tests (10 tests)
./examples/SAR/test/test_sar_example_unit --gtest_filter="GotchaFullPulseIngestion*"

# Run existing reader tests for backward compatibility (4 tests)
./examples/SAR/test/test_sar_example_unit --gtest_filter="GotchaMatReaderTest*"

# Run all SAR tests
ctest --verbose -R "SAR"
```

