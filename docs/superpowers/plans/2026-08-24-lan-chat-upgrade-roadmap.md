# LAN Chat Upgrade Roadmap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` or `superpowers:subagent-driven-development` to implement one approved phase at a time. Steps use checkbox syntax for tracking.

**Goal:** Evolve LAN Chat into a dependable Windows chat application that works on a LAN, through a virtual LAN, or via an optional TCP tunnel, without weakening its local-first TLS model.

**Architecture:** Keep `React -> QWebChannel/ChatBridge -> GuiChatController/GuiConnectionWorker -> TLS/TCP -> Go Hub/SQLite` as the only data path. Each phase adds a narrow protocol and state contract, exposes it through `ChatBridge`, renders it in React, and is independently testable and releasable.

**Tech Stack:** Go + SQLite, C++20 + Qt 6 WebEngine/QWebChannel/OpenSSL, React + TypeScript + Vite, PowerShell packaging, Inno Setup.

**Spec:** User-approved product direction from this conversation; no separate design specification exists yet.

## Global Constraints

- Do not add member invitations: admission is by direct address, virtual-LAN address, or tunnel address, followed by the existing host confirmation flow.
- Do not add administrator roles, role-based moderation, blacklist management, mute controls, or an audit-management interface. The room owner only approves or rejects connection requests.
- Do not add a dedicated accessibility workstream.
- Do not add file, image, audio, or video upload/transfer.
- Never package or publish `server-lan.key`, `chat.db`, chat history, access tokens, or a host certificate. Only a public CA certificate may be distributed to a member when manual connection is required.
- Preserve the current `ChatBridge` boundary: React never opens sockets, handles TLS/private keys, starts server processes, or writes SQLite.
- Use short `transform`/`opacity` transitions only; do not reintroduce per-message staggered animation, large blur layers, or a release performance overlay.
- Before each phase, preserve unrelated working-tree changes and create a phase branch or commit boundary only after the user approves that phase.

---

## Delivery Order

| Phase | Deliverable | Dependency | Release gate |
|---|---|---|---|
| 0 | Baseline and recovery test harness | Current working tree | Existing tests green and current installer can be reproduced |
| 1 | Automatic reconnect and session recovery | 0 | 已通过双机验收；Wi-Fi/IP interruption restores a session without GUI freeze |
| 2 | Delivery states, idempotent retry, and history search | 1 | 已通过验收；Each message has a stable outcome; search survives restart |
| 3 | Skipped: persistent administration | — | Not in product scope |
| 4 | Cross-network onboarding: Radmin VPN first, Sakura Frp second | 1 and 2 | 4A 已完成；4B 搁置，保留文档和实现但不作为当前发布阻塞项 |
| 5 | Skipped: automatic updates and signed manifests | — | Users download releases manually from GitHub |
| 6 | Everyday experience and scale | 1 and 2 | Large-history continuity, bounded memory, and validated 60–90 FPS operation |
| 7 | Skipped: true P2P feasibility | — | Not in product scope |

## Phase 0: Baseline and Recovery Harness

**Files:**

- Modify: `docs/CODEX_HANDOFF.md`, `docs/testing.md`, `scripts/build-modern.ps1`, `scripts/package-installer.ps1`.
- Add: `docs/release-checklist.md`.

- [ ] Record the current executable version, ZIP hash, installer hash, and `git status --short` in the release checklist; do not stage unrelated user changes.
- [ ] Add a repeatable test recipe for: host startup, a second client join, host approval, local disconnect, reconnect, and installer smoke launch.
- [ ] Run `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test` and retain its output with the checklist.
- [ ] Build the unified ZIP and installer; inspect the archive to prove `.key`, `.crt`, `.db`, source, and build tools are excluded.
- [ ] Commit only the checklist/documentation changes as `docs: add release recovery checklist` after approval.

**Acceptance:** a future phase can reproduce a known-good baseline and distinguish a code regression from stale package artifacts.

## Phase 1: Automatic Reconnect and Session Recovery

**Status: Passed manual acceptance.** Keep the reconnection evidence with the
release checklist; any newly observed reconnect regression reopens this phase.

Before the reconnect work, make the two independent LAN admission checks explicit in the UI: the member verifies the host public certificate/fingerprint to prevent a spoofed host, while the host separately approves or rejects the member connection. Discovery wording must not present certificate verification as permission to join; after the member sends a request, show `等待房主批准` until the server returns a final decision.

