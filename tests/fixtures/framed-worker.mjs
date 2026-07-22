const payload = Buffer.from('nativekit-worker')
const header = Buffer.allocUnsafe(4)
header.writeUInt32LE(payload.length)
process.stdout.write(header.subarray(0, 2))

setTimeout(() => {
  process.stdout.write(Buffer.concat([header.subarray(2), payload]))
  if (process.argv.includes('--wait')) {
    setInterval(() => {}, 1_000)
  } else {
    setTimeout(() => process.exit(0), 250)
  }
}, 10)
