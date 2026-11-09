# SolEngine Guidelines & AI Agent Rules

## CRITICAL POLICY: NO AUTOMATED TESTS OR BACKGROUND GPU/AUDIO PROCESSES

### 1. Absolute Prohibition of Automated Testing
- **Under NO circumstances should automated tests be run.**
- The user actively tests updates manually. Test execution is strictly reserved for the user unless explicitly and manually requested (e.g., "run tests now").
- Strictly prohibited from running automatically:
  - Unit test binaries (`solengine_tests.exe`, `*test*.exe`, `*diag*.exe`).
  - Test suites and test runners (`ctest`, `ninja test`).
  - Build script test flags (`.\build.ps1 -Test`).
  - Smoke tests or headless verification flags (`--smoke-test`, `--headless`).
  - Ad-hoc diagnostic or verification binaries.
- Do NOT run tests under the guise of "debugging", "diagnostics", or "verifying changes".

### 2. Windows System Stability and Background Process Safety
- Executables in this repository initialize hardware graphics contexts (WebGPU / `wgpu-native`, DirectX, Vulkan, OpenGL) or real-time audio threads. Running them in subshells or pushing them to background tasks hangs the process, leaks GPU/audio contexts, and stalls Windows and Antigravity IDE.
- If the user explicitly requests compiling or building, ALWAYS specify flags that inhibit running or launching:
  - When running `build.ps1`: pass `-NoRun -NoLaunch`.
  - When running `cmake --build`: only target specified non-test targets, and never execute the resulting binary.