**Files:**

- Modify: `client-cpp/gui/src/gui_connection_worker.hpp`, `client-cpp/gui/src/gui_connection_worker.cpp`, `client-cpp/gui/src/gui_chat_controller.hpp`, `client-cpp/gui/src/gui_chat_controller.cpp`, `client-cpp/gui/src/chat_bridge.cpp`, `frontend/src/bridge/types.ts`, `frontend/src/app/App.tsx`, `frontend/src/components/ChatHeader.tsx`.
- Add: `client-cpp/gui/tests/connection_recovery_tests.cpp`, `frontend/src/app/ConnectionRecovery.test.tsx`.

- [ ] Write a failing C++ test for a retry scheduler that produces bounded exponential delays, resets after a successful login, and never retries after an explicit user disconnect.
- [ ] Implement a `ReconnectPolicy` owned by `GuiConnectionWorker`; it stores only the already-approved connection parameters and a generation number, never a private key or raw credentials in React.
- [ ] Emit bridge states `reconnecting`, `reconnect_attempt`, `reconnect_failed`, and `reconnected`; have `GuiChatController` request fresh history for the active conversation after reconnection.
- [ ] Write a failing React test that expects a reconnecting status, preserves the open conversation and draft, and removes the status after `reconnected`.
- [ ] Add the smallest UI status treatment in `ChatHeader` and the connection page; do not show a blocking modal during scheduled retries.
- [ ] Run the two focused tests, then the full CTest/Vitest suites; manually interrupt the host network and verify recovery on two machines.
- [ ] Commit as `feat: recover chat sessions after transient disconnects`.

**Acceptance:** a temporary network loss does not freeze the UI or lose the active conversation/draft; an intentional Disconnect button stops retries immediately.

## Phase 2: Delivery States, Idempotent Retry, and Search

**Status: Passed manual acceptance.** Preserve the two-client and restart
evidence with the release checklist; any duplicate, missing receipt, or search
regression reopens this phase.

**Files:**

- Modify: `server-go/message.go`, `server-go/protocol.go`, `server-go/client.go`, `server-go/hub.go`, `server-go/auth_store.go`, `client-cpp/gui/src/gui_connection_worker.cpp`, `client-cpp/gui/src/gui_chat_controller.cpp`, `client-cpp/gui/src/chat_model.cpp`, `client-cpp/gui/src/chat_bridge.cpp`, `frontend/src/bridge/types.ts`, `frontend/src/components/MessageItem.tsx`, `frontend/src/components/MessageTimeline.tsx`.
- Add: `server-go/delivery_test.go`, `server-go/history_search_test.go`, `client-cpp/gui/tests/delivery_state_tests.cpp`, `frontend/src/components/MessageDelivery.test.tsx`, `frontend/src/components/MessageSearch.test.tsx`.

- [ ] Write a failing Go test showing that two sends with the same client-generated `message_id` create one stored message and return the same delivery outcome.
- [ ] Add `queued`, `sent`, `delivered`, and `failed` fields to the protocol message envelope; make the SQLite message-id column unique for the sender/conversation scope.
- [ ] Implement explicit receipt messages from the Hub and pass them through `GuiConnectionWorker` into the model instead of treating a socket write as delivery.
- [ ] Write a failing C++ model test that a retry changes a failed outgoing row into `sent` without creating a duplicate timeline row.
- [ ] Add a bounded retry action for failed messages, preserving the original message ID; do not retry recalled or locally deleted messages.
- [ ] Write a failing Go search test for Unicode text, room/private scope, result limit, and cursor pagination; add indexed SQLite queries and a `history_search_request`/`history_search_response` pair.
- [ ] Add a React search affordance that presents results, scrolls to the matching message, and keeps unread/new-message behavior continuous.
- [ ] Run Go race tests, CTest, Vitest, restart-history verification, and a two-client duplicate-send test.
- [ ] Commit as `feat: add reliable delivery states and chat search`.

**Acceptance:** users can distinguish a local queue, server acceptance, failure, and retry; searches work after server restart and do not duplicate messages.

## Phase 3: Skipped — Persistent Administration

Administrator roles, role persistence, mute/blacklist controls, and audit-management UI are explicitly out of scope. The existing room-owner connection approval flow remains the sole admission control.

## Phase 4: Cross-Network Onboarding

