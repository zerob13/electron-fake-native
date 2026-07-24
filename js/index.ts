/** Cross-platform native capabilities for the Electron main process. */

import { EventEmitter } from 'node:events'
import { createRequire } from 'node:module'
import { dirname, isAbsolute, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { isMainThread } from 'node:worker_threads'
import { inflateSync } from 'node:zlib'

export interface Point {
  x: number
  y: number
}

export interface Rect extends Point {
  width: number
  height: number
}

export interface AnchorConfig {
  edge: 'leading' | 'trailing' | 'top' | 'bottom'
  offset: number
}

export interface HostConfig {
  id: string
  title: string
  bounds: Rect
  windowHandle: Buffer
  anchor: AnchorConfig
}

export interface ImageFrame {
  /** Required when more than one host is attached. */
  hostId?: string
  presentationId: string
  sessionId: string
  imageData: string
  appIconPath?: string | null
}

export type OverlayControlIcon = 'close' | 'panel-right-open'

export interface OverlayControlConfig {
  id: string
  icon: OverlayControlIcon
  tooltip?: string
}

export type OverlayToolbarStyle = 'system' | 'light' | 'dark'

export interface OverlayToolbarButtonConfig {
  id: string
  imageData: string
  tooltip?: string
}

export interface OverlayToolbarConfig {
  style?: OverlayToolbarStyle
  buttons?: OverlayToolbarButtonConfig[]
}

export interface OverlayOptions {
  /** @deprecated Use toolbar for caller-provided button images and fixed styles. */
  controls?: OverlayControlConfig[]
  toolbar?: OverlayToolbarConfig
}

export interface SystemWindow {
  id: number
  name: string | null
  bounds: Rect
  level: number
  ownerPid: number
  ownerName: string | null
  isOnscreen: boolean
}

export interface FrontmostWindow {
  bundleId: string
  icon: string | null
  name: string
  title: string | null
}

interface NativeBinding {
  overlayStart(options: OverlayOptions): boolean
  overlayStop(): boolean
  overlayAttachHost(config: HostConfig): boolean
  overlayDetachHost(hostId: string): boolean
  overlaySetVisible(visible: boolean): boolean
  overlaySetMaxSize(size: number): boolean
  overlayPushImage(frame: ImageFrame): boolean
  overlayRemoveImage(presentationId: string): boolean
  overlayCompleteSession(sessionId: string): boolean
  overlayInvalidateSession(sessionId: string, presentationId: string): boolean
  overlaySuppressSessions(sessionIds: string[]): boolean
  overlaySetActiveSession(sessionId: string): boolean
  overlayHasActive(): boolean
  overlayHasAny(): boolean
  overlayOnMaxSizeChanged?(callback: (size: number) => void): void
  overlayOnActivate?(callback: () => void): void
  overlayOnControl?(callback: (controlId: string) => void): void

  windowsFrontmost(): FrontmostWindow | null
  windowsList(relativeTo: number): SystemWindow[]
  windowsFind(id: number): SystemWindow | null
  windowsAtPoint(point: Point, belowId: number): SystemWindow | null

  appsIcon(appPath: string, size: 'small' | 'medium'): string | null

}

interface ElectronScreen {
  dipToScreenPoint(point: Point): Point
  screenToDipRect(window: null, rect: Rect): Rect
}

const modulePath =
  typeof import.meta.url === 'string'
    ? fileURLToPath(import.meta.url)
    : __filename
const require = createRequire(modulePath)
const packageRoot = resolve(dirname(modulePath), '..')

function loadNative(): NativeBinding {
  try {
    const nodeGypBuild = require('node-gyp-build') as (
      directory: string,
    ) => NativeBinding
    return nodeGypBuild(packageRoot)
  } catch (cause) {
    throw new Error(
      `nativekit: no native binary for ${process.platform}-${process.arch}. ` +
        'Install a published prebuild or run `pnpm build:native`.',
      { cause },
    )
  }
}

function assertMainProcess(): void {
  const electronProcess = process as NodeJS.Process & { type?: string }
  if (!process.versions.electron) return
  const electron = require('electron') as { BrowserWindow?: unknown }
  if (
    electronProcess.type !== 'browser' ||
    !isMainThread ||
    typeof electron.BrowserWindow !== 'function'
  ) {
    throw new Error(
      'nativekit must run in the Electron main process; expose only the ' +
        'operations you need to renderers through a context-isolated preload.',
    )
  }
}

assertMainProcess()
const native = loadNative()

class Overlay extends EventEmitter {
  start(options: OverlayOptions = {}): boolean {
    assertMainProcess()
    validateOverlayOptions(options)
    return native.overlayStart(options)
  }

  stop(): boolean {
    assertMainProcess()
    return native.overlayStop()
  }

  attachHost(config: HostConfig): boolean {
    assertMainProcess()
    requireRecord(config, 'config')
    requireNonEmptyString(config.id, 'config.id')
    requireNonEmptyString(config.title, 'config.title')
    requireRect(config.bounds, 'config.bounds')
    requireBuffer(config.windowHandle, 'config.windowHandle')
    requireRecord(config.anchor, 'config.anchor')
    if (!['leading', 'trailing', 'top', 'bottom'].includes(config.anchor.edge)) {
      throw new TypeError(
        'config.anchor.edge must be leading, trailing, top, or bottom',
      )
    }
    requireNonNegative(config.anchor.offset, 'config.anchor.offset')
    return native.overlayAttachHost(config)
  }

  detachHost(hostId: string): boolean {
    assertMainProcess()
    requireNonEmptyString(hostId, 'hostId')
    return native.overlayDetachHost(hostId)
  }

  setVisible(visible: boolean): boolean {
    assertMainProcess()
    requireBoolean(visible, 'visible')
    return native.overlaySetVisible(visible)
  }

  setMaxSize(size: number): boolean {
    assertMainProcess()
    requirePositive(size, 'size')
    return native.overlaySetMaxSize(size)
  }

  pushImage(frame: ImageFrame): boolean {
    assertMainProcess()
    requireRecord(frame, 'frame')
    requireNonEmptyString(frame.presentationId, 'frame.presentationId')
    requireNonEmptyString(frame.sessionId, 'frame.sessionId')
    requireNonEmptyString(frame.imageData, 'frame.imageData')
    if (frame.imageData.length > 32 * 1024 * 1024) {
      throw new RangeError('frame.imageData exceeds the 32 MiB limit')
    }
    if (!/^data:image\/(?:png|jpe?g);base64,/i.test(frame.imageData)) {
      throw new TypeError('frame.imageData must be a PNG or JPEG data URL')
    }
    if (frame.appIconPath !== undefined && frame.appIconPath !== null) {
      requireAbsolutePath(frame.appIconPath, 'frame.appIconPath')
    }
    if (frame.hostId !== undefined) {
      requireNonEmptyString(frame.hostId, 'frame.hostId')
    }
    return native.overlayPushImage(frame)
  }

  removeImage(presentationId: string): boolean {
    assertMainProcess()
    requireNonEmptyString(presentationId, 'presentationId')
    return native.overlayRemoveImage(presentationId)
  }

  completeSession(sessionId: string): boolean {
    assertMainProcess()
    requireNonEmptyString(sessionId, 'sessionId')
    return native.overlayCompleteSession(sessionId)
  }

  invalidateSession(sessionId: string, presentationId: string): boolean {
    assertMainProcess()
    requireNonEmptyString(sessionId, 'sessionId')
    requireNonEmptyString(presentationId, 'presentationId')
    return native.overlayInvalidateSession(sessionId, presentationId)
  }

  suppressSessions(sessionIds: string[]): boolean {
    assertMainProcess()
    if (!Array.isArray(sessionIds)) {
      throw new TypeError('sessionIds must be an array')
    }
    sessionIds.forEach((id, index) =>
      requireNonEmptyString(id, `sessionIds[${index}]`),
    )
    return native.overlaySuppressSessions(sessionIds)
  }

  setActiveSession(sessionId: string): boolean {
    assertMainProcess()
    requireNonEmptyString(sessionId, 'sessionId')
    return native.overlaySetActiveSession(sessionId)
  }

  hasActive(): boolean {
    assertMainProcess()
    return native.overlayHasActive()
  }

  hasAny(): boolean {
    assertMainProcess()
    return native.overlayHasAny()
  }
}

class Windows {
  frontmost(): Promise<FrontmostWindow | null> {
    assertMainProcess()
    return Promise.resolve(native.windowsFrontmost())
  }

  list(options: { relativeTo?: number } = {}): Promise<SystemWindow[]> {
    assertMainProcess()
    requireRecord(options, 'options')
    if (options.relativeTo !== undefined) {
      requireWindowId(options.relativeTo, 'options.relativeTo')
    }
    return Promise.resolve(
      native
        .windowsList(options.relativeTo ?? 0)
        .map(windowFromNativeCoordinates),
    )
  }

  find(id: number): Promise<SystemWindow | null> {
    assertMainProcess()
    requireWindowId(id, 'id')
    return Promise.resolve(
      nullableWindowFromNativeCoordinates(native.windowsFind(id)),
    )
  }

  atPoint(
    point: Point,
    options: { belowId?: number } = {},
  ): Promise<SystemWindow | null> {
    assertMainProcess()
    requirePoint(point, 'point')
    requireRecord(options, 'options')
    if (options.belowId !== undefined) {
      requireWindowId(options.belowId, 'options.belowId')
    }
    return Promise.resolve(
      nullableWindowFromNativeCoordinates(
        native.windowsAtPoint(
          pointToNativeScreenCoordinates(point),
          options.belowId ?? 0,
        ),
      ),
    )
  }
}

class Apps {
  icon(
    appPath: string,
    options: { size?: 'small' | 'medium' } = {},
  ): Promise<string | null> {
    assertMainProcess()
    requireAbsolutePath(appPath, 'appPath')
    requireRecord(options, 'options')
    const size = options.size ?? 'small'
    if (size !== 'small' && size !== 'medium') {
      throw new TypeError('options.size must be small or medium')
    }
    return Promise.resolve(native.appsIcon(appPath, size))
  }
}

export const overlay = new Overlay()
export const windows = new Windows()
export const apps = new Apps()

native.overlayOnMaxSizeChanged?.((size) =>
  overlay.emit('maxSizeChanged', size),
)
native.overlayOnActivate?.(() => overlay.emit('activate'))
native.overlayOnControl?.((controlId) => overlay.emit('control', controlId))

export default { overlay, windows, apps }

function electronScreen(): ElectronScreen | null {
  if (process.platform !== 'win32' || !process.versions.electron) return null
  const electron = require('electron') as { screen?: ElectronScreen }
  return electron.screen ?? null
}

function pointToNativeScreenCoordinates(point: Point): Point {
  return electronScreen()?.dipToScreenPoint(point) ?? point
}

function windowFromNativeCoordinates(window: SystemWindow): SystemWindow {
  const screen = electronScreen()
  return screen === null
    ? window
    : { ...window, bounds: screen.screenToDipRect(null, window.bounds) }
}

function nullableWindowFromNativeCoordinates(
  window: SystemWindow | null,
): SystemWindow | null {
  return window === null ? null : windowFromNativeCoordinates(window)
}

function validateOverlayOptions(options: OverlayOptions): void {
  requireRecord(options, 'options')
  const configuredOptions = options as OverlayOptions
  if (
    configuredOptions.controls !== undefined &&
    configuredOptions.toolbar !== undefined
  ) {
    throw new TypeError(
      'options.controls and options.toolbar cannot be combined',
    )
  }
  if (configuredOptions.controls !== undefined) {
    validateToolbarButtons<OverlayControlConfig>(
      configuredOptions.controls,
      'options.controls',
      (control, name) => {
        if (
          control.icon !== 'close' &&
          control.icon !== 'panel-right-open'
        ) {
          throw new TypeError(
            `${name}.icon must be close or panel-right-open`,
          )
        }
      },
    )
    return
  }

  if (configuredOptions.toolbar === undefined) return
  requireRecord(configuredOptions.toolbar, 'options.toolbar')
  const toolbar = configuredOptions.toolbar as OverlayToolbarConfig
  const style = toolbar.style
  if (
    style !== undefined &&
    style !== 'system' &&
    style !== 'light' &&
    style !== 'dark'
  ) {
    throw new TypeError(
      'options.toolbar.style must be system, light, or dark',
    )
  }
  if (toolbar.buttons === undefined) return
  validateToolbarButtons<OverlayToolbarButtonConfig>(
    toolbar.buttons,
    'options.toolbar.buttons',
    (button, name) => {
      validateToolbarImageData(button.imageData, `${name}.imageData`)
    },
  )
}

function validateToolbarButtons<T extends { id: string; tooltip?: string }>(
  buttons: T[],
  name: string,
  validateButton: (button: T, name: string) => void,
): void {
  if (!Array.isArray(buttons)) {
    throw new TypeError(`${name} must be an array`)
  }
  if (buttons.length > 2) {
    throw new RangeError(`${name} supports at most two buttons`)
  }
  const ids = new Set<string>()
  buttons.forEach((button, index) => {
    const buttonName = `${name}[${index}]`
    requireRecord(button, buttonName)
    requireNonEmptyString(button.id, `${buttonName}.id`)
    validateButton(button, buttonName)
    if (ids.has(button.id)) {
      throw new TypeError(`${name} ids must be unique`)
    }
    ids.add(button.id)
    if (button.tooltip !== undefined) {
      requireNonEmptyString(button.tooltip, `${buttonName}.tooltip`)
    }
  })
}

function validateToolbarImageData(value: unknown, name: string): void {
  requireNonEmptyString(value, name)
  if (value.length > 512 * 1024) {
    throw new RangeError(`${name} exceeds the 512 KiB limit`)
  }
  const match = /^data:image\/png;base64,([a-z0-9+/]*={0,2})$/i.exec(value)
  if (match === null) {
    throw new TypeError(`${name} must be a PNG data URL`)
  }
  const png = Buffer.from(match[1], 'base64')
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])
  if (
    png.length < 26 ||
    !png.subarray(0, signature.length).equals(signature) ||
    png.toString('ascii', 12, 16) !== 'IHDR'
  ) {
    throw new TypeError(`${name} must contain PNG image data`)
  }
  const width = png.readUInt32BE(16)
  const height = png.readUInt32BE(20)
  if (width === 0 || height === 0 || width > 256 || height > 256) {
    throw new RangeError(`${name} dimensions must be between 1 and 256 pixels`)
  }
  let hasTransparentPixel = false
  try {
    hasTransparentPixel = pngHasTransparentPixel(png)
  } catch {
    throw new TypeError(`${name} must contain PNG image data`)
  }
  if (!hasTransparentPixel) {
    throw new TypeError(`${name} must contain a transparent pixel`)
  }
}

