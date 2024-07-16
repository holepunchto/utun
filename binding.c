#include <assert.h>
#include <bare.h>
#include <js.h>
#include <uv.h>

#if UTUN_APPLE
#include "apple.h"
#elif UTUN_LINUX
#include "linux.h"
#endif

static js_value_t *
utun_open (js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc == 1);

  char *ifname;
  err = js_get_typedarray_info(env, argv[0], NULL, (void **) &ifname, NULL, NULL, NULL);
  assert(err == 0);

#if UTUN_APPLE
  err = utun_open__apple(ifname);
#elif UTUN_LINUX
  err = utun_open__linux(ifname);
#else
  err = -1;
#endif

  if (err < 0) {
    js_throw_error(env, NULL, "Could not open TUN device");
    return NULL;
  }

  int fd = err;

  js_value_t *result;
  err = js_create_int32(env, fd, &result);
  assert(err == 0);

  return result;
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

  V("open", utun_open)
#undef V

  return exports;
}

BARE_MODULE(utun, utun_exports)
