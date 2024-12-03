const binding = require('./binding')
const { isMac, isLinux, isBare } = require('which-runtime')
const { EventEmitter } = require('bare-events')
const { spawn } = require(isBare && 'bare-subprocess' || 'child_process')

const IS_SUPPORTED = isLinux || isMac

module.exports = class UTUN extends EventEmitter {
  constructor (opts = {}) {
    if (!IS_SUPPORTED) throw new Error('Unsupported platform')
    super()

    const defaultName = opts.name || 'tunnel0'

    this.handle = binding.open(
      this,
      this._onread,
      this._onflush,
      defaultName
    )
    this.name = this.info().name
    this.ip = null
    this.netmask = null
    this.mtu = 0
    this.route = null
  }

  static IS_SUPPORTED = IS_SUPPORTED

  _onread (buffer) {
    this.emit('data', Buffer.from(buffer))
  }

  _onflush (userCallback) {
    queueMicrotask(userCallback)
  }

  write (buffer) {
    return new Promise((resolve, reject) => {
      const n = binding.write(this.handle, buffer, onflush)
      if (n < 0) reject(new Error('write error: ' + n))

      function onflush () {
        if (!n) resolve(this.write(buffer)) // try re-queue
        else resolve() // accepted
      }
    })
  }

  tryWrite (buffer) {
    const n = binding.write(this.handle, buffer)
    if (n < 0) throw new Error('write error: ' + n)
    return n
  }

  info (resetAfterRead = false) {
    return binding.info(this.handle, resetAfterRead)
  }

  async close () {
    await new Promise(resolve => {
      binding.close(this.handle, resolve)
    })
    this.emit('close')
  }

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
  // console.log('$', cmd, args.join(' '))
  const proc = spawn(cmd, args)
  return new Promise(function (resolve, reject) {
    const out = []
    proc.on('data', chunk => out.push(chunk))
    proc.on('exit', function (code) {
      if (code) reject(new Error('Failed: ' + code + ' cmd:' + [cmd, ...args].join(' ') + '\n' + out.join('')))
      else resolve(out.length && out.join(''))
    })
  })
}
