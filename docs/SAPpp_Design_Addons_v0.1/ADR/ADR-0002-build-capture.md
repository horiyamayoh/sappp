# ADR-0002: Build Capture は compile_commands + ラッパ方式を採用

- Status: Accepted
- Date: 2026-01-17

## Context
TU解析の前提（マクロ、include、標準、ターゲット、ABI等）が揃わないと Soundness の土台が崩れる。
CMake と Make を広くサポートする必要がある。

## Decision
- CMake: `compile_commands.json` を入力として取り込む（`CMAKE_EXPORT_COMPILE_COMMANDS=ON`）。
- Make 等: コンパイララッパ（`sappp-cc` / `sappp-cxx`）で実行されたコンパイルを収集し build_snapshot を生成する。
- response file（.rsp）は **展開後 argv を保存**し、元 .rsp の path/hash も記録する。

## Consequences
- 解析の再現性（pack）に必要なビルド前提を保持できる。
- Windows/MSVC では `clang-cl` / response file の扱いが難所になり得る（別ADRで補足）。

## References
- SRS v1.1: REQ-IN-001/002, REQ-REP-001/002
