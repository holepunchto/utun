/**
 * INJECTION TEST:
 *
 * generates same packets as udxburst.js
 * but injects them directly into local tun interface
 *
 * in term one:
 * $ bare udxburst.js s # server
 *
 * and term two:
 * $ sudo bare ipcburst.js 100 # injection client
 *
 *
 *
 * CAPTURE TEST:
 * decodes raw packets and evaluates them using
 * udx-eval code.
 *
 * in term one:
 * $ sudo bare ipcburst.js # capturing server
 *
 * and term two:
 * $ bare udxburst.js 10.22.0.13 # client
 *
 */

const { burst, measure } = require('./tools')
const UTUN = require('../index')
const utun = new UTUN()
let __pktId = 777

async function main () {
  await utun.configure({
    ip: '10.22.0.12',
    netmask: '255.255.255.0',
    gateway: '10.22.0.0/24',
    mtu: 1300,
  })

  // utun.on('data', packet => console.log('captured', packet))
  utun.on('error', err => console.error('utun:err', err))
  const mbits = Number(Bare.argv[2] || 0)

  if (mbits > 0) {
    console.log('injecting samples')
    const transmit = data => {
      // async
      // return utun.write(mkPacket(begin))

      // sync nonblocking
      return utun.tryWrite(mkPacket(data))
    }

    await burst(transmit, mbits)

    console.log(utun.info())
  } else {
    console.log('capturing traffic')

    let seen = 0

    // setInterval(() => console.log('captured C:', utun.info().rxPackets, ', seen by JS:', seen), 2000)

    // print stats then reset counters
    const onfinish = () => {
      console.log('captured C:', utun.info().rxPackets, ', seen by JS:', seen)
      console.log(utun.info(true))
      seen = 0
    }

    // Decode raw packet then forward to measure handler
    const subscribe = handler => {
      utun.on('data', packet => {
        seen++
        // const src = Array.from(packet.subarray(12, 16)).join('.')
        // const dst = Array.from(packet.subarray(16, 20)).join('.')
        const port = packet.readUInt16BE(22)

        // if (port !== 1911) console.log('captured', src, dst, port)
        if (port !== 1911) return // simple filter

        const data = packet.subarray(20 + 8)
        handler(data)
      })
    }

    measure(subscribe, onfinish)
  }
}

main()

function mkPacket (data) {
  const ipHeader = Buffer.from([ // require('ip-packet').encode() could be used.
    0x45, 0x00, // Version (4) + IHL (5), DSCP/ECN (0)
    0x00, 0x20, // Total length: 32 bytes
    0x1c, 0x46, // Identification
    0x40, 0x00, // Flags (Don't Fragment) + Fragment offset
    0x40, 0x11, // TTL (64), Protocol (UDP = 0x11)
    0x00, 0x00, // Header checksum (0 for now, calculate later)
    10, 22, 0, 11, // Source IP: 10.22.0.11
    10, 22, 0, 12 // Destination IP: 10.22.0.12
  ]) // 20 bytes
  ipHeader.writeUInt16BE(20 + 8 + data.byteLength, 2) // overwrite length
  __pktId = (__pktId + 1) & 0xffff
  ipHeader.writeUInt16BE(__pktId, 4)
  ipHeader.writeUInt16BE(calculateChecksum(ipHeader), 10)
  // console.log('ip header', ipHeader)

  const udpHeader = Buffer.from([
    0xa2, 0xf6, // Source port: 41718
    0x07, 0x77, // Destination port: 1911
    0x00, 0x0c, // Length: 12 bytes (header + data)
    0x00, 0x00 // Checksum (0 for simplicity)
  ]) // 8 bytes
  udpHeader.writeUInt16BE(8 + data.byteLength, 4) // overwrite length
  // udpHeader.writeUInt16BE(calculateChecksum(Buffer.concat([pseudoHeader, udpHeader, data])), 6)
  // console.log('udp header', udpHeader)

  return Buffer.concat([ipHeader, udpHeader, data])
}

function calculateChecksum (buffer) {
  let sum = 0
  for (let i = 0; i < buffer.byteLength; i += 2) {
    sum += i === buffer.byteLength - 1
      ? buffer[i] << 8
      : buffer.readUInt16BE(i)
    if (sum > 0xffff) sum = (sum & 0xffff) + 1
  }
  return (~sum) & 0xffff
}
