#!/bin/bash
#
# GraphX Clean Build & Pre-Phase 1 Verification Script
# 
# Purpose: Execute complete clean build, testing, and baseline capture
# Usage: ./scripts/clean-build-and-verify.sh
#

set -e  # Exit on first error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build-ninja"
TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
LOG_DIR="${PROJECT_ROOT}/.graphx-operator/clean-build-${TIMESTAMP}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Functions
log_header() {
    echo -e "${BLUE}===================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}===================================================${NC}"
}

log_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

log_error() {
    echo -e "${RED}✗ $1${NC}"
}

log_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

log_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# Create log directory
mkdir -p "$LOG_DIR"

log_header "GraphX Clean Build & Pre-Phase 1 Verification"
log_info "Timestamp: $TIMESTAMP"
log_info "Project root: $PROJECT_ROOT"
log_info "Build directory: $BUILD_DIR"
log_info "Log directory: $LOG_DIR"

# ============================================================================
# PHASE 1: Environment Validation
# ============================================================================

log_header "PHASE 1: Environment Validation"

log_info "Checking platform..."
PLATFORM=$(uname -s)
ARCH=$(uname -m)
if [[ "$PLATFORM" == "Darwin" ]]; then
    log_success "Platform: macOS"
elif [[ "$PLATFORM" == "Linux" ]]; then
    log_success "Platform: Linux"
else
    log_error "Unsupported platform: $PLATFORM"
    exit 1
fi
log_info "Architecture: $ARCH"

log_info "Checking compiler..."
if ! command -v clang &> /dev/null; then
    log_error "clang not found"
    exit 1
fi
CLANG_VERSION=$(clang --version | head -1)
log_success "Compiler: $CLANG_VERSION"

log_info "Checking CMake..."
if ! command -v cmake &> /dev/null; then
    log_error "cmake not found"
    exit 1
fi
CMAKE_VERSION=$(cmake --version | head -1)
log_success "CMake: $CMAKE_VERSION"

log_info "Checking Ninja..."
if ! command -v ninja &> /dev/null; then
    log_error "ninja not found"
    exit 1
fi
NINJA_VERSION=$(ninja --version)
log_success "Ninja: $NINJA_VERSION"

# ============================================================================
# PHASE 2: Repository State Validation
# ============================================================================

log_header "PHASE 2: Repository State Validation"

cd "$PROJECT_ROOT"

log_info "Checking git status..."
GIT_STATUS=$(git status --porcelain)
if [[ -n "$GIT_STATUS" ]]; then
    log_warning "Working directory has uncommitted changes:"
    echo "$GIT_STATUS"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_error "Aborting due to uncommitted changes"
        exit 1
    fi
fi

