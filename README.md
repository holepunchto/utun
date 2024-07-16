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

## License

Apache-2.0
