# EXP-07 / Experiment for [[QAS-10](../04-quality-attribute-requirements.md#qas-10-pc-pi-platform-separation)]: Cross-platform build & deployment

## Objective

Confirm the same codebase builds and runs on the ARM Pi 5, x86 PC, and macOS (Intel / Apple Silicon)

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- Build and execution results for each platform (Windows, macOS, Linux PC, Raspberry Pi OS)

- Commits for any code changes made to enable builds on specific platforms

- Source code that can be built and executed on all of the above platforms

## Resources required

- **Software Tools and Frameworks**

  - Qt per platform (Qt Creator 19.0.2, Qt 6.11.1)

- **Hardware**

  - 1 Windows laptop

  - 1 macOS laptop

  - 1 Linux laptop

  - 1 Raspberry Pi board

- **Documentation and Reference Materials**

  - Qt official documentation (Cross-platform build guide)

  - Current project source code

## Experiment description

1. **Environment Setup:** Install Qt and Qt Creator on each platform and configure the environment.

2. **Source Code Checkout:** Clone the same source code on each platform.

3. **Build Attempt:** Run a CMake build on each platform and record whether it succeeds or fails.

4. **Issue Analysis and Fix:** If a build fails, analyze the root cause and resolve any platform compatibility issues.

5. **Execution Verification:** After a successful build, run the application and verify that core functionality works as expected.

## Duration

05/25–06/08

## Links and references

[QAS-10](../04-quality-attribute-requirements.md#qas-10-pc-pi-platform-separation) · [RISK-16](../06-risk-management.md#risk-16) · CON-SW-03

## Results and recommendations

- (05/28) Confirmed that Raspberry Pi OS and Windows 11 can build and run using the source code provided at the beginning of the project.

- (05/29) Code changes required for Linux PC (Ubuntu 24.04) build: <https://github.com/jaehong179/architect_cmu/commit/b0f4b338931187cdc8b0ced1155b8c82707164e3>

- (06/07) Initial macOS (Apple Clang) build succeeded, but waveform rendering crashed at runtime.

- (06/07) Root cause: QCustomPlot::getOptimizedScatterData() — valuePixelSpan == 0 causes std::lround(infinity) → int overflow → UB crash. Latent on Linux/Windows, triggered on Mac (Apple Clang). (patch : [https://github.com/jaehong179/architect_cmu/commit/0817867d70d04d698b8ad872f4d20b20d8b5f582](https://github.com/jaehong179/architect_cmu/commit/0817867d70d04d698b8ad872f4d20b20d8b5f582))

- (06/08) The code from the latest commit can be built and executed on all required operating systems.
