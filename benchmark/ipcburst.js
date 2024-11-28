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

const { burst, measure, generatePacket } = require('./tools')
const UTUN = require('../index')
const utun = new UTUN()
let __pktId = 777

async function main () {
  await utun.configure({
    ip: '10.22.0.12',
    netmask: '255.255.255.0',
    mtu: 1300
  })

  // utun.on('data', packet => console.log('captured', packet))
  utun.on('error', err => console.error('utun:err', err))
  const mbits = Number(globalThis.Bare.argv[2] || 0)

  if (mbits > 0) {
    console.log('injecting samples')
    const transmit = data => {
      const packet = generatePacket('10.22.0.11', '10.22.0.12', data)
      // async
      // return utun.write(packet)

      // sync nonblocking
      return utun.tryWrite(packet)
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