function pngHasTransparentPixel(png: Buffer): boolean {
  if (
    png.length < 33 ||
    png.readUInt32BE(8) !== 13 ||
    png.toString('ascii', 12, 16) !== 'IHDR'
  ) {
    throw new Error('Invalid PNG header')
  }
  const width = png.readUInt32BE(16)
  const height = png.readUInt32BE(20)
  const bitDepth = png[24]
  const colorType = png[25]
  const compression = png[26]
  const filter = png[27]
  const interlace = png[28]
  const channels = new Map<number, number>([
    [0, 1],
    [2, 3],
    [3, 1],
    [4, 2],
    [6, 4],
  ]).get(colorType)
  const validBitDepths: Record<number, readonly number[]> = {
    0: [1, 2, 4, 8, 16],
    2: [8, 16],
    3: [1, 2, 4, 8],
    4: [8, 16],
    6: [8, 16],
  }
  if (
    channels === undefined ||
    !validBitDepths[colorType]?.includes(bitDepth) ||
    compression !== 0 ||
    filter !== 0 ||
    (interlace !== 0 && interlace !== 1)
  ) {
    throw new Error('Unsupported PNG header')
  }

  const idatChunks: Buffer[] = []
  let transparency: Buffer | undefined
  let sawEnd = false
  for (let offset = 8; offset + 12 <= png.length; ) {
    const length = png.readUInt32BE(offset)
    if (length > png.length - offset - 12) {
      throw new Error('Invalid PNG chunk')
    }
    const type = png.toString('ascii', offset + 4, offset + 8)
    const data = png.subarray(offset + 8, offset + 8 + length)
    if (type === 'IDAT') idatChunks.push(data)
    if (type === 'tRNS') transparency = data
    offset += length + 12
    if (type === 'IEND') {
      sawEnd = true
      break
    }
  }
  if (idatChunks.length === 0 || !sawEnd) {
    throw new Error('Incomplete PNG')
  }

  const passes = interlace === 0
    ? [[0, 0, 1, 1] as const]
    : [
        [0, 0, 8, 8] as const,
        [4, 0, 8, 8] as const,
        [0, 4, 4, 8] as const,
        [2, 0, 4, 4] as const,
        [0, 2, 2, 4] as const,
        [1, 0, 2, 2] as const,
        [0, 1, 1, 2] as const,
      ]
  const bitsPerPixel = channels * bitDepth
  let expectedLength = 0
  for (const [startX, startY, stepX, stepY] of passes) {
    const passWidth = passDimension(width, startX, stepX)
    const passHeight = passDimension(height, startY, stepY)
    if (passWidth === 0 || passHeight === 0) continue
    expectedLength += (Math.ceil(passWidth * bitsPerPixel / 8) + 1) * passHeight
  }
  const inflated = inflateSync(Buffer.concat(idatChunks), {
    maxOutputLength: expectedLength + 1,
  })
  if (inflated.length !== expectedLength) {
    throw new Error('Invalid PNG scanline length')
  }

  const maximumSample = (1 << bitDepth) - 1
  const bytesPerPixel = Math.max(1, Math.ceil(bitsPerPixel / 8))
  let offset = 0
  for (const [startX, startY, stepX, stepY] of passes) {
    const passWidth = passDimension(width, startX, stepX)
    const passHeight = passDimension(height, startY, stepY)
    if (passWidth === 0 || passHeight === 0) continue
    const rowLength = Math.ceil(passWidth * bitsPerPixel / 8)
    let previous = Buffer.alloc(rowLength)
    for (let y = 0; y < passHeight; ++y) {
      const filterType = inflated[offset++]
      const row = Buffer.from(inflated.subarray(offset, offset + rowLength))
      offset += rowLength
      unfilterPngRow(row, previous, bytesPerPixel, filterType)
      for (let x = 0; x < passWidth; ++x) {
        const sample = (channel: number) =>
          pngSample(row, x * channels + channel, bitDepth)
        if (
          (colorType === 4 && sample(1) < maximumSample) ||
          (colorType === 6 && sample(3) < maximumSample) ||
          (colorType === 0 && transparency?.length === 2 &&
            sample(0) === transparency.readUInt16BE(0)) ||
          (colorType === 2 && transparency?.length === 6 &&
            sample(0) === transparency.readUInt16BE(0) &&
            sample(1) === transparency.readUInt16BE(2) &&
            sample(2) === transparency.readUInt16BE(4)) ||
          (colorType === 3 && transparency !== undefined &&
            sample(0) < transparency.length && transparency[sample(0)] < 255)
        ) {
          return true
        }
      }
      previous = row
    }
  }
  return false
}

