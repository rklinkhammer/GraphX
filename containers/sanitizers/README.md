# Linux sanitizer validation container

This supported container runs GraphX validation under Linux AddressSanitizer,
LeakSanitizer, and UndefinedBehaviorSanitizer. It is independent of the legacy
FHSS dashboard operator image. Docker is optional: all host-native build and
dashboard workflows remain supported.

The image copies the current repository working tree, including uncommitted
files that are not excluded by `.dockerignore`. Build products and compiler
cache data use Docker-managed named volumes; they do not modify host build
directories or use `/private/tmp`. CMake is reconfigured with `--fresh` on
every run so a persisted cache cannot silently select stale options.

The container selects a console-only log4cxx configuration. This avoids the
Ubuntu package's uninstrumented rolling-file appender teardown crossing the
instrumented allocation boundary; GraphX code remains fully instrumented and
leak detection remains enabled.

Build the image from the repository root:

```bash
docker compose -f containers/sanitizers/compose.yaml build
```

Run the focused Phase 0/1/2 preservation and dashboard verification lanes:

```bash
docker compose -f containers/sanitizers/compose.yaml run --rm sanitizer focused
```

Run every enabled configured CTest plus the frontend checks:

```bash
docker compose -f containers/sanitizers/compose.yaml run --rm sanitizer full
```

The commands fail on any ASan, LeakSanitizer, UBSan, build, frontend, or test
failure. `GRAPHX_BUILD_JOBS` defaults to 2 and can be increased explicitly:

The sanitizer preset retains debug information but uses `-O1`, as recommended
for useful sanitizer execution speed and stack traces. Test deadlines remain
instrumentation-aware; native production deadlines are unchanged. The preset
also suppresses GCC 14's `maybe-uninitialized` false positive inside the
libstdc++ regular-expression implementation; project warnings remain enabled.

```bash
GRAPHX_BUILD_JOBS=4 docker compose \
  -f containers/sanitizers/compose.yaml run --rm sanitizer full
```

Use the native architecture reported by Docker. In particular, do not force
`linux/amd64` under emulation on Apple Silicon for sanitizer qualification;
emulation can distort timing and memory-runtime behavior. Run a separate native
AMD64 job when cross-architecture evidence is required.

This lane qualifies Linux CPU execution. It does not qualify macOS, Metal,
CUDA, browser rendering, external hardware, HWIL, conducted RF, OTA, or live RF.
