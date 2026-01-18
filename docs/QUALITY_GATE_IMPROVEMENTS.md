# Quality Gate Improvements

This document summarizes the quality-gate updates and the issues they address.

## Issues Addressed

- Local success but remote failure (clang-tidy parity and tool gating)
- clang-tidy strictness causing frequent friction
- Excessive repeat work on small changes
- Ad-hoc gate behavior without a single strategy

## Key Changes

- Added gate profiles: `quick`, `smart`, `full`, and `ci`.
- Default pre-commit mode is now `smart` (change-aware, faster feedback).
- `--ci` mode enforces CI parity and forbids skip flags.
- clang-tidy uses Clang compile_commands in CI to match local behavior.
- Docker CI can now write stamps (`--stamp`) for pre-push validation.
- Docker caching is enabled by default (ccache + host build). Disable with
  `--no-cache` or `--tmpfs` when isolation is preferred.
- Docker uses dedicated build dirs (`build-docker`, `build-docker-clang`) to
  avoid host/CI cache path mismatches.
- pre-push hook is treated as required (missing hook is flagged by check-env).
- Stamp now records mode, tidy scope, and skipped steps.
- Skipped steps are explicitly reported and downgrade full/ci stamps to partial.
- GitHub CI runs the same Docker gate script as local for parity.
- CI gate mode can be controlled via `SAPPP_CI_GATE_MODE` or workflow_dispatch.

## Compatibility Notes

- CI keeps all strict checks; local "smart" is intended for iteration speed.
- Use `make ci` or `./scripts/docker-ci.sh --ci` before push for parity.
- If tools are missing in `full/ci`, the check fails (no false success).
