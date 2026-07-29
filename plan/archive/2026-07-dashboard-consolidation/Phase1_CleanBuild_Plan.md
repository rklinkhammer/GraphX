# GraphX Clean Build & Pre-Phase1 Verification Plan

> Archived historical dashboard-planning record. Not current authority.

**Date:** 2026-07-24  
**Purpose:** Establish clean baseline before Phase 1 implementation  
**Scope:** Build, test, Docker validation, operator acceptance

---

## Clean Build Strategy

### 1. Environment Preparation

```bash
# Clear previous builds
rm -rf build-ninja/
rm -rf build/
rm -rf cmake-build-debug/

# Confirm environment
uname -a                    # macOS x86_64 or ARM64
clang --version            # AppleClang 15.0.0+
cmake --version            # 4.0.0+
ninja --version            # 1.11+
```

### 2. Build Configurations to Test

| Config | Purpose | Build Dir | Expected |
|--------|---------|-----------|----------|
| **ninja-debug** (macOS default) | Development | `build-ninja/ninja-debug` | ✓ All tests pass |
| **ninja-debug-linux-host** (if available) | CI compliance | `build-ninja/ninja-debug-linux-host` | ✓ All tests pass (Linux only) |
| **Dashboard Disabled** | Verify Phase 1 prep | `build-ninja/ninja-debug` with `BUILD_WEB_DASHBOARD=OFF` | ✓ Builds, excludes dashboard |

### 3. Test Suites to Run

```
Primary Test Suites:
├─ libgraph tests        (core runtime)
├─ libdsp tests          (DSP examples, FHSS)
├─ libgpu tests          (accelerator integration)
├─ examples/DSP tests    (end-to-end workflows)
└─ examples/SAR tests    (SAR pipelines - if enabled)

Docker Container:
├─ Build validation
├─ ASan/UBSan checks
└─ Integration test

Operator Acceptance:
├─ FHSS operator workflow (if implemented)
├─ API schema validation
└─ Baseline capture for rollback
```

---

## Build Execution Plan

### Phase 1: Clean macOS Build (ninja-debug)

```bash
# Step 1: Configure
cd /Users/rklinkhammer/workspace/GraphX
cmake -S . -B build-ninja/ninja-debug \
  --preset ninja-debug \
  -DBUILD_TESTS=ON \
  -DGRAPHX_BUILD_WEB_DASHBOARD=OFF \
  -DCMAKE_BUILD_TYPE=Debug

# Step 2: Build
cmake --build build-ninja/ninja-debug --parallel 4

# Step 3: Test Results
cd build-ninja/ninja-debug
ctest --verbose --output-on-failure

# Expected output:
#   All tests pass (0 failures)
#   No compiler warnings
#   No ASan/UBSan issues (if enabled)
```

### Phase 2: Regression Build (Dashboard Disabled)

```bash
# Verify Phase 1 prep: dashboard-disabled build contains NO dashboard files
cd /Users/rklinkhammer/workspace/GraphX
cmake -S . -B build-ninja/ninja-debug-no-dashboard \
  --preset ninja-debug \
  -DBUILD_TESTS=ON \
  -DGRAPHX_BUILD_WEB_DASHBOARD=OFF

cmake --build build-ninja/ninja-debug-no-dashboard --parallel 4

# Verify no dashboard files
ls -la build-ninja/ninja-debug-no-dashboard/share/graphx/
# Expected: no fhss-dashboard directory

# Run tests
cd build-ninja/ninja-debug-no-dashboard
ctest --verbose
```

### Phase 3: Dashboard-Enabled Build (Pre-Phase 1)

```bash
# This will be the starting point for Phase 1 modifications
cd /Users/rklinkhammer/workspace/GraphX
cmake -S . -B build-ninja/ninja-debug-with-dashboard \
  --preset ninja-debug \
  -DBUILD_TESTS=ON \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON

cmake --build build-ninja/ninja-debug-with-dashboard --parallel 4

# Run tests
cd build-ninja/ninja-debug-with-dashboard
ctest --verbose

# Capture dashboard baseline
find . -path "*fhss-dashboard*" -type f | sort > /tmp/dashboard-baseline.txt
find . -path "*dashboard*" -type f | sort >> /tmp/dashboard-baseline.txt
```

---

## Docker Container Validation

### Test Strategy

```bash
# Build: Verify container builds successfully
docker build -f containers/dashboard-operator/Dockerfile \
  -t graphx:latest \
  .

# Health: Verify container health checks
docker run --name graphx-test \
  -d \
  -p 8765:8765 \
  graphx:latest

# Wait for container
sleep 5

# Check health
docker exec graphx-test curl -s http://localhost:8765/healthz | jq .

# Stop
docker stop graphx-test
docker rm graphx-test

# Expected output:
#   {"status": "alive"}
```

### Container Test Checklist

- [ ] Container builds without warnings
- [ ] Image size reasonable (<1 GB)
- [ ] Health endpoint responds
- [ ] Port binding works (8765)
- [ ] Container stops gracefully
- [ ] No orphaned processes

---

## Operator Acceptance Test

### FHSS Operator Workflow (If Implemented)

```bash
# Check if operator tool exists
ls -la examples/DSP/dashboard/operator/
ls -la examples/DSP/dashboard/operator/fhss_dashboard_operator.py

# If exists, run baseline workflow
cd examples/DSP/dashboard/operator/

# Step 1: Prepare
python3 fhss_dashboard_operator.py prepare --phase current

# Step 2: Serve (short-lived test)
timeout 10 python3 fhss_dashboard_operator.py serve --phase current

# Step 3: Exercise (basic checks)
python3 fhss_dashboard_operator.py exercise --phase current

# Step 4: Verify (schema validation)
python3 fhss_dashboard_operator.py verify --phase current

# Step 5: Report (generate baseline)
python3 fhss_dashboard_operator.py report --output baseline-2026-07-24.json

# Step 6: Cleanup
python3 fhss_dashboard_operator.py cleanup
```

