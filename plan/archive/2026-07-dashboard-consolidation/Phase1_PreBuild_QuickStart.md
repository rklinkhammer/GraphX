# GraphX Pre-Phase 1 Clean Build & Verification Guide

> Archived historical dashboard-planning record. Not current authority.

**Purpose:** Establish clean baseline before Phase 1 implementation  
**Date:** 2026-07-24  
**Status:** Ready for local execution  

---

## Quick Start (Run These Commands)

### Step 1: Prepare Environment

```bash
cd /Users/rklinkhammer/workspace/GraphX

# Clean previous builds
rm -rf build-ninja/
rm -rf build/

# Verify environment
clang --version          # Should be AppleClang 21+
cmake --version          # Should be 4.0+
ninja --version          # Should be 1.11+
```

### Step 2: Build 1 - Default (Dashboard Disabled)

```bash
# Configure
cmake -S . -B build-ninja/ninja-debug \
  --preset ninja-debug \
  -DBUILD_TESTS=ON \
  -DGRAPHX_BUILD_WEB_DASHBOARD=OFF \
  -DCMAKE_BUILD_TYPE=Debug

# Build (this takes ~5-10 minutes)
cmake --build build-ninja/ninja-debug --parallel 4

# Run tests
cd build-ninja/ninja-debug
ctest --verbose
cd ../..

# Expected: All tests pass ✓
```

### Step 3: Build 2 - Dashboard Enabled (Phase 1 Baseline)

```bash
# Configure
cmake -S . -B build-ninja/ninja-debug-dashboard \
  --preset ninja-debug \
  -DBUILD_TESTS=ON \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build-ninja/ninja-debug-dashboard --parallel 4

# Run tests
cd build-ninja/ninja-debug-dashboard
ctest --verbose
cd ../..

# Expected: All tests pass ✓
```

### Step 4: Verify Dashboard Assets (Phase 1 Prep)

```bash
# Check that dashboard files exist and are correct
ls -la build-ninja/ninja-debug-dashboard/share/graphx/fhss-dashboard/

# Expected output:
# - index.html
# - openapi.yaml (will be added in Phase 1)
# - JSON schema files (will be added in Phase 1)
```

### Step 5: Capture Baseline Hashes

```bash
# Save current state for rollback
shasum -r examples/DSP/dashboard/ > /tmp/dashboard-baseline-hash.txt
git rev-parse HEAD > /tmp/git-commit-baseline.txt
git describe --tags > /tmp/git-tags-baseline.txt

echo "✓ Baseline captured"
cat /tmp/git-commit-baseline.txt
```

---

## What This Verifies

| Check | Purpose | Expected Result |
|-------|---------|-----------------|
| **C++26 Build** | Compiler compatibility | ✓ Builds without warnings |
| **Unit Tests** | Regression baseline | ✓ All tests pass |
| **Dashboard Disabled** | Phase 1 prep verification | ✓ No dashboard files in build |
| **Dashboard Enabled** | Phase 1 baseline | ✓ Dashboard assets present |
| **Baseline Hashes** | Rollback capability | ✓ Hashes recorded |

---

## Success Criteria

Phase 1 is **ready to begin** when:

- ✅ Build 1 (dashboard disabled) passes all tests
- ✅ Build 2 (dashboard enabled) passes all tests
- ✅ Dashboard files are in correct location
- ✅ Baseline hashes are captured
- ✅ Repository is clean and committed
- ✅ Git state is recorded

---

## If Something Fails

### Memory issue during build
```bash
# Reduce parallelism
cmake --build build-ninja/ninja-debug --parallel 2
```

### Compiler error
```bash
# Check compiler version
clang --version

# Verify C++26 support
clang++ -std=c++2c --version
```

### Test failures
```bash
# Run failed test with verbose output
cd build-ninja/ninja-debug
ctest --verbose --output-on-failure
```

### Clean rebuild
```bash
# Start from scratch
rm -rf build-ninja/
cmake -S . -B build-ninja/ninja-debug --preset ninja-debug -DBUILD_TESTS=ON
cmake --build build-ninja/ninja-debug --parallel 4
```

---

## Docker Validation (Optional)

If you want to test the container:

```bash
# Build container
docker build -f containers/dashboard-operator/Dockerfile \
  -t graphx:pre-phase1 \
  .

# Run container
docker run -d --name graphx-test \
  -p 8765:8765 \
  graphx:pre-phase1

# Test health endpoint
sleep 2
curl http://localhost:8765/healthz

# Stop container
docker stop graphx-test && docker rm graphx-test

# Expected: {"status": "alive"}
```

---

## Timeline

- **Build 1 (dashboard disabled):** ~5-10 minutes
- **Build 2 (dashboard enabled):** ~5-10 minutes  
- **Test execution:** ~2-5 minutes
- **Baseline capture:** ~1 minute

**Total:** ~20-30 minutes

---

## After Successful Verification

```bash
# 1. Commit baseline state
git add plan/Phase1_CleanBuild_Plan.md
git add scripts/clean-build-and-verify.sh
git commit -m "Phase 1 pre-build baseline (2026-07-24)"

# 2. Tag for reference
git tag phase-1-pre-build-baseline

# 3. Ready for Phase 1
echo "✓ Repository ready for Phase 1 implementation"
echo "✓ Use orchestration prompt: plan/prompts/FHSS_Dashboard_V2_Phase1_Implementation.md"
```

---

## Orchestration Prompt

Once baseline is verified, use this for Phase 1:

**Location:** `plan/prompts/FHSS_Dashboard_V2_Phase1_Implementation.md`

**Models:**
- Orchestrator: Claude 3.5 Sonnet (Copilot)
- Implementer: Claude 3.5 Sonnet (Copilot)
- Verifier: Claude 3.5 Sonnet (Copilot) — fresh instance

---

**Guide Version:** 1.0  
**Created:** 2026-07-24  
**Status:** Ready for local execution
