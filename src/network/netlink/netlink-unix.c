#include "netlink.h"

#include <network/sockets/sockets.h>

#include <sys/ioctl.h>
#include <net/if.h>

#include <strings/strings.h>
#include <string.h>

#include <stdlib.h>


#ifdef SIOCGIFNETMASK
static void
netlink_interface_prefix_new(struct netlink_interface *iface) {
  uint32_t unetmask = ntohl(iface->netmask.u32);

  while (unetmask & 0x80000000)
    iface->prefix++, unetmask <<= 1;
}

#endif

static inline void
netlink_interface_network_new(struct netlink_interface *iface) {
  iface->network.u32 = (uint32_t)(iface->addr.u32 & iface->netmask.u32);
}

#ifdef SIOCGIFFLAGS
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

#endif

struct netlink_interface **
netlink_interfaces(void) {
#ifdef SIOCGIFCONF
  struct netlink_interface **ifaces = NULL, *iface = NULL;

  socketfd_t socket = SOCKET_INVALID;

  struct ifconf ifconf = {0};
  struct ifreq *ifreq = NULL;

  size_t nifs, nif;

  if (!(socket = socketfd_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP)))
    goto _return;

  if (ioctl(socket, SIOCGIFCONF, &ifconf) < 0)
    goto _return;
  
  if (!(ifconf.ifc_buf = (char *)calloc((size_t)ifconf.ifc_len, sizeof(char))))
    goto _return;
/*
  ifconf.ifc_buf = buffer;
  ifconf.ifc_len = sizeof(buffer);
*/

  if (ioctl(socket, SIOCGIFCONF, &ifconf) < 0)
    goto _return;

  nifs = (size_t)ifconf.ifc_len/sizeof(struct ifreq);

  if (!(ifaces = (struct netlink_interface **)calloc(nifs+1, sizeof(struct netlink_interface))))
    goto _return;

  for (nif = 0; nif < nifs; nif++) {
    ifreq = &ifconf.ifc_req[nif];

    if (!(ifaces[nif] = (struct netlink_interface *)calloc(1, sizeof(struct netlink_interface))))
      continue;
    iface = (struct netlink_interface *)ifaces[nif];

    iface->addr = *(inetv4_t *)&((struct sockaddr_in *)&ifreq->ifr_addr)->sin_addr;

#ifdef SIOCGIFBRDADDR
    if (ioctl(socket, SIOCGIFBRDADDR, ifreq) >= 0)
      iface->broadcast = *(inetv4_t *)&((struct sockaddr_in *)&ifreq->ifr_broadaddr)->sin_addr;
#endif

#ifdef SIOCGIFNETMASK
    if (ioctl(socket, SIOCGIFNETMASK, ifreq) >= 0)
      iface->netmask = *(inetv4_t *)&((struct sockaddr_in *)&ifreq->ifr_netmask)->sin_addr;

    netlink_interface_prefix_new(iface);
 // iface->prefix = netlink_prefixbynetmask(&iface->netmask);

    netlink_interface_network_new(iface);
 // iface->network.u32 = htonl(ntohl(iface->addr.u32) & (0xFFFFFFFF << (32-iface->prefix)));
#endif

#ifdef SIOCGIFHWADDR
    if (ioctl(socket, SIOCGIFHWADDR, ifreq) >= 0)
      iface->mac = *(macieee802_t *)&ifreq->ifr_hwaddr.sa_data;
#endif

    iface->name = string_new(ifreq->ifr_name);

#ifdef SIOCGIFINDEX
    if (ioctl(socket, SIOCGIFINDEX, ifreq) >= 0)
      iface->index = (uint32_t)ifreq->ifr_ifindex;
#endif

#ifdef SIOCGIFFLAGS
    if (ioctl(socket, SIOCGIFFLAGS, ifreq) >= 0)
      netlink_interface_flags_new(iface, (uint32_t)ifreq->ifr_flags);
#endif

#ifdef SIOCGIFMTU
    if (ioctl(socket, SIOCGIFMTU, ifreq) >= 0)
      iface->mtu = (size_t)ifreq->ifr_mtu;
#endif
  }

_return:
  socketfd_close(socket);
  free(ifconf.ifc_buf);

  return ifaces;
#endif

  return NULL;
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

  if (!iface)
    netlink_seterror(NETLINK_ENOINTERFACE);

  netlink_interfaces_free(ifaces);

  return iface;
}

struct netlink_interface *
netlink_interfacebyindex(uint32_t index) {
  struct netlink_interface **ifaces = NULL, *iface = NULL;
  size_t nif;

  if (!index) {
    netlink_seterror(NETLINK_EINVALINTERFACE);
    return NULL;
  }

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

  if (!iface)
    netlink_seterror(NETLINK_ENOINTERFACE);

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
