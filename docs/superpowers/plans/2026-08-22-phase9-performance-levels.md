# Phase 9 Performance Levels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish Phase 8 with release-boundary verification and add a persisted, renderer-aware performance level policy for the Qt Quick UI.

**Architecture:** Keep `PerformanceSampler` and `PerformanceOverlay.qml` debug-only. Add a small always-available `PerformanceProfile` QObject that converts the user mode plus `GraphicsInfo` and optional frame-time observations into stable UI capability flags. QML consumes those flags to gate decorative effects and motion without changing chat/network behavior.

**Tech Stack:** Qt 6 C++, Qt Quick/QML, QSettings, Qt Test, CMake.

**Spec:** `docs/ui-modernization-plan.md`, Phase 8 and Phase 9 entries.

## Global Constraints

- Supported modes are exactly `Automatic`, `High`, `Balanced`, and `Power Saving`.
- Automatic decisions use renderer/software-rendering state, refresh rate, and observed frame time; never GPU vendor/model names.
- Debug performance overlay remains enabled only by `LAN_CHAT_ENABLE_PERF_OVERLAY=ON`.
- Default/release builds must not compile or package `PerformanceSampler` or `PerformanceOverlay.qml`.
- No per-message shadow, blur, or continuous shader effect is introduced.
- QML changes are limited to visual capability bindings; connection, model, and protocol behavior remain unchanged.

---

### Task 1: Close Phase 8 with regression coverage

**Files:**
- Modify: `client-cpp/gui/tests/performance_sampler_tests.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`
- Modify: `docs/ui-modernization-plan.md`

- [x] **Step 1: Add tests for bounded sampling and debug-only ownership.**
- [x] **Step 2: Run the focused sampler tests and confirm the existing implementation passes.**
- [x] **Step 3: Verify default CMake configuration does not list sampler sources/QML.**
- [x] **Step 4: Record Phase 8 completion criteria and evidence in the phase document.**

### Task 2: Define the Phase 9 policy contract

**Files:**
- Create: `client-cpp/gui/src/performance_profile.hpp`
- Create: `client-cpp/gui/src/performance_profile.cpp`
- Create: `client-cpp/gui/tests/performance_profile_tests.cpp`
- Modify: `client-cpp/gui/CMakeLists.txt`

**Interfaces:**
- `QString mode() const`, `void setMode(const QString&)`
- `QString effectiveMode() const`
- `bool effectsEnabled() const`, `bool animationsEnabled() const`, `bool gradientsEnabled() const`
- `double animationDurationScale() const`
- `void updateGraphicsContext(bool hardwareAcceleration, bool softwareRendering, double refreshRate)`
- `void observeFrameTime(double frameMs)`

- [x] **Step 1: Write tests for explicit mode mapping, invalid mode fallback, and Automatic decisions.**
- [x] **Step 2: Run the new tests and confirm they fail because the policy does not exist.**
- [x] **Step 3: Implement the smallest policy that satisfies the tests.**
- [x] **Step 4: Run the policy tests and the existing graphics/sampler tests.**

### Task 3: Wire policy into the GUI and settings

**Files:**
- Modify: `client-cpp/gui/src/main.cpp`
- Modify: `client-cpp/gui/qml/Main.qml`
- Modify: `client-cpp/gui/qml/pages/SettingsPage.qml`
- Modify: `client-cpp/gui/qml/theme/Theme.qml`
- Modify: `client-cpp/gui/qml/pages/ChatPage.qml`

- [x] **Step 1: Expose `PerformanceProfile` as a QML context property and feed graphics/frame observations.**
- [x] **Step 2: Add the four-mode settings selector with Chinese labels and persisted selection.**
- [x] **Step 3: Gate only decorative gradients/glows and nonessential motion with policy flags.**
- [x] **Step 4: Keep the overlay conditional on the existing debug compile flag.**

### Task 4: Verify P8 closure and P9 behavior

**Files:**
- Modify: `docs/ui-modernization-plan.md`
- Modify: `docs/testing.md`

- [x] **Step 1: Build and run all Qt GUI tests in the debug diagnostic build.**
- [x] **Step 2: Build the default GUI and verify sampler/overlay symbols and QML are absent from its build inputs.**
- [x] **Step 3: Launch the default GUI and diagnostic GUI for smoke tests.**
- [x] **Step 4: Record P8 closed and P9 first slice complete, including known limitations.**

### Task 5: Continue P9 with low-cost runtime observation

**Files:**
- Modify: `client-cpp/gui/src/performance_profile.hpp`
- Modify: `client-cpp/gui/src/performance_profile.cpp`
- Modify: `client-cpp/gui/src/main.cpp`
- Modify: common QML controls and chat components with visual Behaviors
- Modify: `docs/ui-modernization-plan.md`

- [x] **Step 1: Bound the automatic frame-time window and expose its count for regression tests.**
- [x] **Step 2: Sample the attached `QQuickWindow` every four swapped frames in all builds.**
- [x] **Step 3: Gate common color, opacity, and scale Behaviors from the selected performance policy.**
- [x] **Step 4: Rebuild default and diagnostic targets, run 9/9 tests in each, and smoke-test both GUIs.**

### Task 6: Calibrate and stabilize Automatic mode

**Files:**
- Modify: `client-cpp/gui/src/performance_profile.hpp`
- Modify: `client-cpp/gui/src/performance_profile.cpp`
- Modify: `client-cpp/gui/tests/performance_profile_tests.cpp`
- Modify: `client-cpp/gui/qml/pages/SettingsPage.qml`
- Modify: `client-cpp/gui/qml/controls/PerformanceOverlay.qml`
- Modify: `docs/ui-modernization-plan.md`
- Create: `docs/p9-performance-calibration.md`

**Interfaces:**
- `PerformanceProfile` exposes `observedFps`, `observedP95FrameMs`, `observedMaxFrameMs`, and `automaticReason` as read-only QML properties.
- Automatic mode confirms a changed frame-time candidate across three successive observations before applying it, while renderer and refresh-rate hard limits remain immediate.

- [x] **Step 1: Add failing tests for metrics, reason reporting, and automatic hysteresis.**
- [x] **Step 2: Run the focused profile test and verify the new assertions fail.**
- [x] **Step 3: Implement bounded metrics and three-observation automatic hysteresis.**
- [x] **Step 4: Add the metrics/reason display to Settings and the diagnostic overlay.**
- [x] **Step 5: Document the calibration procedure and rerun both build matrices.**
