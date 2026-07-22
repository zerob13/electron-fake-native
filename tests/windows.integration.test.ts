import { resolve } from 'node:path'

import { describe, expect, it } from 'vitest'

import {
  apps,
  overlay,
  secureChannel,
  windows,
  type SystemWindow,
} from '../js/index.js'

const workerPath = resolve(import.meta.dirname, 'fixtures/framed-worker.mjs')

function expectWindowShape(window: SystemWindow): void {
  expect(Number.isSafeInteger(window.id)).toBe(true)
  expect(window.id).toBeGreaterThan(0)
  expect(Number.isFinite(window.bounds.x)).toBe(true)
  expect(Number.isFinite(window.bounds.y)).toBe(true)
  expect(window.bounds.width).toBeGreaterThan(0)
  expect(window.bounds.height).toBeGreaterThan(0)
  expect(Number.isSafeInteger(window.ownerPid)).toBe(true)
  expect(typeof window.isOnscreen).toBe('boolean')
}

describe('windows native integration', () => {
  it('enumerates and resolves a window snapshot', async () => {
    const snapshot = await windows.list()
    expect(Array.isArray(snapshot)).toBe(true)
    expect(snapshot.length).toBeGreaterThan(0)
    expectWindowShape(snapshot[0])

    let resolved = null
    for (const candidate of snapshot.slice(0, 20)) {
      resolved = await windows.find(candidate.id)
      if (resolved !== null) break
    }
    expect(resolved).not.toBeNull()
  })

  it('returns a well-formed frontmost application when available', async () => {
    const frontmost = await windows.frontmost()
    if (frontmost === null) return
    expect(frontmost.name.length).toBeGreaterThan(0)
    expect(typeof frontmost.bundleId).toBe('string')
    expect(frontmost.icon === null || frontmost.icon.startsWith('data:image/png;base64,'))
      .toBe(true)
  })
})

describe('JavaScript boundary validation', () => {
  it('rejects malformed overlay input before crossing N-API', () => {
    expect(() =>
      overlay.pushImage({
        presentationId: 'presentation',
        sessionId: 'session',
        imageData: 'not-an-image',
      }),
    ).toThrow('PNG or JPEG data URL')
  })

  it('rejects relative executable and application paths', () => {
    expect(() => secureChannel.spawn('worker')).toThrow('absolute path')
    expect(() =>
      secureChannel.spawn(process.execPath, [42 as unknown as string]),
    ).toThrow('arguments[0] must be a string')
    expect(() => apps.icon('Safari.app')).toThrow('absolute path')
  })
})

describe('overlay lifecycle integration', () => {
  it('starts, reports empty state, emits size changes, and stops idempotently', async () => {
    expect(overlay.start()).toBe(true)
    expect(overlay.hasAny()).toBe(false)
    expect(overlay.hasActive()).toBe(false)

    const changed = new Promise<number>((resolvePromise) => {
      overlay.once('maxSizeChanged', resolvePromise)
    })
    expect(overlay.setMaxSize(360)).toBe(true)
    await expect(changed).resolves.toBe(360)

    expect(overlay.stop()).toBe(true)
    expect(overlay.stop()).toBe(true)
  })
})

describe('secure channel integration', () => {
  it('verifies the executable and decodes framed worker output', async () => {
    const data = new Promise<Buffer>((resolvePromise) => {
      secureChannel.once('data', resolvePromise)
    })
    const exit = new Promise<number>((resolvePromise) => {
      secureChannel.once('exit', resolvePromise)
    })

    const pid = await secureChannel.spawn(process.execPath, [workerPath])
    expect(pid).not.toBeNull()
    expect(await secureChannel.verify(pid!, process.execPath)).toBe(true)
    expect(await secureChannel.spawn(process.execPath, [workerPath])).toBeNull()
    await expect(data).resolves.toEqual(Buffer.from('nativekit-worker'))
    await expect(exit).resolves.toBe(0)
    expect(secureChannel.terminate()).toBe(true)
    expect(secureChannel.wasTerminatedByPrivacy()).toBe(false)
  })

  it('terminates an active worker', async () => {
    const data = new Promise<Buffer>((resolvePromise) => {
      secureChannel.once('data', resolvePromise)
    })
    const exit = new Promise<number>((resolvePromise) => {
      secureChannel.once('exit', resolvePromise)
    })

    expect(
      await secureChannel.spawn(process.execPath, [workerPath, '--wait']),
    ).not.toBeNull()
    await data
    expect(secureChannel.terminate()).toBe(true)
    await expect(exit).resolves.toEqual(expect.any(Number))
  })
})
