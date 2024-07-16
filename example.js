const TUN = require('./')

const u = new TUN()

u.configure({
  ip: '10.22.0.10',
  netmask: '255.255.255.255',
  route: '10.22.0.0/24'
})

u.on('data', console.log)
