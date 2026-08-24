# LAN Chat Upgrade Roadmap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` or `superpowers:subagent-driven-development` to implement one approved phase at a time. Steps use checkbox syntax for tracking.

**Goal:** Evolve LAN Chat into a dependable Windows chat application that works on a LAN, through a virtual LAN, or via an optional TCP tunnel, without weakening its local-first TLS model.

**Architecture:** Keep `React -> QWebChannel/ChatBridge -> GuiChatController/GuiConnectionWorker -> TLS/TCP -> Go Hub/SQLite` as the only data path. Each phase adds a narrow protocol and state contract, exposes it through `ChatBridge`, renders it in React, and is independently testable and releasable.

**Tech Stack:** Go + SQLite, C++20 + Qt 6 WebEngine/QWebChannel/OpenSSL, React + TypeScript + Vite, PowerShell packaging, Inno Setup.

**Spec:** User-approved product direction from this conversation; no separate design specification exists yet.

## Global Constraints

- Do not add member invitations: admission is by direct address, virtual-LAN address, or tunnel address, followed by the existing host confirmation flow.
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
| 1 | Automatic reconnect and session recovery | 0 | Wi-Fi/IP interruption restores a session without GUI freeze |
| 2 | Delivery states, idempotent retry, and history search | 1 | Each message has a stable outcome; search survives restart |
| 3 | Persistent roles, audit trail, blacklist, and rate protection | 2 | Restart preserves authority and audit data |
| 4 | Cross-network onboarding: Radmin VPN first, Sakura Frp second | 1 and 3 | Manual address connection is secure and documented |
| 5 | Release trust: signed manifests and in-app update check | 0 | Update metadata is verified before prompting the user |
| 6 | Everyday experience and scale | 1 and 2 | Notifications, shortcuts, profile visuals, and large-history performance |
| 7 | Optional true P2P feasibility phase | 3 and 4 | Go/no-go design decision, not an automatic implementation commitment |

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

## Phase 3: Persistent Administration and Abuse Protection

**Files:**

- Modify: `server-go/auth_store.go`, `server-go/hub.go`, `server-go/client.go`, `server-go/message.go`, `client-cpp/gui/src/gui_chat_controller.cpp`, `client-cpp/gui/src/chat_bridge.cpp`, `frontend/src/app/WorkspacePage.tsx`, `frontend/src/components/MemberPanel.tsx`.
- Add: `server-go/audit_store_test.go`, `server-go/role_persistence_test.go`, `server-go/rate_limit_test.go`, `frontend/src/app/AdminAudit.test.tsx`.

- [ ] Write failing SQLite migration tests for persistent room roles, mute state, blacklist entries, and append-only audit events.
- [ ] Add database tables `room_roles`, `moderation_state`, `blocked_identities`, and `audit_events`; use transactions so a moderation action and its audit row succeed or fail together.
- [ ] Make Go authorization read persisted state after restart; preserve the existing host-only connection approval rule and do not add an invitation path.
- [ ] Add per-IP and per-identity connection/message rate limits with retry-after errors; log no passwords, private keys, or certificate bodies.
- [ ] Write tests that a blocked identity cannot reconnect, a muted user cannot send, and an administrator restart does not reset roles or audit history.
- [ ] Expose a read-only audit screen and blacklist management through `ChatBridge`; require confirmation for destructive actions.
- [ ] Run Go race/vet/tests plus a restart test using a real temporary SQLite database.
- [ ] Commit as `feat: persist moderation policy and audit events`.

**Acceptance:** moderation authority and evidence survive restart; direct-address admission remains host-approved and abusive retries are throttled.

## Phase 4: Cross-Network Onboarding

### 4A — Radmin VPN / Virtual LAN

**Files:**

- Modify: `client-cpp/gui/src/lan_discovery_service.cpp`, `client-cpp/gui/src/chat_bridge.cpp`, `frontend/src/app/App.tsx`, `frontend/src/app/SettingsPage.tsx`, `docs/release-setup.md`.
- Add: `client-cpp/gui/tests/virtual_lan_address_tests.cpp`, `frontend/src/app/CrossNetworkGuide.test.tsx`, `docs/cross-network-radmin.md`.

- [ ] Write a failing C++ test for selecting non-loopback IPv4 addresses from active adapters while excluding link-local and unspecified addresses.
- [ ] Add a host-only “copy virtual-LAN connection details” command that exposes a selected Radmin/other virtual-adapter address, port, and certificate fingerprint—not a private key.
- [ ] Add a guide in the existing connection flow: install the virtual-LAN software outside LAN Chat, join the same virtual network, then connect with the copied address and preserve host approval.
- [ ] Verify two PCs on different physical networks join over Radmin VPN without relying on UDP discovery; retain manual direct connection as the fallback.

### 4B — Sakura Frp TCP Tunnel

**Files:**

- Modify: `frontend/src/app/App.tsx`, `frontend/src/app/SettingsPage.tsx`, `frontend/src/bridge/types.ts`, `docs/release-setup.md`.
- Add: `frontend/src/app/TunnelConnectionGuide.test.tsx`, `docs/cross-network-sakura-frp.md`.

- [ ] Write a failing React test for a tunnel setup guide that only accepts a host/port and a public CA certificate path; it must never render a tunnel token or private key field.
- [ ] Add a documented, manual `frpc` setup whose local target is `127.0.0.1:8888`; do not bundle third-party tunnel binaries or persist provider tokens in LAN Chat.
- [ ] Add a remote-connect profile for `public-node:remote-port`, preserving the usual certificate verification and host approval.
- [ ] Document mandatory tunnel-side authentication/IP allowlisting, Windows firewall scope, CA distribution, and token storage in the provider tool rather than the chat application.
- [ ] Verify a member can connect through a test TCP tunnel while LAN discovery is unavailable.

