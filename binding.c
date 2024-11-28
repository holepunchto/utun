#include "utf.h"
#include <assert.h>
#include <bare.h>
#include <js.h>
#include <sys/select.h>
#include <uv.h>
#include <stdlib.h>
#include <stdatomic.h>


#define IFNAME_MAXLEN 16

#if UTUN_APPLE
#include "apple.h"
#elif UTUN_LINUX
#include "linux.h"
#endif

// tuned using benchmark/[ipc|udx]burst.js
#define TX_QUEUE_LEN 32
#define RX_QUEUE_LEN 256
#define READ_WAKE_USEC 25000 // 25ms

typedef struct {
  size_t len;
  char *buffer;
  js_ref_t *buffer_ref;
  js_ref_t *callback;
} tx_packet_t;

typedef struct {
  uint16_t len;
  char buffer[2048 - sizeof(uint16_t)];
} rx_packet_t;

typedef struct {
  js_env_t *env;
  js_ref_t *ctx;

  js_ref_t *on_read;
  js_ref_t *on_flush;

  int fd;
  char ifname[IFNAME_MAXLEN];

  atomic_int closing;

  struct {
    uv_async_t drain;
    uv_async_t flush;
  } signals;

  uv_thread_t reader_id;
  uv_thread_t writer_id;

  uv_sem_t write_wait;

  struct {
    tx_packet_t queue[TX_QUEUE_LEN];

    atomic_int flushed;
    atomic_int pending;
    atomic_int sent;
  } tx;

  struct {
    rx_packet_t *queue;
    js_ref_t *buffer_ref;

    atomic_int captured;
    atomic_int drained;
  } rx;

  struct {
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t rx_drop; // filtered + relieved backpressure

    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t tx_reject;
  } stats;
} tunnel_t;

void
utun__read_loop (void* arg) {
  tunnel_t* tun = arg;
  int fd = tun->fd;

  fd_set read_set;
  struct timeval timeout = {0};

  int n = 0;
  while (!tun->closing) {
    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);

    if (!timeout.tv_usec) timeout.tv_usec = READ_WAKE_USEC;

    n = select(fd + 1, &read_set, NULL, NULL, &timeout);
    assert(n >= 0);

    if (n == 0) continue; // timeout

    assert(FD_ISSET(fd, &read_set));

    rx_packet_t *pkt = &tun->rx.queue[tun->rx.captured];

#if UTUN_APPLE
    n = utun_read__apple(fd, pkt->buffer, sizeof(pkt->buffer));
#elif UTUN_LINUX
    n = utun_read__linux(fd, pkt->buffer, sizeof(pkt->buffer));
#else
    n = -1;
#endif

    if (n < 1) goto drop;

    pkt->len = n;

    if (pkt->buffer[0] != 0x45) goto drop; // skip non-ipv4 for now.

    const int next = (tun->rx.captured + 1) % RX_QUEUE_LEN;
    if (tun->rx.drained == next) goto drop; // buffer full

    tun->rx.captured = next;
    tun->stats.rx_packets++;
    tun->stats.rx_bytes += n;

    uv_async_send(&tun->signals.drain);

    continue; // capture next

drop:
    tun->stats.rx_drop++;

    if (n < 0 && errno != EINTR) {
      perror("read error");
      printf("n: %i, errno: %i\n", n, errno);
      assert(0); // fatal, stop execution
    }
  }
}

