const Pipe = require('bare-pipe')
const { spawn } = require('bare-subprocess')
const { isMac, isLinux } = require('which-runtime')
const binding = require('./binding')

const IS_SUPPORTED = isLinux || isMac
const MAC_HEADER = Buffer.from([0, 0, 0, 2])

module.exports = class UTUN extends Pipe {
  constructor (opts = {}) {
    if (!IS_SUPPORTED) throw new Error('Unsupported platform')

    const defaultName = opts.name || 'tunnel0'
    const nameBuffer = isLinux ? toCString(defaultName) : Buffer.allocUnsafe(1024)
    const fd = binding.open(nameBuffer)

    super(fd)

    this._readableState.map = mapReadable
    this._writableState.map = mapWritable

    this.fd = fd
    this.name = nameBuffer.slice(0, nameBuffer.indexOf(0)).toString()
    this.ip = null
    this.netmask = null
    this.mtu = 0
    this.route = null
  }

  IS_SUPPORTED = IS_SUPPORTED

  async configure ({ ip, mtu, netmask, route }) {
    if (ip) this.ip = ip
    if (mtu) this.mtu = mtu
    if (netmask) this.netmask = netmask
    if (route) this.route = route

    if (!this.ip) throw new Error('IP is required')

    if (isMac) {
      if (ip) await run('ifconfig', this.name, this.ip, this.ip)
      if (mtu) await run('ifconfig', this.name, 'mtu', this.mtu, 'up')
      if (netmask) await run('ifconfig', this.name, this.ip, this.ip, 'netmask', this.netmask)
      if (route) await run('route', '-n', 'add', '-net', this.route, '-iface', this.name)
    }

    if (isLinux) {
      if (!this.netmask) throw new Error('Netmask is required')

      if (ip) {
        await run('ip', 'addr', 'add', `${this.ip}/${this.netmask}`, 'dev', this.name)
        await run('ip', 'link', 'set', 'dev', this.name, 'up')
      }

      if (mtu) await run('ip', 'link', 'set', 'dev', this.name, 'mtu', this.mtu)
      if (route) await run('ip', 'route', 'add', this.route, 'dev', this.name)
    }
  }
}

function run (cmd, ...args) {
  const proc = spawn(cmd, [...args])

  return new Promise(function (resolve, reject) {
    proc.on('exit', function (code) {
      if (code) reject(new Error('Failed: ' + code))
      else resolve()
    })
  })
}

function toCString (str) {
  const buf = Buffer.alloc(Buffer.byteLength(str) + 1)
  buf.write(str)
  return buf
}

function mapReadable (data) {
  return isMac ? data.subarray(4) : data
}

function mapWritable (data) {
  return isMac ? Buffer.concat([MAC_HEADER, data]) : data
}
