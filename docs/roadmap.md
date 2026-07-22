# Roadmap

The original capability roadmap through Phase 3 is implemented in release
candidate `0.4.0`. Phase 4 remains intentionally deferred because a partial
animation surface would create cross-platform behavior the library cannot keep.

## Phase 0 — Foundation (`windows`)

- [x] Shared `Rect`, `Point`, `SystemWindow`, and window-query interface
- [x] macOS `frontmost`, `list`, `find`, and `atPoint`
- [x] Windows `frontmost`, `list`, `find`, and `atPoint`
- [x] Application identity, title, owner, bounds, visibility, and z-order
- [x] Electron DIP coordinate normalization
- [x] Main-process guard, validation, Promise normalization, and declarations
- [x] CI targets for `darwin-arm64`, `darwin-x64`, and `win32-x64`

## Phase 1 — Floating overlay (`overlay`)

- [x] Shared host, presentation, session, suppression, active-session, stack,
  and anchor state
- [x] Transactional image updates with decode-failure rollback
- [x] macOS non-activating `NSPanel` on all Spaces
- [x] Windows layered topmost `HWND` on the current virtual desktop
- [x] PNG/JPEG data-URL decode with compressed and decoded-size limits
- [x] Aspect-preserving, work-area-bounded stack layout
- [x] Application-icon badge on both platforms
- [x] DPI-aware hide/relocate controls and tooltips
- [x] `maxSizeChanged`, `activate`, and `visibilityRequest` events
- [x] Session complete, invalidate, suppress, activate, and visibility lifecycle
- [x] Immediate cross-platform transitions with no partial animation contract

## Phase 2 — Verified worker channel (`secureChannel`)

- [x] Private inherited stdout pipe with 4-byte little-endian framing
- [x] Non-empty payloads up to 16 MiB
- [x] Suspended start and canonical executable-path verification
- [x] macOS `posix_spawn`, isolated process group, and group termination
- [x] Windows restricted token, inherited-handle allowlist, and Job lifetime
- [x] Null stdin/stderr and no shell
- [x] Ordered `data...exit` delivery through one ThreadSafeFunction
- [x] Invalid/truncated channel detection and exit code `-1`
- [x] Deterministic cleanup and active-worker termination semantics

## Phase 3 — Desktop integration (`apps`, `drag`, demo, release)

### App icons

- [x] macOS `NSWorkspace` lookup
- [x] Windows Shell lookup and WIC encoding
- [x] Exact 16×16 and 32×32 PNG variants

### File drag-out

- [x] macOS `NSDraggingSession`
- [x] Windows OLE `IDataObject`, `IDropSource`, `CF_HDROP`, and drag helper
- [x] One active copy-only session
- [x] Drop/cancel result and cross-platform screen DIP coordinates
- [x] DPI-correct Windows client origin

### Consumer and release path

- [x] Vite library build for ESM and CommonJS
- [x] Generated TypeScript declarations
- [x] pnpm workspace Electron demo with context-isolated preload
- [x] Automated macOS capability smoke, including native drag-loop completion
- [x] CMake primary build and `binding.gyp` fallback
- [x] Matrix CI build/test/package gates for all supported targets
- [x] Tag-driven npm tarball assembly and GitHub release workflow
- [x] Node-API prebuild lookup through `node-gyp-build`

## Verification status

| Target | Compile | Native integration | Interactive demo |
|---|---|---|---|
| `darwin-arm64` | Local CMake + node-gyp | 14 integration tests | Smoke and visual QA complete |
| `darwin-x64` | Local cross-compile + CI workflow | CI gate configured | Requires an Intel runner/device |
| `win32-x64` | CI workflow configured; code statically reviewed | CI gate configured | Explorer drop and visual behavior require a Windows device |

Windows is implemented, but it is not described as locally runtime-verified.
The release workflow makes a Windows compile and integration pass mandatory
before npm publication.

## Phase 4 — Animation toolkit (deferred)

This phase remains out of scope unless all criteria can ship together:

- [ ] Spring model with damping, stiffness, mass, and initial velocity
- [ ] Display-link/message-pump 60 fps drivers on both platforms
- [ ] Interactive drag, resize, and programmatic re-anchor
- [ ] Shared behavior across overlay and future window effects
- [ ] Cross-platform parity and performance tests

Until then, overlay movement remains immediate.

## Release line

| Version | Capability set |
|---|---|
| `0.1.0` | window awareness |
| `0.2.0` | window awareness + overlay |
| `0.3.0` | verified worker channel |
| `0.4.0` | app icons, drag-out, demo, and release pipeline |
| `1.0.0` | after public API feedback and Windows device qualification |
