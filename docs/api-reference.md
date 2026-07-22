# API reference

`@zerob13/nativekit` must be imported in the Electron main process. Renderer
code should call a narrow, context-isolated preload API instead of importing the
package directly.

```ts
import { apps, drag, overlay, secureChannel, windows } from '@zerob13/nativekit'
```

Promise-returning methods currently wrap synchronous native results or native
session completion. This keeps the JavaScript contract stable if more work moves
off the main thread later.

## Shared coordinates

Every public point and rectangle uses Electron device-independent pixels (DIP)
with a top-left screen origin. macOS points already use this model. On Windows,
the wrapper converts between Electron DIP and native physical pixels.

```ts
interface Point {
  x: number
  y: number
}

interface Rect extends Point {
  width: number
  height: number
}
```

## `overlay`

Floating image panels with host, presentation, and session lifecycle. macOS
panels join all Spaces. Windows panels are topmost on their current virtual
desktop.

### Types

```ts
interface AnchorConfig {
  edge: 'leading' | 'trailing' | 'top' | 'bottom'
  offset: number
}

interface HostConfig {
  id: string
  title: string
  bounds: Rect
  windowHandle: Buffer
  anchor: AnchorConfig
}

interface ImageFrame {
  hostId?: string
  presentationId: string
  sessionId: string
  imageData: string
  appIconPath?: string | null
}

interface OverlayOptions {
  tooltip?: {
    hide?: string
    relocate?: string
  }
}
```

Pass `BrowserWindow.getContentBounds()` as `bounds` and
`BrowserWindow.getNativeWindowHandle()` as `windowHandle`. Width and height
constrain panel sizing; the native handle selects the correct display. Refresh
the host after the BrowserWindow moves, resizes, or changes display.

`imageData` must be a PNG or JPEG base64 data URL no longer than 32 MiB. Decoded
images are limited to 8192 pixels per dimension and 64 MiB of RGBA pixels.
`appIconPath` is a `.app` path on macOS or executable path on Windows.

### Methods

#### `overlay.start(options?: OverlayOptions): boolean`

Start the platform renderer or update its tooltip options. Repeated calls are
safe.

#### `overlay.stop(): boolean`

Destroy every host and panel. Repeated calls are safe.

#### `overlay.attachHost(config: HostConfig): boolean`

Add or update a BrowserWindow host. `id` is unique within the process.

```ts
overlay.attachHost({
  id: 'main',
  title: 'Assistant',
  bounds: win.getContentBounds(),
  windowHandle: win.getNativeWindowHandle(),
  anchor: { edge: 'trailing', offset: 16 },
})
```

#### `overlay.detachHost(hostId: string): boolean`

Remove a host and all of its presentations. Returns `false` when the host does
not exist.

#### `overlay.setVisible(visible: boolean): boolean`

Show or hide all presentations without deleting them.

#### `overlay.setMaxSize(size: number): boolean`

Set the maximum rendered panel edge in DIP. Emits `maxSizeChanged` after the new
configured cap has been applied.

#### `overlay.pushImage(frame: ImageFrame): boolean`

Create or update a presentation. `hostId` may be omitted only when exactly one
host exists. An existing `presentationId` cannot move to another host or
session. Invalid image data rejects the call without replacing the previous
valid presentation.

```ts
overlay.pushImage({
  hostId: 'main',
  presentationId: 'snapshot-1',
  sessionId: 'task-42',
  imageData: 'data:image/png;base64,iVBORw0KGgo...',
  appIconPath: '/Applications/Safari.app',
})
```

#### `overlay.removeImage(presentationId: string): boolean`

Remove one presentation. Returns `false` when it does not exist.

#### `overlay.completeSession(sessionId: string): boolean`

Remove every presentation in a session. Returns whether anything was removed.

#### `overlay.invalidateSession(sessionId: string, presentationId: string): boolean`

Remove one presentation only if it belongs to the supplied session.

#### `overlay.suppressSessions(sessionIds: string[]): boolean`

Replace the set of suppressed sessions. Suppressed items remain in state but do
not occupy stack space.

#### `overlay.setActiveSession(sessionId: string): boolean`

Place a session first in presentation ordering.

#### `overlay.hasActive(): boolean`

Whether at least one unsuppressed presentation is eligible to display while the
overlay is globally visible.

#### `overlay.hasAny(): boolean`

Whether any presentation exists, including suppressed or globally hidden ones.

### Events

| Event | Listener | Meaning |
|---|---|---|
| `maxSizeChanged` | `(size: number) => void` | `setMaxSize()` applied a new configured cap. |
| `activate` | `() => void` | The user double-clicked a panel outside its controls. |
| `visibilityRequest` | `(visible: boolean) => void` | A panel control requested global visibility change; currently hide emits `false`. |

