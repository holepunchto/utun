# benchmark

These tools generate metrics by sending udp packets with unique Id's and measure
how many packets make it through.

### udxburst (IPv4 test)

Start server on computer 1 (192.168.0.1):
```bash
bare udxburst.js
```

Send testdata from computer 2:

```
bare udxburst.js 192.168.0.1 100
```

- `100` is the burst rate in MBits/sec


### ipcburst (capture)

Samples `utun.on('data')` capture speed and/or rx-buffers.

pop two bash tabs/windows on 1 computer.

start raw packet receiver in tab1:
```
sudo bare ipcburst.js
```

Send bursts to vnet in tab2:

```
bare udxburst.js 10.22.0.99
```

### ipcburst (inject)

Tests `utun.tryWrite()` throughput and/or tx-buffers.

Use one computer.


plain server in tab1:
```
bare udxburst.js
```

packet injector in tab2:

```
sudo ipcburst.js 100
```

- `100` is the burst rate in MBits/sec