**Acceptance:** Radmin VPN requires no protocol change; Frp exposes only a TLS-protected public endpoint and never packages or displays secret tunnel credentials.

## Phase 5: Release Trust and Update Checks

**Files:**

- Modify: `scripts/package-unified-release.ps1`, `scripts/package-installer.ps1`, `installer/LANChat.iss`, `client-cpp/gui/src/launcher_main.cpp`, `client-cpp/gui/src/chat_bridge.cpp`, `frontend/src/app/SettingsPage.tsx`.
- Add: `client-cpp/gui/src/update_manifest.hpp`, `client-cpp/gui/src/update_manifest.cpp`, `client-cpp/gui/tests/update_manifest_tests.cpp`, `docs/release-signing.md`.

- [ ] Write a failing parser test for a versioned JSON manifest with artifact URL, SHA-256, minimum supported version, and detached signature metadata; reject malformed URLs, downgrades, and unsigned data.
- [ ] Add an opt-in update check that downloads only a small manifest over HTTPS, compares semantic versions, and opens the verified release URL in the system browser; it does not self-replace binaries in this phase.
- [ ] Add release SHA-256 files and a reproducible artifact manifest to the packaging scripts.
- [ ] Define Authenticode signing as an externally supplied release credential: the signing certificate must be in the release operator’s secure store and never committed, copied into packages, or exposed to React.
- [ ] Test manifest verification offline with fixed fixtures and packaging output with a clean temporary directory.
- [ ] Commit as `feat: add signed update metadata checks`.

**Acceptance:** update prompts are based on a verified manifest; automatic binary replacement and certificate procurement remain separate, explicit release decisions.

## Phase 6: Everyday Experience and Large-History Performance

**Files:**

- Modify: `frontend/src/components/MessageTimeline.tsx`, `frontend/src/components/MessageItem.tsx`, `frontend/src/components/MessageComposer.tsx`, `frontend/src/components/IdentityCard.tsx`, `frontend/src/state/appSettings.ts`, `frontend/src/styles/global.css`, `client-cpp/gui/src/performance_profile.cpp`.
- Add: `frontend/src/components/MessageVirtualization.test.tsx`, `frontend/src/components/KeyboardShortcuts.test.tsx`, `frontend/src/app/NotificationSettings.test.tsx`.

- [ ] Write a failing timeline test that 500+ historical rows render a bounded window while scroll position, unread counts, and new-message count remain correct.
- [ ] Introduce simple windowed rendering before adding a third-party virtualization dependency; preserve initial-history no-animation and appended-message-only animation rules.
- [ ] Add configurable desktop notifications for direct messages and mentions, with application-level throttling and a quiet-hours setting; do not notify for the currently focused conversation.
- [ ] Add documented shortcuts for focus composer, send, search, escape overlays, and jump to latest; do not override text-entry editing shortcuts.
- [ ] Add optional local avatar/profile presentation without uploading images or sending file data; store only a small local preference or server-approved text profile.
- [ ] Benchmark 500 and 2,000 messages with the existing sampler; make no release build include the debug performance overlay.
- [ ] Commit as `feat: improve everyday chat flow at large history sizes`.

**Acceptance:** the UI remains responsive under large history, notification noise is controlled, and the paper theme remains visually stable.

## Phase 7: True P2P Feasibility Gate

**Files:**

- Add: `docs/superpowers/specs/2026-08-24-p2p-connectivity-design.md`, `docs/p2p-threat-model.md`, `docs/p2p-feasibility-results.md`.

- [ ] Measure direct TCP/UDP reachability across at least two NAT types and a CGNAT/mobile connection; record success rate, latency, and relay fallback behavior.
- [ ] Define an ICE-like design with STUN for candidate discovery, a signed rendezvous service, direct encrypted data channels, and TURN/relay fallback.
- [ ] Threat-model candidate spoofing, host approval bypass, metadata exposure, abusive relay usage, and key rotation.
- [ ] Compare a maintained library/service against implementing the protocol in C++/Go; document licensing, operating cost, Windows support, and maintenance burden.
- [ ] Present a go/no-go decision to the user before changing the client/server protocol.

**Acceptance:** no P2P production code is started without measured feasibility, a relay cost model, and a separately approved architecture.

## Program Verification

- [ ] Every functional phase begins with a focused failing test and ends with focused tests plus `scripts/build-modern.ps1 -Action Test`.
- [ ] Network phases require a two-machine test on separate physical networks where relevant, with host approval observed from the host UI.
- [ ] Every release rebuilds the Vite assets and verifies generated Qt resource entries contain the current hashes.
- [ ] Every package is inspected for forbidden identity, database, certificate, key, source, SDK, and token files before delivery.
- [ ] No phase modifies the user’s existing uncommitted package/UI changes unless the user explicitly includes them in the phase commit.

## Execution Order

1. Approve and complete Phase 0 only.
2. Approve Phase 1 after Phase 0 acceptance.
3. Approve Phases 2 and 3 separately because they change protocol/database authorization behavior.
4. Approve Phase 4A before 4B; do not begin Phase 7 unless the user explicitly asks for true P2P.
5. Treat code signing credentials and release hosting as user-controlled external authority, not an automatic coding task.