function passDimension(size: number, start: number, step: number): number {
  return size <= start ? 0 : Math.ceil((size - start) / step)
}

function pngSample(row: Buffer, sampleIndex: number, bitDepth: number): number {
  if (bitDepth === 16) return row.readUInt16BE(sampleIndex * 2)
  if (bitDepth === 8) return row[sampleIndex]
  const bitOffset = sampleIndex * bitDepth
  const shift = 8 - bitDepth - bitOffset % 8
  return row[Math.floor(bitOffset / 8)] >> shift & ((1 << bitDepth) - 1)
}

function unfilterPngRow(
  row: Buffer,
  previous: Buffer,
  bytesPerPixel: number,
  filterType: number,
): void {
  if (filterType < 0 || filterType > 4) throw new Error('Invalid PNG filter')
  for (let index = 0; index < row.length; ++index) {
    const left = index >= bytesPerPixel ? row[index - bytesPerPixel] : 0
    const above = previous[index]
    const upperLeft = index >= bytesPerPixel
      ? previous[index - bytesPerPixel]
      : 0
    let prediction = 0
    if (filterType === 1) prediction = left
    if (filterType === 2) prediction = above
    if (filterType === 3) prediction = Math.floor((left + above) / 2)
    if (filterType === 4) prediction = paethPredictor(left, above, upperLeft)
    row[index] = (row[index] + prediction) & 0xff
  }
}