```ts
overlay.on('activate', () => win.show())
overlay.on('visibilityRequest', (visible) => {
  if (!visible) overlay.setVisible(false)
})
```

Overlay transitions are immediate. There is no partial animation API.

## `windows`

### Types

```ts
interface SystemWindow {
  id: number
  name: string | null
  bounds: Rect
  level: number
  ownerPid: number
  ownerName: string | null
  isOnscreen: boolean
}

interface FrontmostWindow {
  bundleId: string
  icon: string | null
  name: string
  title: string | null
}
```

`level` is the front-to-back z-order position; lower values are nearer the
front. OS filtering can leave gaps. `isOnscreen` means visible, not minimized or
cloaked, and intersecting a display. `bundleId` is a bundle identifier (or app
path fallback) on macOS and the executable path on Windows.

#### `windows.frontmost(): Promise<FrontmostWindow | null>`

Return the foreground application, its best normal-window title, and a 32×32
PNG icon when available.

#### `windows.list(options?: { relativeTo?: number }): Promise<SystemWindow[]>`

Return a front-to-back snapshot including hidden windows. With `relativeTo`,
the result starts below that window and excludes it and all windows above it.
An unknown reference produces an empty array.

#### `windows.find(id: number): Promise<SystemWindow | null>`

Resolve one current window snapshot by native id.

#### `windows.atPoint(point: Point, options?: { belowId?: number }): Promise<SystemWindow | null>`

Return the first on-screen window containing the screen DIP point. `belowId`
excludes that window and everything above it.

## `secureChannel`

Launch one path-verified worker and decode its stdout as ordered binary frames.
This is process isolation and restricted IPC, not a hostile-code sandbox.

Each frame is:

```text
uint32 little-endian payload length | non-empty payload bytes
```

Payload length must be between 1 byte and 16 MiB. Invalid or truncated framing
terminates the channel and reports exit code `-1`.

#### `secureChannel.spawn(executablePath: string, arguments?: string[]): Promise<number | null>`

Start a worker without a shell. The absolute executable path is canonicalized
and verified while the process is suspended. Returns its PID, or `null` if the
path is invalid, startup fails, or another worker is active.

```ts
const pid = await secureChannel.spawn(process.execPath, [workerPath])
```

The worker's stdin and stderr point to the null device. stdout is reserved for
framed messages. Child processes are terminated with the worker when the
channel closes.

#### `secureChannel.verify(pid: number, executablePath: string): Promise<boolean>`

Compare a running process's canonical executable path with the expected path.
`spawn()` performs this verification before resuming its child.

#### `secureChannel.terminate(): boolean`

Terminate the active worker and its process group/job. Returns `false` when no
worker is active, including after a natural exit.

### Events

| Event | Listener | Meaning |
|---|---|---|
| `data` | `(payload: Buffer) => void` | One complete frame. |
| `exit` | `(code: number) => void` | Worker exit after all decoded data events have been delivered. |

On macOS, signal exits use `128 + signal`. Windows returns the process exit
code converted to a signed 32-bit number. Channel/framing failures use `-1`.

## `apps`

#### `apps.icon(appPath: string, options?: { size?: 'small' | 'medium' }): Promise<string | null>`

Return an exact-size PNG data URL, or `null` when no application icon can be
read. `small` is 16×16 and `medium` is 32×32.

- macOS: absolute `.app` bundle or executable path.
- Windows: absolute executable or file path understood by the Shell.

```ts
const icon = await apps.icon('/Applications/Safari.app', { size: 'medium' })
```

## `drag`

### Types

```ts
interface DragConfig {
  files: string[]
  windowHandle: Buffer
  position: Point
}

interface DragResult extends Point {
  dropped: boolean
}
```

#### `drag.start(config: DragConfig): Promise<void>`

Begin one copy-only native file drag. Every path must be absolute and exist.
`windowHandle` comes from the source BrowserWindow. `position` is a DIP point
inside that window's content area.

Call this method synchronously from a primary `pointerdown`/mouse-down handler;
the OS drag loop expects the button to remain pressed when it begins. The
promise resolves on either drop or cancel and rejects if validation or native
startup fails. A second concurrent drag rejects.

```ts
element.addEventListener('pointerdown', async (event) => {
  if (event.button !== 0 || !event.isPrimary) return
  await drag.start({
    files: ['/absolute/path/report.pdf'],
    windowHandle: win.getNativeWindowHandle(),
    position: { x: event.clientX, y: event.clientY },
  })
})
```

### Events

| Event | Listener | Meaning |
|---|---|---|
| `ended` | `(result: DragResult) => void` | Drop/cancel result and final top-left-origin screen DIP point. |
