#include "netlink.h"

#include <collections/hashmap/hashmap.h>

#include <network/sockets/sockets.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <sys/sysctl.h>
#include <sys/param.h>

#include <unistd.h>

#include <net/if_dl.h>
#include <net/if.h>
#include <net/route.h>

#include <strings/strings.h>
#include <string.h>

#include <stddef.h>
#include <stdlib.h>

#include <stdio.h>


#define SALIGN (sizeof(long)-1)
#define SA_RLEN(sa) ((sa)->sa_len ? ((sa->sa_len+SALIGN) & ~SALIGN) : (SALIGN+1))


typedef hashmap_t netlink_interfaces_hashmap_t;


static struct netlink_interface *
netlink_interfaces_hashmap_get(netlink_interfaces_hashmap_t *hashmap, uint32_t ifindex) {
#define hashmap_insert(hashmap, index, iface) hashmap_insert(hashmap, hashmap_clkey(&index, sizeof(uint32_t)), hashmap_rvalue(iface, sizeof(struct netlink_interface *)))
  struct netlink_interface *iface = NULL;

  if ((iface = (struct netlink_interface *)hashmap_get(hashmap, &ifindex, sizeof(uint32_t))))
    return iface;

  if (!(iface = (struct netlink_interface *)calloc(1, sizeof(struct netlink_interface))))
    goto _return;
  iface->index = ifindex;

  if (hashmap_insert(hashmap, iface->index, iface) == -1)
    goto _return;

  return iface;

_return:
  netlink_interface_free(iface);
  return NULL;
}

static inline netlink_interfaces_hashmap_t *
netlink_interfaces_hashmap_new(void) {
  return hashmap_new(free, NULL);
}

static inline void
netlink_interfaces_hashmap_free(netlink_interfaces_hashmap_t *hashmap) {
  hashmap_free(hashmap);
}

static inline void
netlink_interface_prefix_new(struct netlink_interface *iface) {
  uint8_t x11, x22;

  for (x11 = 0; x11 < INETV4_SIZE; x11++) {
    x22 = iface->netmask.u8[x11];
    while (x22) {
      iface->prefix += (x22 & 1);
      x22 >>= 1;
    }
  }
}

static inline void
netlink_interface_network_new(struct netlink_interface *iface) {
  iface->network.u32 = (uint32_t)(iface->addr.u32 & iface->netmask.u32);
}

static inline void
netlink_interface_prefix6_new(struct netlink_interface *iface) {
  uint8_t x11, x22;
  uint8_t prefix6 = 0;

  for (x11 = 0; x11 < INETV6_SIZE; x11++) {
    x22 = iface->netmask6.u8[x11];
    while (x22) {
      prefix6 += (x22 & 1);
      x22 >>= 1;
    }
  }

  iface->prefix6 = prefix6;
}

static inline void
netlink_interface_network6_new(struct netlink_interface *iface) {
  uint8_t complement = (iface->prefix6 / 8), remaining = (iface->prefix6 % 8);

  iface->network6 = iface->addr6;

  memset(&iface->network6.u8[complement], 0, INETV6_SIZE-complement);

  if (remaining)
    iface->network6.u8[remaining] &= (uint8_t)(0xFF << (8 - remaining)) & 0xFF;
}

static inline void
netlink_interface_flags_new(struct netlink_interface *iface, uint32_t flags) {
  if (flags & IFF_UP)
    iface->flags |= NETLINK_IFUP;

  if (flags & IFF_BROADCAST)
    iface->flags |= NETLINK_IFBROADCAST;

  if (flags & IFF_LOOPBACK)
    iface->flags |= NETLINK_IFLOOPBACK;

  if (flags & IFF_POINTOPOINT)
    iface->flags |= NETLINK_IFPOINTTOPOINT;

  if (flags & IFF_MULTICAST)
    iface->flags |= NETLINK_IFMULTICAST;

  if (flags & IFF_RUNNING)
    iface->flags |= NETLINK_IFRUNNING;
}

static struct netlink_interface **
netlink_interfaces_new(netlink_interfaces_hashmap_t *hashmap) {
  struct netlink_interface **ifaces = NULL;
  hashmap_iterator_t ifaces_iter = NULL;
  size_t nif;

  if (!(ifaces = (struct netlink_interface **)calloc(hashmap_size(hashmap)+1, sizeof(struct netlink_interface *))))
    return NULL;

  for (ifaces_iter = hashmap_begin(hashmap), nif = 0; ifaces_iter; ifaces_iter = hashmap_next(ifaces_iter), nif++)
    ifaces[nif] = (struct netlink_interface *)hashmap_value(ifaces_iter);

  return ifaces;
}