function paethPredictor(left: number, above: number, upperLeft: number): number {
  const estimate = left + above - upperLeft
  const leftDistance = Math.abs(estimate - left)
  const aboveDistance = Math.abs(estimate - above)
  const upperLeftDistance = Math.abs(estimate - upperLeft)
  if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) {
    return left
  }
  return aboveDistance <= upperLeftDistance ? above : upperLeft
}

function requireRecord(
  value: unknown,
  name: string,
): asserts value is Record<string, unknown> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    throw new TypeError(`${name} must be an object`)
  }
}

function requireNonEmptyString(
  value: unknown,
  name: string,
): asserts value is string {
  if (typeof value !== 'string' || value.length === 0) {
    throw new TypeError(`${name} must be a non-empty string`)
  }
}

function requireAbsolutePath(
  value: unknown,
  name: string,
): asserts value is string {
  requireNonEmptyString(value, name)
  if (!isAbsolute(value)) {
    throw new TypeError(`${name} must be an absolute path`)
  }
}

function requireBuffer(value: unknown, name: string): asserts value is Buffer {
  const isBuffer = Buffer.isBuffer(value)
  const byteLength = isBuffer ? value.byteLength : 0
  const expectedByteLength = process.platform === 'linux' ? 4 : 8
  if (!isBuffer || byteLength !== expectedByteLength) {
    throw new TypeError(
      `${name} must be the Buffer returned by getNativeWindowHandle()`,
    )
  }
}