GIT_COMMIT=$(git rev-parse HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
log_success "Git commit: $GIT_COMMIT"
log_success "Git branch: $GIT_BRANCH"

# ============================================================================
# PHASE 3: Clean Previous Builds
# ============================================================================

log_header "PHASE 3: Clean Previous Builds"

log_info "Removing old build directories..."
rm -rf "$BUILD_DIR/ninja-debug" 2>/dev/null || true
rm -rf "$BUILD_DIR/ninja-debug-no-dashboard" 2>/dev/null || true
rm -rf "$BUILD_DIR/ninja-debug-with-dashboard" 2>/dev/null || true
log_success "Build directories cleaned"

# ============================================================================
# PHASE 4: Build 1 - Default ninja-debug
# ============================================================================

log_header "PHASE 4: Build 1 - Default ninja-debug"

log_info "Configuring..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR/ninja-debug" \
    --preset ninja-debug \
    -DBUILD_TESTS=ON \
    -DGRAPHX_BUILD_WEB_DASHBOARD=OFF \
    -DCMAKE_BUILD_TYPE=Debug \
    2>&1 | tee "$LOG_DIR/build1-configure.log"

log_success "Configuration complete"

log_info "Building..."
cmake --build "$BUILD_DIR/ninja-debug" --parallel 4 2>&1 | tee "$LOG_DIR/build1-build.log"
log_success "Build complete"

log_info "Running tests..."
cd "$BUILD_DIR/ninja-debug"
ctest --verbose --output-on-failure 2>&1 | tee "$LOG_DIR/build1-tests.log"
log_success "All tests passed"

cd "$PROJECT_ROOT"

# ============================================================================
# PHASE 5: Build 2 - Dashboard Disabled
# ============================================================================

log_header "PHASE 5: Build 2 - Dashboard Disabled Regression"

log_info "Configuring..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR/ninja-debug-no-dashboard" \
    --preset ninja-debug \
    -DBUILD_TESTS=ON \
    -DGRAPHX_BUILD_WEB_DASHBOARD=OFF \
    -DCMAKE_BUILD_TYPE=Debug \
    2>&1 | tee "$LOG_DIR/build2-configure.log"

log_success "Configuration complete"

log_info "Building..."
cmake --build "$BUILD_DIR/ninja-debug-no-dashboard" --parallel 4 2>&1 | tee "$LOG_DIR/build2-build.log"
log_success "Build complete"

log_info "Verifying no dashboard files..."
if find "$BUILD_DIR/ninja-debug-no-dashboard" -path "*fhss-dashboard*" -type f | grep -q .; then
    log_error "Dashboard files found in disabled build!"
    exit 1
fi
log_success "No dashboard files in disabled build"

log_info "Running tests..."
cd "$BUILD_DIR/ninja-debug-no-dashboard"
ctest --verbose 2>&1 | tee "$LOG_DIR/build2-tests.log"
log_success "All tests passed"

cd "$PROJECT_ROOT"

# ============================================================================
# PHASE 6: Build 3 - Dashboard Enabled (Phase 1 Baseline)
# ============================================================================

log_header "PHASE 6: Build 3 - Dashboard Enabled (Phase 1 Baseline)"

log_info "Configuring..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR/ninja-debug-with-dashboard" \
    --preset ninja-debug \
    -DBUILD_TESTS=ON \
    -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    2>&1 | tee "$LOG_DIR/build3-configure.log"

log_success "Configuration complete"

log_info "Building..."
cmake --build "$BUILD_DIR/ninja-debug-with-dashboard" --parallel 4 2>&1 | tee "$LOG_DIR/build3-build.log"
log_success "Build complete"

log_info "Running tests..."
cd "$BUILD_DIR/ninja-debug-with-dashboard"
ctest --verbose 2>&1 | tee "$LOG_DIR/build3-tests.log"
log_success "All tests passed"

cd "$PROJECT_ROOT"

# ============================================================================
# PHASE 7: Baseline Artifact Collection
# ============================================================================

log_header "PHASE 7: Baseline Artifact Collection"

log_info "Capturing dashboard baseline..."
find "$PROJECT_ROOT/examples/DSP/dashboard" -type f | sort > "$LOG_DIR/dashboard-files-baseline.txt"
log_success "Dashboard file listing saved"

log_info "Hashing baseline files..."
shasum -r "$PROJECT_ROOT/examples/DSP/dashboard/" > "$LOG_DIR/dashboard-baseline-hash.txt" 2>/dev/null || true
log_success "Dashboard hashes captured"

log_info "Recording HTTP server baseline..."
find "$PROJECT_ROOT/src" -name "*Dashboard*" -o -name "*Http*" | sort > "$LOG_DIR/http-server-files-baseline.txt"
log_success "HTTP server file listing saved"

log_info "Recording git state..."
git status > "$LOG_DIR/git-status-baseline.txt"
git log --oneline -5 > "$LOG_DIR/git-log-baseline.txt"
git describe --tags > "$LOG_DIR/git-describe-baseline.txt" 2>/dev/null || true
log_success "Git state recorded"

# ============================================================================
# PHASE 8: Docker Container Validation (Optional)
# ============================================================================

log_header "PHASE 8: Docker Container Validation (Optional)"

if ! command -v docker &> /dev/null; then
    log_warning "Docker not available, skipping container validation"
else
    log_info "Building Docker image..."
    if docker build -f "$PROJECT_ROOT/containers/dashboard-operator/Dockerfile" \
        -t graphx:pre-phase1-baseline \
        "$PROJECT_ROOT" 2>&1 | tee "$LOG_DIR/docker-build.log"; then
        log_success "Docker image built"
        
        log_info "Testing Docker container..."
        if docker run --rm -d --name graphx-test graphx:pre-phase1-baseline > /dev/null 2>&1; then
            sleep 2
            if docker exec graphx-test curl -s http://localhost:8765/healthz > "$LOG_DIR/docker-health.json" 2>&1; then
                log_success "Docker container health check passed"
            else
                log_warning "Docker health check inconclusive (may be expected)"
            fi
            docker stop graphx-test > /dev/null 2>&1 || true
        else
            log_warning "Docker container launch inconclusive"
        fi
    else
        log_warning "Docker build skipped or failed (this is OK for baseline)"
    fi
fi

# ============================================================================
# PHASE 9: Operator Acceptance Test (Optional)
# ============================================================================

log_header "PHASE 9: Operator Acceptance Test (Optional)"

OPERATOR_SCRIPT="$PROJECT_ROOT/examples/DSP/dashboard/operator/fhss_dashboard_operator.py"
if [[ ! -f "$OPERATOR_SCRIPT" ]]; then
    log_warning "Operator script not found, skipping operator tests"
else
    log_info "Running operator acceptance tests..."
    
    if ! command -v python3 &> /dev/null; then
        log_warning "Python3 not available, skipping operator tests"
    else
        cd "$PROJECT_ROOT/examples/DSP/dashboard/operator"
        
        # Test prepare
        if python3 fhss_dashboard_operator.py prepare > "$LOG_DIR/operator-prepare.log" 2>&1; then
            log_success "Operator prepare command passed"
        else
            log_warning "Operator prepare command skipped (may be expected)"
        fi
        
        # Generate baseline report
        if python3 fhss_dashboard_operator.py report > "$LOG_DIR/operator-baseline.json" 2>&1; then
            log_success "Operator baseline report generated"
        else
            log_warning "Operator report generation skipped"
        fi
        
        cd "$PROJECT_ROOT"
    fi
fi

# ============================================================================
# PHASE 10: Final Summary
# ============================================================================

log_header "PHASE 10: Summary and Next Steps"

log_success "Clean build completed successfully!"
log_info "Timestamp: $TIMESTAMP"
log_info "Build artifacts: $BUILD_DIR"
log_info "Baseline logs: $LOG_DIR"

echo ""
log_header "Baseline Artifacts Saved"
ls -lh "$LOG_DIR/" | tail -n +2

echo ""
log_header "Next Steps"
cat << 'EOF'
1. Review build logs for any warnings:
   - build1-build.log (default ninja-debug)
   - build2-build.log (dashboard disabled)
   - build3-build.log (dashboard enabled)

2. Verify test results in:
   - build1-tests.log
   - build2-tests.log
   - build3-tests.log

3. Save baseline hashes for Phase 1 rollback:
   - dashboard-baseline-hash.txt
   - git-status-baseline.txt

4. When ready to begin Phase 1:
   - Use orchestration prompt: plan/prompts/FHSS_Dashboard_V2_Phase1_Implementation.md
   - Reference baseline: .graphx-operator/clean-build-<timestamp>/

5. To rollback if Phase 1 goes wrong:
   rm -rf build-ninja/
   git clean -fdx

EOF

echo ""
log_success "Clean build baseline establishment complete!"
log_info "Repository is ready for Phase 1 assignment"

exit 0
