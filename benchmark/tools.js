const mtu = 1000 // test data packet size (actual MTU = 1300)
const END = Buffer.from([0x2])

async function burst (transmit, mbits = 100, rounds = 10) {
  const rate = Math.floor((mbits / 8) * (1024 ** 2) / mtu)

  const begin = Buffer.allocUnsafe(9)
  begin[0] = 0x0
  begin.writeUInt32BE(rounds, 1)
  begin.writeUInt32BE(rate, 5)
  for (let i = 0; i < 3; i++) await transmit(begin, true)
  await sleep(50)
  let sumTime = 0
  let sumDiscarded = 0
  for (let g = 0; g < rounds; g++) {
    const mem = Buffer.allocUnsafe((mbits / 8) * (1024 ** 2))

    mem.fill(0xaa) // empty space = 01010101

    const start = Date.now()
    let discarded = 0
    for (let i = 0; i < rate; i++) {
      const data = mem.subarray(i * mtu, i * mtu + mtu)
      data[0] = 0x1
      data.writeUInt32BE(g, 1)
      data.writeUInt32BE(i, 5)
      data.writeUInt32BE(Date.now() - start, 9)
      if (!await transmit(data)) discarded++
    }
    const ms = (Date.now() - start)
    console.log('burst round', g,
      'sent', rate, 'packets',
      ms, 'ms', '(', mbits, 'mBit/s )',
      ' (discard', discarded, ')'
    )
    sumTime += ms
    sumDiscarded += discarded
    if (ms < 1000) await sleep(1000 - ms)
  }
  await sleep(100)
  for (let i = 0; i < 10; i++) await transmit(END, true)
  console.log('burst complete, avg time', sumTime / rounds, 'ms, discarded', sumDiscarded)
}

function measure (subscribe, onfinish = noop) {
  let received = null
  let current = -1
  let timings = []
  let startedAt = null
  subscribe(data => {
    const type = data[0]
    switch (type) {
      case 0: { // begin
        const rounds = data.readUInt32BE(1)
        const rate = data.readUInt32BE(5)
        // reset
        current = 0
        timings = []
        received = []
        for (let i = 0; i < rounds; i++) {
          received[i] = []
          for (let j = 0; j < rate; j++) {
            received[i][j] = 0
          }
        }
        startedAt = Date.now()
        console.log('incoming burst, rounds', rounds, 'rate', rate, 'p/s')
      } break

      case 1: { // record
        const round = data.readUInt32BE(1)
        const i = data.readUInt32BE(5)
        received[round][i]++
        const time = data.readUInt32BE(9)
        timings[time] ||= 0
        timings[time]++

        if (round > current) {
          const c = current
          sleep(100).then(() => logStats(received, c))
          current = round
        }
      } break

      case 2: { // log measurement
        if (!received) break
        const r = received
        sleep(500).then(() => logStats(r))
        received = null
      } break

      default:
        console.log('unknown message [', type, ']')
        break
    }
  })

  function countRound (received, round) {
    let lost = 0
    let recv = 0
    for (const i of received[round]) {
      if (!i) lost++
      else recv++
    }
    return [lost, recv]
  }

  function logStats (received, round = -1) {
    const rate = received[0].length
    const rounds = received.length

    if (round >= 0) { // round stats
      const [lost] = countRound(received, round)
      console.log(
        'round', round,
        'complete, loss', lost, '/', rate,
        '(', (100 * (lost / rate)).toFixed(2), '%)',
        (rate * mtu * 8 / (1024 ** 2)).toFixed(2), 'mBit/s'
      )
      // console.log(received[round])
    } else { // final stats
      logStats(received, rounds - 1) // also log last round
      const elapsed = Date.now() - startedAt
      let lost = 0
      let recv = 0
      for (let i = 0; i < rounds; i++) {
        const [l, r] = countRound(received, i)
        lost += l
        recv += r
      }
      const total = rate * rounds
      const throughputBits = recv * mtu * 8 / (elapsed / 1000)
      console.log(
        'final loss', lost, '/', total,
        '(', (100 * (lost / total)).toFixed(2), '%)',
        'recv', recv,
        'measured throughput', (throughputBits / (1024 ** 2)).toFixed(1), 'mBit/s'
      )
      onfinish({ received, timings })
    }
  }
}

function sleep (t) { return new Promise(resolve => setTimeout(resolve, t)) }

function noop () {}

module.exports = {
  burst,
  measure,
  sleep
}