void
utun__on_drain (uv_async_t *handle) {
  int err = 0;
  tunnel_t *tun = handle->data;
  js_env_t *env = tun->env;

  if (tun->closing) return;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(env, tun->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_message;
  err = js_get_reference_value(env, tun->on_read, &on_message);
  assert(err == 0);

  while (tun->rx.drained != tun->rx.captured) {
    const int current = tun->rx.drained;

    rx_packet_t *pkt = &tun->rx.queue[current];

    js_value_t *argv[1];

    char *output_buf;
    err = js_create_arraybuffer(env, pkt->len, (void **) &output_buf, &argv[0]);
    assert(err == 0);

    memcpy(output_buf, pkt->buffer, pkt->len);

    tun->rx.drained = (current + 1) % RX_QUEUE_LEN;

    err = js_call_function(env, ctx, on_message, 1, argv, NULL);
    assert(err == 0);
  }

  js_close_handle_scope(env, scope);
}

void
utun__write_loop(void* arg) {
  tunnel_t *tun = arg;
  const int fd = tun->fd;

  while (!tun->closing) {
    if (tun->tx.sent == tun->tx.pending) { // all caught up
      uv_sem_wait(&tun->write_wait);
      continue;
    }

    tx_packet_t *msg = &tun->tx.queue[tun->tx.sent];

    int n;
#if UTUN_APPLE
    n = utun_write__apple(fd, msg->buffer, msg->len);
#elif UTUN_LINUX
    n = utun_write__linux(fd, msg->buffer, msg->len);
#else
    n = -1;
#endif

    assert(n == msg->len);

    tun->stats.tx_packets++;
    tun->stats.tx_bytes += msg->len;

    int do_flush = tun->tx.sent == tun->tx.flushed;
    tun->tx.sent = (tun->tx.sent + 1) % TX_QUEUE_LEN; // next
    if (do_flush) uv_async_send(&tun->signals.flush);
  }
}

void
utun__on_flush (uv_async_t *handle) {
  int err;
  tunnel_t *tun = handle->data;
  js_env_t *env = tun->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  js_get_null(env, &ctx);

  while (tun->tx.flushed != tun->tx.sent) {
    tx_packet_t *msg = &tun->tx.queue[tun->tx.flushed];
    err = js_delete_reference(env, msg->buffer_ref);
    assert(err == 0);

    if (msg->callback) {
      js_value_t *cb;
      err = js_get_reference_value(env, tun->on_flush, &cb);
      assert(err == 0);

      js_value_t *argv[1];
      err = js_get_reference_value(env, msg->callback, &argv[0]);
      assert(err == 0);

      if (!tun->closing) {
        err = js_call_function(env, ctx, cb, 1, argv, NULL);
        assert(err == 0);
      }

      err = js_delete_reference(env, msg->callback);
      assert(err == 0);
    }

    tun->tx.flushed = (tun->tx.flushed + 1) % TX_QUEUE_LEN;
  }

  err = js_close_handle_scope(env, scope);
  assert(err == 0);

  // poke write-thread if sleeping due to underflush
  if (tun->tx.pending != tun->tx.sent) uv_sem_post(&tun->write_wait);
}

static js_value_t *
utun_open (js_env_t *env, js_callback_info_t *info) {
  int err;

  js_value_t *handle;
  tunnel_t *tun;

  err = js_create_arraybuffer(env, sizeof(tunnel_t), (void **) &tun, &handle);
  assert(err == 0);

  memset(tun, 0, sizeof(tunnel_t));

  tun->env = env;

  size_t argc = 4; // context(this), on_message, on_flush, [ifname]
  js_value_t *argv[4];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);
  assert(argc > 2);

  if (argc > 3) { // ifname provided
    err = js_get_value_string_utf8(env, argv[3], (utf8_t *) tun->ifname, IFNAME_MAXLEN, NULL);
    assert(err == 0);
  }

#if UTUN_APPLE
  err = utun_open__apple(tun->ifname);
#elif UTUN_LINUX
  err = utun_open__linux(tun->ifname);
#else
  err = -1;
#endif

  if (err < 0) {
    js_throw_error(env, NULL, "Could not open TUN device");
    return NULL;
  }

  tun->fd = err;

  err = js_create_reference(env, argv[0], 1, &tun->ctx);
  assert(err == 0);

  err = js_create_reference(env, argv[1], 1, &tun->on_read);
  assert(err == 0);

  err = js_create_reference(env, argv[2], 1, &tun->on_flush);
  assert(err == 0);

  js_value_t *receive_buffer;
  err = js_create_arraybuffer(env, RX_QUEUE_LEN * sizeof(rx_packet_t), (void **) &tun->rx.queue, &receive_buffer);
  assert(err == 0);

  err = js_create_reference(env, receive_buffer, 1, &tun->rx.buffer_ref);
  assert(err == 0);

  uv_loop_t *loop;
  err = js_get_env_loop(env, &loop);
  assert(err == 0);

  err = uv_async_init(loop, &tun->signals.drain, utun__on_drain);
  assert(err == 0);

  tun->signals.drain.data = (void *) tun;

  err = uv_async_init(loop, &tun->signals.flush, utun__on_flush);
  assert(err == 0);

  tun->signals.flush.data = (void *) tun;

  err = uv_sem_init(&tun->write_wait, 0);
  assert(err == 0);

  err = uv_thread_create(&tun->reader_id, utun__read_loop, tun);
  assert(err == 0);

  err = uv_thread_create(&tun->writer_id, utun__write_loop, tun);
  assert(err == 0);

  return handle;
}

