#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/kern_event.h>
#include <sys/socket.h>
#include <stdint.h>
#include <sys/uio.h>
#include <assert.h>

#define UTUN_CONTROL_NAME "com.apple.net.utun_control"
#define UTUN_OPT_IFNAME   2

static inline int
utun_open__apple (char *ifname) {
  struct sockaddr_ctl addr;
  struct ctl_info info;
  socklen_t ifname_len = sizeof(ifname);
  int fd = -1;
  int err = 0;

  fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
  if (fd < 0) return fd;

  bzero(&info, sizeof(info));
  strncpy(info.ctl_name, UTUN_CONTROL_NAME, MAX_KCTL_NAME);

  err = ioctl(fd, CTLIOCGINFO, &info);
  if (err != 0) goto err;

  addr.sc_len = sizeof(addr);
  addr.sc_family = AF_SYSTEM;
  addr.ss_sysaddr = AF_SYS_CONTROL;
  addr.sc_id = info.ctl_id;
  addr.sc_unit = 0;

  err = connect(fd, (struct sockaddr *) &addr, sizeof(addr));
  if (err != 0) goto err;

  // TODO: forward ifname (we just expect it to be utun0 for now...)
  err = getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &ifname_len);
  if (err != 0) goto err;

  ifname[ifname_len] = '\0';

  fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (err != 0) goto err;

err:
  if (err != 0) {
    close(fd);
    return err;
  }

  return fd;
}

static inline ssize_t
utun_read__apple (const int fd, char *buffer, const size_t len) {
  uint8_t protocol_info[4];
  struct iovec iov[2];

  iov[0].iov_base = protocol_info; // read value ignored for now
  iov[0].iov_len = 4;

  iov[1].iov_base = buffer;
  iov[1].iov_len = len;

  ssize_t n = readv(fd, iov, 2);

  if (n < 1) {
    return n;
  } else {
    assert(n > 4);
    return n - 4;
  }
}

static inline ssize_t
utun_write__apple (const int fd, const char *buffer, const size_t len) {
  uint8_t protocol_info[4] = {0};

  // IPv4
  if (buffer[0] == 0x45) *((uint32_t *) protocol_info) = 0x02000000;

  struct iovec iov[2];
  iov[0].iov_base = protocol_info;
  iov[0].iov_len = 4;

  iov[1].iov_base = (void *)buffer;
  iov[1].iov_len = len;

  ssize_t n = writev(fd, iov, 2);

  if (n < 1) {
    return n;
  } else {
    assert(n > 4);
    return n - 4;
  }
}
