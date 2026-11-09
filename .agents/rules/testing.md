---
trigger: always_on
---

# CRITICAL POLICY: NO AUTOMATED TESTS OR BACKGROUND GPU/AUDIO PROCESSES

1. **Absolute Prohibition of Automated Testing**: Under NO circumstances should automated tests (`*test*.exe`, `ctest`, `.\build.ps1 -Test`, `--headless`, `--smoke-test`) be executed by the AI agent unless explicitly asked by the user.
2. **Windows GPU/Audio Process Safety**: Never launch executables that initialize WebGPU, `wgpu-native`, or audio devices from subshells. When compiling via `build.ps1`, always pass `-NoRun -NoLaunch`.