static js_value_t *
utun_write (js_env_t *env, js_callback_info_t *info) {
  int err;
  js_value_t *ret;
  size_t argc = 3;
  js_value_t *argv[3]; // handle, data, onflush

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);
  assert(argc > 1);

  tunnel_t *tun;
  err = js_get_arraybuffer_info(env, argv[0], (void **) &tun, NULL);
  assert(err == 0);

  tx_packet_t *msg;
  int i = 0;

  if (tun->closing) {
    i = -1;
    goto reject;
  }

  uint32_t next = (tun->tx.pending + 1) % TX_QUEUE_LEN;

  if (next == tun->tx.flushed) { // would overflow
    // attempt immediate flush
    if (tun->tx.sent != tun->tx.flushed) {
      utun__on_flush(&tun->signals.flush);
    }

    // queue still full
    if (next == tun->tx.flushed) {
      tun->stats.tx_reject++;
      goto reject; // discard packet
    }
  }

  msg = &tun->tx.queue[tun->tx.pending];
  memset(msg, 0, sizeof(tx_packet_t));

  // populate message fields
  err = js_get_typedarray_info(tun->env, argv[1], NULL, (void **) &msg->buffer, &msg->len, NULL, NULL);
  assert(err == 0);

  err = js_create_reference(env, argv[1], 1, &msg->buffer_ref);
  assert(err == 0);

  if (argc > 2) {
    err = js_create_reference(env, argv[2], 1, &msg->callback);
    assert(err == 0);
  }

  int nudge_writer = tun->tx.sent == tun->tx.pending;
  tun->tx.pending = next;
  if (nudge_writer) uv_sem_post(&tun->write_wait);

  ++i;

reject:

  js_create_int32(env, i, &ret);
  return ret;
}

js_value_t *
utun_close (js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);
  assert(argc == 1);

  tunnel_t *tun;
  err = js_get_arraybuffer_info(env, argv[0], (void **) &tun, NULL);
  assert(err == 0);

  tun->closing = 1;

  uv_sem_post(&tun->write_wait);

  err = uv_thread_join(&tun->writer_id); // does not block
  assert(err == 0);

  uv_sem_destroy(&tun->write_wait);

  tun->tx.sent = tun->tx.pending; // mark everything sent
  utun__on_flush(&tun->signals.flush); // release all usercb refs

  err = uv_thread_join(&tun->reader_id); // blocks max 25ms
  assert(err == 0);

  err = close(tun->fd);
  assert(err == 0);

  err = js_delete_reference(env, tun->ctx);
  assert(err == 0);

  err = js_delete_reference(env, tun->on_read);
  assert(err == 0);

  err = js_delete_reference(env, tun->on_flush);
  assert(err == 0);

  err = js_delete_reference(env, tun->rx.buffer_ref);
  assert(err == 0);

  // async confirmation
  uv_close((uv_handle_t *) &tun->signals.drain, NULL);
  uv_close((uv_handle_t *) &tun->signals.flush, NULL);

  js_value_t *ret;
  js_get_null(env, &ret);
  return ret;
}

js_value_t *
utun_info (js_env_t *env, js_callback_info_t *info) {
  size_t argc = 2;
  js_value_t *argv[2];
  int err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  tunnel_t *tun;
  err = js_get_arraybuffer_info(env, argv[0], (void **) &tun, NULL);
  assert(err == 0);

  js_value_t *ret;
  err = js_create_object(env, &ret);
  assert(err == 0);

  js_value_t *val;

  err = js_create_string_utf8(env, (utf8_t*) tun->ifname, strlen(tun->ifname), &val);
  assert(err == 0);

  err = js_set_named_property(env, ret, "name", val);
  assert(err == 0);

#define set_int32(name, value) \
  { \
    err = js_create_int32(env, value, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, ret, name, val); \
    assert(err == 0); \
  }

  set_int32("rxBytes", tun->stats.rx_bytes);
  set_int32("rxPackets", tun->stats.rx_packets);
  set_int32("rxDrop", tun->stats.rx_drop);

  set_int32("txBytes", tun->stats.tx_bytes);
  set_int32("txPackets", tun->stats.tx_packets);
  set_int32("txRejected", tun->stats.tx_reject);

#undef set_int32

  _Bool do_reset = 0;
  if (argc > 1) {
    err = js_coerce_to_boolean(env, argv[1], &argv[1]);
    assert(err == 0);
    err = js_get_value_bool(env, argv[1], &do_reset);
    assert(err == 0);
  }

  if (do_reset) memset(&tun->stats, 0, sizeof(tun->stats));

  return ret;
}

static js_value_t *
utun_exports (js_env_t *env, js_value_t *exports) {
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, NULL, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("info", utun_info)
  V("open", utun_open)
  V("write", utun_write)
  V("close", utun_close)
#undef V

  return exports;
}

BARE_MODULE(utun, utun_exports)