struct netlink_interface **
netlink_interfaces(void) {
  int request[] = { CTL_NET, PF_ROUTE, 0, AF_UNSPEC, NET_RT_IFLIST, 0 };
  
  struct rt_msghdr *rtmsgh = NULL;

  uint8_t *buffer = NULL, *next = NULL;
  size_t length = 0;

  netlink_interfaces_hashmap_t *hashmap = NULL;
  struct netlink_interface **ifaces = NULL;

  if (!(hashmap = netlink_interfaces_hashmap_new()))
    goto _return;

  if (sysctl(request, sizeof(request)/sizeof(request[0]), NULL, &length, NULL, 0) < 0)
    goto _return;

  if (!(buffer = (uint8_t *)calloc(length, sizeof(uint8_t))))
    goto _return;

  if (sysctl(request, sizeof(request)/sizeof(request[0]), buffer, &length, NULL, 0) < 0)
    goto _return;

  for (next = buffer; next < buffer+length; next += rtmsgh->rtm_msglen) {
    struct netlink_interface *iface = NULL;
    rtmsgh = (struct rt_msghdr *)next;

    if (rtmsgh->rtm_version != RTM_VERSION)
      continue;

    switch (rtmsgh->rtm_type) {
      case RTM_IFINFO: {
        struct if_msghdr *ifmsgh = (struct if_msghdr *)rtmsgh;

        if (ifmsgh->ifm_addrs & RTA_IFP) {
          struct sockaddr_dl *datalink_addr = (struct sockaddr_dl *)(ifmsgh+1);
          char ifname[IF_NAMESIZE] = {0};

          if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)ifmsgh->ifm_index)))
            continue;

          string_write(ifname, datalink_addr->sdl_data, datalink_addr->sdl_nlen);
          iface->name = string_new(ifname);

          iface->mac = *(macieee802_t *)((uint8_t *)datalink_addr->sdl_data+datalink_addr->sdl_nlen);
          iface->index = (uint32_t)ifmsgh->ifm_index;

          netlink_interface_flags_new(iface, (uint32_t)ifmsgh->ifm_flags);

          iface->mtu = (size_t)ifmsgh->ifm_data.ifi_mtu;
        }

        break;
      }

#define RTA_MASKS (RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_IFA | RTA_BRD)
 
      case RTM_NEWADDR: {
        struct ifa_msghdr *ifamsgh = (struct ifa_msghdr *)rtmsgh;
        char *payload = (char *)(ifamsgh+1), *offset = NULL;
        size_t rtax;

        if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)ifamsgh->ifam_index)))
          continue;

        for (offset = payload, rtax = 0; rtax < RTAX_MAX; rtax++) {
          struct sockaddr *socket_addr = (struct sockaddr *)offset;
          size_t length = SA_RLEN(socket_addr);

          if ((RTA_MASKS & (ifamsgh->ifam_addrs & (1 << rtax))) == 0)
            continue;

          switch (rtax) {
            case RTAX_IFA: {
              if (socket_addr->sa_family == AF_INET)
                iface->addr = *(inetv4_t *)&((struct sockaddr_in *)socket_addr)->sin_addr;

              if (socket_addr->sa_family == AF_INET6)
                iface->addr6 = *(inetv6_t *)&((struct sockaddr_in6 *)socket_addr)->sin6_addr;

              break;
            }

         /*
            case RTAX_DST: {
              if (socket_addr->sa_family == AF_INET)
                iface->gateway = *(inetv4_t *)&((struct sockaddr_in *)socket_addr)->sin_addr;

              if (socket_addr->sa_family == AF_INET6)
                iface->gateway6 = *(inetv6_t *)&((struct sockaddr_in6 *)socket_addr)->sin6_addr;

              break;
            }
          */

            case RTAX_GATEWAY: {
              if (socket_addr->sa_family == AF_INET)
                iface->gateway = *(inetv4_t *)&((struct sockaddr_in *)socket_addr)->sin_addr;

              if (socket_addr->sa_family == AF_INET6)
                iface->gateway6 = *(inetv6_t *)&((struct sockaddr_in6 *)socket_addr)->sin6_addr;

              break;
            }

            case RTAX_NETMASK: {
              if (socket_addr->sa_family == AF_INET) {
                iface->netmask = *(inetv4_t *)&((struct sockaddr_in *)socket_addr)->sin_addr;
                netlink_interface_prefix_new(iface);
              }

              if (socket_addr->sa_family == AF_INET6) {
                iface->netmask6 = *(inetv6_t *)&((struct sockaddr_in6 *)socket_addr)->sin6_addr;
                netlink_interface_prefix6_new(iface);
              }

              break;
            }

            case RTAX_BRD: {
              if (socket_addr->sa_family == AF_INET)
                iface->broadcast = *(inetv4_t *)&((struct sockaddr_in *)socket_addr)->sin_addr;

              break;
            }
          }

          offset += length;
        }

        netlink_interface_network_new(iface);
        netlink_interface_network6_new(iface);

        break;
      }
    }
  }

  ifaces = netlink_interfaces_new(hashmap);

_return:
  netlink_interfaces_hashmap_free(hashmap);
  free(buffer);

  return ifaces;
}

struct netlink_interface *
netlink_interfacebyname(const char *name) {
  struct netlink_interface **ifaces = NULL, *iface = NULL;
  size_t nif;

  if (!(ifaces = netlink_interfaces()))
    return NULL;

  for (nif = 0; ifaces[nif]; nif++)
    if (string_equals(ifaces[nif]->name, name, true)) {
      if (!(iface = (struct netlink_interface *)calloc(1, sizeof(struct netlink_interface))))
        break;

      memcpy(iface, ifaces[nif], sizeof(struct netlink_interface));
      iface->name = string_new(iface->name);

      break;
    }

  netlink_interfaces_free(ifaces);

  return iface;
}

struct netlink_interface *
netlink_interfacebyindex(uint32_t index) {
  struct netlink_interface **ifaces = NULL, *iface = NULL;
  size_t nif;

  if (!(ifaces = netlink_interfaces()))
    return NULL;

  for (nif = 0; ifaces[nif]; nif++)
    if (ifaces[nif]->index == index) {
      if (!(iface = (struct netlink_interface *)calloc(1, sizeof(struct netlink_interface))))
        break;

      memcpy(iface, ifaces[nif], sizeof(struct netlink_interface));
      iface->name = string_new(iface->name);

      break;
    }

  netlink_interfaces_free(ifaces);
  
  return iface;
}

void
netlink_interface_free(struct netlink_interface *iface) {
  if (iface) {
    if (iface->name)
      free(iface->name), iface->name = NULL;

    free(iface);
  }
}