### 4A — Radmin VPN / Virtual LAN

**Files:**

- Modify: `server-go/lan_discovery.go`, `frontend/src/app/App.tsx`, `docs/release-setup.md`.
- Add: `server-go/lan_discovery_test.go`, `docs/cross-network-radmin.md`.

- [x] Write failing tests proving virtual-LAN IPv4 addresses are announced while loopback, link-local, unspecified, and host-route addresses are excluded.
- [x] Keep directed UDP discovery broadcast on every eligible IPv4 interface, including active Radmin/virtual-LAN adapters; never announce or copy a private key.
- [x] Keep the member discovery page scanning continuously while it is open, so a room that starts later or changes virtual-network IPv4 appears automatically.
- [x] Add a guide: install virtual-LAN software outside LAN Chat, join the same virtual network, rely on automatic discovery first, and use manual connection only as a fallback; preserve certificate verification and host approval.
- [x] Verify two PCs on different physical networks join over Radmin VPN using automatic discovery; retain manual direct connection as the fallback.

### 4B — Sakura Frp TCP Tunnel

**Status: Deferred by product decision.** The existing guide and manual tunnel
entry remain available, but no new Frp work or release gate is scheduled until
the user explicitly resumes this phase.

**Files:**

- Modify: `frontend/src/app/App.tsx`, `frontend/src/app/SettingsPage.tsx`, `frontend/src/bridge/types.ts`, `docs/release-setup.md`.
- Add: `frontend/src/app/TunnelConnectionGuide.test.tsx`, `docs/cross-network-sakura-frp.md`.

- [x] Write a failing React test for a tunnel setup guide that only accepts a host/port and a public CA certificate path; it must never render a tunnel token or private key field.
- [x] Add a documented, manual `frpc` setup whose local target is `127.0.0.1:8888`; do not bundle third-party tunnel binaries or persist provider tokens in LAN Chat.
- [x] Add a remote-connect profile for `public-node:remote-port`, preserving the usual certificate verification and host approval.
- [x] Document mandatory tunnel-side authentication/IP allowlisting, Windows firewall scope, CA distribution, and token storage in the provider tool rather than the chat application.
- [ ] Verify a member can connect through a test TCP tunnel while LAN discovery is unavailable.

**Acceptance:** Radmin VPN requires no protocol change; Frp exposes only a TLS-protected public endpoint and never packages or displays secret tunnel credentials.

## Phase 5: Skipped — Automatic Updates and Signed Manifests

The user chose manual updates through the fixed GitHub repository. Do not add a client-side update check, background GitHub request, update manifest, release-signing key, Authenticode workstream, download prompt, or binary self-replacement. A future release may revisit this phase only with explicit approval and a separately managed signing key.

## Phase 6: Everyday Experience and Large-History Performance

**Status: Implementation and automated verification complete; reference-machine
benchmark acceptance is deferred by user decision.** The automated gate must not
be treated as a substitute for an interactive hardware run. See
`docs/p6-performance-validation.md`.

### Reference machine and scope

Use the following machine as the primary Windows benchmark reference:

- CPU: Intel Core i9-14900HX.
- GPU: NVIDIA GeForce RTX 4060 Laptop GPU.
- Memory: 16 GB (two 8 GB modules).
- Display: external 24-inch monitor. Record its actual resolution and refresh
  rate in every benchmark run; neither value is inferred from Task Manager.

The initial idle observation on this reference machine is **110.8 MB working
set and 0.1% CPU** for `lan-chat-gui.exe`, with no observed disk or network
throughput at the instant captured. It is a baseline observation only, not an
FPS or peak-memory result.

An external GamePP capture on 2026-08-25 reported `lan-chat-gui.exe` at about
**21.89 FPS average**, **11 FPS minimum**, **35 FPS maximum**, and **2 FPS 1%
low** over four minutes on the 1920×1080 240 Hz display. This is a diagnostic
lead, not a P6 gate result: Qt WebEngine renders the page in its separate
`QtWebEngineProcess.exe`, whereas the capture selected the parent process.
Before tuning, compare the in-app `requestAnimationFrame` telemetry, the
WebEngine graphics backend, and a short active interaction trace against this
parent-process measurement.

P6 is deliberately limited to memory, message-history continuity and frame
budget work. Do **not** add settings-page scroll animation. The following
ideas are out of current scope and may be reconsidered only in a later,
separately approved phase: desktop notifications/quiet hours, new keyboard
shortcut work, local avatars or profiles, and diagnostic-log export UI.

