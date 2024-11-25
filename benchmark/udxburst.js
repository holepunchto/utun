/**
 * Usage:
 *
 * # server
 * $ bare udxburst.js s
 *
 * # client
 * $ bare udxburst.js ADDRESS mBits
 */
const UDX = require('udx-native')
const { burst, measure} = require('./tools')
const port = 1911
const addr = Bare.argv[2]

if (addr) udxClient()
else udxServer()

async function udxClient () {
  const u = new UDX()
  const socket = u.createSocket()
  const mbps = parseInt(Bare.argv[3] || 10)
  const rounds = parseInt(Bare.argv[4] || 10)

  socket.bind(0)

  await burst(send, mbps, rounds)

  await socket.close()

  function send (data, safe = true) {
    return safe
      ? socket.send(data, port, addr) // async/wait for flush
      : socket.trySend(data, port, addr) || true // realtime scenario
  }
}

function udxServer () {
  console.log('listening on ', port)
  const u = new UDX()
  const socket = u.createSocket()
  socket.bind(port)
  measure(handler => socket.on('message', handler))
}


