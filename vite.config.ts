import { builtinModules } from 'node:module'
import { resolve } from 'node:path'
import { defineConfig } from 'vite'

const external = [
  'node-gyp-build',
  ...builtinModules,
  ...builtinModules.map((module) => `node:${module}`),
]

export default defineConfig({
  build: {
    target: 'node20',
    sourcemap: true,
    lib: {
      entry: resolve(import.meta.dirname, 'js/index.ts'),
      formats: ['es', 'cjs'],
      fileName: (format) => (format === 'es' ? 'index.js' : 'index.cjs'),
    },
    rollupOptions: {
      external,
      output: { exports: 'named' },
    },
  },
})