**Files:**

- Modify: `frontend/src/components/MessageTimeline.tsx`, `frontend/src/components/MessageItem.tsx`, `frontend/src/components/MessageComposer.tsx`, `frontend/src/components/IdentityCard.tsx`, `frontend/src/state/appSettings.ts`, `frontend/src/styles/global.css`, `client-cpp/gui/src/performance_profile.cpp`.
- Add: `frontend/src/components/MessageVirtualization.test.tsx`, `frontend/src/components/KeyboardShortcuts.test.tsx`, `frontend/src/app/NotificationSettings.test.tsx`.

- [x] Write a failing timeline test that 500+ historical rows render a bounded window while scroll position, unread counts, and new-message count remain correct.
- [x] Introduce simple windowed rendering before adding a third-party virtualization dependency; preserve initial-history no-animation and appended-message-only animation rules.
- [ ] Establish a repeatable 1920×1080 benchmark profile on a hardware-accelerated Windows reference machine: 2,000 retained messages, 10 room/private-conversation switches, 100 incoming messages, and five settings-to-chat transitions. Record warm-up working set, peak working set, P95/P99 frame time, and refresh rate before optimization.
- [x] Bound memory ownership at every history boundary: keep only the configured timeline window in React, cap `ChatListModel` rows per conversation, discard inactive history pages that can be reloaded from SQLite, and ensure WebEngine/QWebChannel snapshots do not retain replaced message arrays. Add regression tests for model row caps, conversation eviction, and repeated history replacement.
- [x] Add a frame-budget policy in `PerformanceProfile`: never render faster than 90 FPS, target a sustained 60 FPS minimum on the reference profile, and reduce animation work when P95 exceeds 16.7 ms. Do not busy-loop, force a display refresh rate, or expose the debug performance overlay in release builds.
- [x] Feed the modern React/WebEngine surface into the existing frame-budget policy with batched `requestAnimationFrame` samples; ignore invalid/background gaps and do not add a release performance overlay.
- [x] Add a benchmark harness that fails the Phase 6 gate when post-warm-up working set grows by more than 15% after the repeat profile, when P95 exceeds 16.7 ms during ordinary navigation, or when animation bursts exceed 33.3 ms more than once per transition. Treat software rendering, battery-saver mode, and a display below 60 Hz as explicitly reported unsupported benchmark conditions rather than false passes.
- [ ] Benchmark 500 and 2,000 messages with the existing sampler; make no release build include the debug performance overlay.
- [ ] Commit as `feat: improve everyday chat flow at large history sizes`.

**Acceptance:** on the defined hardware-accelerated benchmark profile, memory growth after warm-up stays within 15%, ordinary UI navigation sustains at least 60 FPS (P95 ≤ 16.7 ms), rendering remains within the 60–90 FPS operating range without a busy loop, animation bursts are bounded, the paper theme remains visually stable, and the settings page has no scroll animation.

## Phase 7: Skipped — True P2P Feasibility

P2P, NAT traversal, STUN/TURN, relay services, rendezvous infrastructure and
their associated threat model are out of product scope by user decision. Keep
the existing direct LAN, virtual-LAN and manually configured TLS tunnel routes;
do not begin P2P research or change the protocol for it unless the user
explicitly reopens this decision.

## Program Verification

- [ ] Every functional phase begins with a focused failing test and ends with focused tests plus `scripts/build-modern.ps1 -Action Test`.
- [ ] Network phases require a two-machine test on separate physical networks where relevant, with host approval observed from the host UI.
- [ ] Every release rebuilds the Vite assets and verifies generated Qt resource entries contain the current hashes.
- [ ] Every package is inspected for forbidden identity, database, certificate, key, source, SDK, and token files before delivery.
- [ ] No phase modifies the user’s existing uncommitted package/UI changes unless the user explicitly includes them in the phase commit.

## Execution Order

1. Phases 1 and 2 have passed acceptance; preserve their evidence in the release checklist.
2. Keep Phase 4A available. Phase 4B is deferred and must not block current releases.
3. Skip Phases 3 and 5.
4. P6 reference-machine sampling is deferred by user decision and does not block maintenance work.
5. Phase 7 is skipped; do not begin P2P research or implementation unless the user explicitly reopens it.