function requireBoolean(
  value: unknown,
  name: string,
): asserts value is boolean {
  if (typeof value !== 'boolean') {
    throw new TypeError(`${name} must be a boolean`)
  }
}

function requireFiniteNumber(
  value: unknown,
  name: string,
): asserts value is number {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    throw new TypeError(`${name} must be a finite number`)
  }
}

function requireNonNegative(value: unknown, name: string): void {
  requireFiniteNumber(value, name)
  if (value < 0) throw new TypeError(`${name} must be non-negative`)
}

function requirePositive(value: unknown, name: string): void {
  requireFiniteNumber(value, name)
  if (value <= 0) throw new TypeError(`${name} must be positive`)
}

function requirePositiveInteger(value: unknown, name: string): void {
  requireFiniteNumber(value, name)
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new TypeError(`${name} must be a positive safe integer`)
  }
}

function requireWindowId(value: unknown, name: string): void {
  requirePositiveInteger(value, name)
}

function requirePoint(value: unknown, name: string): asserts value is Point {
  requireRecord(value, name)
  requireFiniteNumber(value.x, `${name}.x`)
  requireFiniteNumber(value.y, `${name}.y`)
}

function requireRect(value: unknown, name: string): asserts value is Rect {
  requireRecord(value, name)
  requireFiniteNumber(value.x, `${name}.x`)
  requireFiniteNumber(value.y, `${name}.y`)
  requirePositive(value.width, `${name}.width`)
  requirePositive(value.height, `${name}.height`)
}
