#ifndef _NETWORK_NETLINK_NETLINK_UNIX_H
#define _NETWORK_NETLINK_NETLINK_UNIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <network/macieee802/macieee802.h>
#include <network/inet/inet.h>

#include <stddef.h>
#include <stdint.h>


struct netlink_interface {
  inetv4_t network, gateway, broadcast, netmask, addr;
  inetv6_t network6, gateway6, netmask6, addr6;
  macieee802_t mac;

  char *name;

  uint32_t index, flags;
  size_t mtu;

  uint8_t prefix, prefix6;
};

#ifdef __cplusplus
}
#endif

#endif
