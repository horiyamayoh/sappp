# Quality Gate Strategy (SAP++)

This document defines the quality gate tiers, their intent, and the recommended
commands for local and CI use. The goal is deterministic, CI-parity checks while
keeping developer feedback fast and avoiding duplicate work.

## Goals

- CI parity: local "full/ci" checks mirror remote CI behavior.
- Fast feedback: "quick" and "smart" modes reduce turnaround time.
- Determinism: warnings-as-errors, determinism tests, and schema validation
  are always enforced in CI mode.
- Transparency: stamp metadata records mode, tidy scope, and skipped steps.

## Gate Levels

- L0 (Quick): fast checks for active development.
  - Format on changed files
  - Incremental GCC build + quick tests
  - Command: `./scripts/quick-check.sh`

- L1 (Smart): change-aware checks before committing.
  - Format on changed files
  - GCC build + full tests (only if build-relevant files changed)
  - Determinism tests (only if build-relevant files changed)
  - clang-tidy on changed C++ (full when headers change)
  - Schema validation only when schemas change
  - Command: `./scripts/pre-commit-check.sh --smart`

- L2 (CI): strict, CI-equivalent checks.
  - Format on all C++ sources
  - GCC build + full tests
  - Determinism tests
  - Clang build + tests
  - clang-tidy on all `libs/**/*.cpp`
  - Schema validation
  - Command: `./scripts/pre-commit-check.sh --ci` or
    `./scripts/docker-ci.sh --ci`

CI uses the same Docker-based gate as local development to prevent drift.
The mode can be controlled with `SAPPP_CI_GATE_MODE` (`smart`/`full`/`ci`)
or via workflow_dispatch input.

## Stamps

Successful checks write a stamp file (default: `.git/sappp/ci-stamp.json`) with
these fields:

- `check_mode`: quick/smart/full/ci/partial
- `tidy_scope`: changed/all
- `skipped_steps`: comma-delimited skipped steps

The pre-push check validates the stamp and rejects modes not in its allow-list
(default: `full,ci`). Override via:

- `SAPPP_PRE_PUSH_REQUIRE=full,ci,smart`
- `./scripts/pre-push-check.sh --require=full,ci,smart`

## Recommended Workflow

1. `make quick` while iterating
2. `make smart` before committing (default pre-commit hook)
3. `make ci` before pushing or when CI parity is required

## Docker Usage

- CI parity: `./scripts/docker-ci.sh --ci`
- Stamp write: `./scripts/docker-ci.sh --stamp`
- Cache for speed (local only): `./scripts/docker-ci.sh --cache`
