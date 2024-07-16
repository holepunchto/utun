#include <uv.h>
#include <js.h>
#include <bare.h>
#include <assert.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>

#ifdef __APPLE__
#include <sys/kern_event.h>
#include <sys/socket.h>
#include <sys/kern_control.h>
#include <ctype.h>

#define UTUN_CONTROL_NAME "com.apple.net.utun_control"
#define UTUN_OPT_IFNAME 2

static int
simpletun_open (char *ifname) {
  struct sockaddr_ctl addr;
  struct ctl_info info;
  socklen_t ifname_len = sizeof(ifname);
  int fd = -1;
  int err = 0;

  fd = socket (PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
  if (fd < 0) return fd;

  bzero(&info, sizeof (info));
  strncpy(info.ctl_name, UTUN_CONTROL_NAME, MAX_KCTL_NAME);

  err = ioctl(fd, CTLIOCGINFO, &info);
  if (err != 0) goto on_error;

  addr.sc_len = sizeof(addr);
  addr.sc_family = AF_SYSTEM;
  addr.ss_sysaddr = AF_SYS_CONTROL;
  addr.sc_id = info.ctl_id;
  addr.sc_unit = 0;

  err = connect(fd, (struct sockaddr *)&addr, sizeof (addr));
  if (err != 0) goto on_error;

  // TODO: forward ifname (we just expect it to be utun0 for now...)
  err = getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &ifname_len);
  if (err != 0) goto on_error;

  ifname[ifname_len] = '\0';

  err = fcntl(fd, F_SETFL, O_NONBLOCK);
  if (err != 0) goto on_error;

  fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (err != 0) goto on_error;

on_error:
  if (err != 0) {
    close(fd);
    return err;
  }

  return fd;
}

#elif __linux__
#include <linux/if.h>
#include <linux/if_tun.h>

static int
simpletun_open(char *ifname) {
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("open /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    if (*ifname) {
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    }

    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
        perror("ioctl TUNSETIFF");
        close(fd);
        return err;
    }

    strcpy(ifname, ifr.ifr_name);

    err = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (err != 0) {
        perror("fcntl F_SETFL");
        close(fd);
        return err;
    }

    err = fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (err != 0) {
        perror("fcntl F_SETFD");
        close(fd);
        return err;
    }

    return fd;

on_error:
    if (fd != -1) {
        close(fd);
    }
    return err;
}

#else
#error "Unsupported platform"
#endif

static js_value_t *
utun_native_open (js_env_t *env, js_callback_info_t *info) {
  size_t argc = 1;
  js_value_t *argv[1];

  js_get_callback_info(env, info, &argc, argv, NULL, NULL);

  char *ifname;
  js_get_typedarray_info(env, argv[0], NULL, (void **) &ifname, NULL, NULL, NULL);

  int fd = simpletun_open(ifname);

  js_value_t *ret;
  js_create_int32(env, fd, &ret);

  return ret;
}

static js_value_t *
utun_native_exports (js_env_t *env, js_value_t *exports) {
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, NULL, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("open", utun_native_open)
#undef V

  return exports;
}

BARE_MODULE(utun_native, utun_native_exports)
