# utun

Make TUN devices

```
npm i utun
```

## Usage

```js
const UTUN = require('utun')
const ip = require('ip-packet')

const u = new UTUN()

u.configure({
  ip: '10.22.0.10',
  netmask: '255.255.255.255',
  route: '10.22.0.0/24'
})

u.on('data', function (buffer) {
  console.log(ip.decode({ start: 0, end: buffer.byteLength, buffer }))
})

```

## API

#### `new UTUN(opts = {})`

`opts`
- `name` set interface name where supported, defaults to `tunnel0`

#### `async configure(opts)`
Attempts to normalize interface configuration across
platforms.

`opts`
- `ip` assigns address to interface
- `netmask` sets netmask
- `route` adds an additional route

_note1:_ on linux the `route` option may have to be omitted as
as system adds route `ip/netmask` to interface by default.\
Use `ip route show` to verify routes after configure resolves.

_note2:_ on mac, packets to local address (`utun.ip`) are captured by default.\
Use `route -n` to verify routes after configure resolves.

#### `async write(buffer)`

Transmits a packet on the tunnel device, resolves when packet is was enqueued.

#### `tryWrite(buffer)`

Same as write but does not retry when queue is full.
Simply returns `boolean` indicating wether packet was accepted or not.

#### `async close()`

Closes the interface

#### `info(reset = false)`

Exports tunnel statistics and counters.

sample:

```
{
  name: 'tunnel0',
  rxBytes: 1188,
  rxPackets: 10,
  rxDrop: 5,
  txBytes: 0,
  txPackets: 0,
  txRejected: 0
}
```

- `reset` resets all counters.

## License

Apache-2.0
