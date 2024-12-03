const test = require('brittle')
const UTUN = require('.')
const UDX = require('udx-native')
const ip = require('ip-packet')
const { generatePacket } = require('./benchmark/tools')

test('send packet', async t => {
  const tun = new UTUN()
  await tun.configure({ ip: '10.22.0.12', netmask: '255.255.255.0' })

  const u = new UDX()
  const socket = u.createSocket()
  socket.bind(1911, tun.ip)

  const received = new Promise(resolve => {
    socket.once('message', resolve)
  })

  for (let i = 0; i < 10; i++) {
    await tun.write(generatePacket('10.22.0.11', tun.ip, 'hello'))
  }

  const message = await received
  t.is(message.toString(), 'hello', 'packet arrived on port')

  await socket.close()
  await tun.close()
})
test('receive packet', async t => {
  const tun = new UTUN()
  await tun.configure({ ip: '10.22.0.12', netmask: '255.255.255.0' })

  const captured = new Promise(resolve => {
    tun.once('data', matchUDP(resolve))
  })

  const u = new UDX()
  const socket = u.createSocket()
  socket.bind(0)

  for (let i = 0; i < 10; i++) {
    /** BUG!
     * Linux sets up automatic route to provided net so
     * tun.configure({ ..., route: '10.22.0.x/24'} }) throws error 'already exists'.
     * Also packets to local v-ip are not looped into tunnel by default.
     *
     * Apple on the other hand does not seem to receive packets destined to the
     * v-net without adding a route. But for some reason packets to local v-ip are captured.
     */

    // Quickfix for now:
    await socket.send(Buffer.from('world'), 1911, '10.22.0.99') // captured on linux
    await socket.send(Buffer.from('world'), 1911, tun.ip) // captured on apple
  }

  const message = await captured
  t.is(message.toString(), 'world', 'packet captured')

  await socket.close()
  await tun.close()
})

function matchUDP (cb) {
  return buffer => {
    const { version, protocol, data } = ip.decode({ buffer, start: 0 })
    if (version !== 4) return // ipv4
    if (protocol !== 0x11) return // UDP
    const body = data.subarray(8) // skip udp header
    cb(body)
  }
}
