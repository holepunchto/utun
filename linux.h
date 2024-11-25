#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static inline int
utun_open__linux (char *ifname) {
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

  if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
    perror("ioctl TUNSETIFF");
    close(fd);
    return err;
  }

  strncpy(ifname, ifr.ifr_name, IFNAMSIZ);

  err = fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (err != 0) {
    perror("fcntl F_SETFD");
    close(fd);
    return err;
  }

  return fd;
}