### Operator Baseline Capture

```json
{
  "date": "2026-07-24",
  "phase": "current",
  "commit": "$(git rev-parse HEAD)",
  "compiler": "$(clang --version | head -1)",
  "platform": "$(uname -a)",
  "operator_version": "$(grep -o 'version.*' examples/DSP/dashboard/operator/fhss_dashboard_operator.py | head -1)",
  "tests_executed": ["prepare", "serve", "exercise", "verify", "report"],
  "all_tests_passed": true,
  "api_endpoints": [
    "GET /",
    "GET /healthz",
    "GET /readyz",
    "GET /version",
    "GET /api/v1/fhss/snapshot"
  ],
  "generated_files": [
    "baseline-2026-07-24.json"
  ]
}
```

---

## Baseline Artifact Collection

### Hash Current State for Rollback

```bash
# Hash dashboard directory (pre-Phase 1)
shasum -r examples/DSP/dashboard/ > /tmp/dashboard-baseline-hash.txt

# Hash operator tool
shasum examples/DSP/dashboard/operator/fhss_dashboard_operator.py > /tmp/operator-baseline-hash.txt

# Hash HTTP server implementation
find src -name "*Dashboard*" -o -name "*Http*" | xargs shasum > /tmp/http-server-baseline-hash.txt

# Record git state
git status > /tmp/git-status-baseline.txt
git log --oneline -5 > /tmp/git-log-baseline.txt
git describe --tags > /tmp/git-describe-baseline.txt
```

### Save to Repository Memory

Store baseline hashes and logs in `/memories/repo/Phase1_PreBuild_Baseline.md`:

```markdown
# Phase 1 Pre-Build Baseline (2026-07-24)

## Repository State
- Commit: abc123...
- Branch: main
- Status: clean
- Last tag: v1.x.x

## Build Environment
- Platform: macOS x86_64
- AppleClang: 15.0.0
- CMake: 4.3.3
- Ninja: 1.11.1

## Baseline Hashes
- dashboard/: sha256:...
- operator/: sha256:...
- http-server/: sha256:...

## Test Results (Baseline)
- libgraph tests: ✓ PASS (X/X)
- libdsp tests: ✓ PASS (X/X)
- examples/DSP: ✓ PASS (X/X)
- Operator workflow: ✓ PASS

## Docker Validation
- Container builds: ✓ OK
- Health endpoint: ✓ OK
- Port binding: ✓ OK

## Operator Report (Baseline)
- All commands: ✓ OK
- API endpoints: ✓ 5/5 responding
- Report JSON: ✓ valid
```

---

## Clean Build Checklist

### Pre-Build Validation

- [ ] Repository clean (`git status` = clean)
- [ ] No uncommitted changes
- [ ] Current branch is `main`
- [ ] Compiler available (`clang --version`)
- [ ] CMake 4.0+ available
- [ ] Ninja available

### Build Execution

- [ ] Phase 1: ninja-debug build succeeds
- [ ] Phase 1: All tests pass
- [ ] Phase 1: No compiler warnings
- [ ] Phase 2: Dashboard-disabled build succeeds
- [ ] Phase 2: All tests pass
- [ ] Phase 3: Dashboard-enabled build succeeds
- [ ] Phase 3: Baseline artifacts captured

### Docker Validation

- [ ] Container builds
- [ ] Image size reasonable
- [ ] Health endpoint works
- [ ] Port binding works
- [ ] Container stops gracefully

### Operator Acceptance (If Implemented)

- [ ] Operator tool found
- [ ] prepare command works
- [ ] serve command launches
- [ ] exercise command validates
- [ ] verify command checks schemas
- [ ] report command generates JSON
- [ ] cleanup command removes temp files

### Baseline Preservation

- [ ] Baseline hashes saved
- [ ] Operator report captured
- [ ] Git state recorded
- [ ] Build logs archived
- [ ] Ready for Phase 1 assignment

---

## Rollback Procedure (If Needed)

```bash
# If anything goes wrong, rollback to pre-Phase 1 state:

# Option 1: Clean and rebuild
cd /Users/rklinkhammer/workspace/GraphX
rm -rf build-ninja/
git clean -fdx  # Remove untracked files

# Option 2: Revert to baseline commit
git reset --hard HEAD

# Option 3: Use git tag rollback
git checkout phase-1-pre-build-baseline

# Verify rollback
git status
git describe --tags
```

---

## Success Criteria

Phase 1 is **ready to begin** when:

1. ✅ Clean build succeeds (ninja-debug)
2. ✅ All regression tests pass
3. ✅ Dashboard-disabled build verified
4. ✅ Docker container validates
5. ✅ Operator workflow passes (if applicable)
6. ✅ Baseline artifacts preserved
7. ✅ No unresolved issues
8. ✅ Repository is clean and committed

---

## Next Steps After Baseline

Once clean build is validated:

1. **Commit baseline state** → git tag `phase-1-pre-build-baseline`
2. **Archive this plan** → save to `/memories/repo/`
3. **Preserve hashes** → save baseline artifact hashes
4. **Ready for Phase 1** → orchestrator can assign implementer
5. **Follow orchestration prompt** → use Phase1_Implementation.md

---

**Plan Version:** 1.0  
**Created:** 2026-07-24  
**Status:** Ready for Execution
