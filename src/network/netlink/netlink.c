#include "netlink.h"

#include <stdlib.h>


bool
netlink_interface_isconnected(struct netlink_interface *iface) {
  return netlink_interface_isup(iface) && netlink_interface_isrunning(iface);
}

bool
netlink_interface_isup(struct netlink_interface *iface) {
  return iface->flags & NETLINK_IFUP;
}

bool
netlink_interface_isrunning(struct netlink_interface *iface) {
  return iface->flags & NETLINK_IFRUNNING;
}

bool
netlink_interface_isbroadcast(struct netlink_interface *iface) {
  return iface->flags & NETLINK_IFBROADCAST;
}

bool
netlink_interface_isloopback(struct netlink_interface *iface) {
  return iface->flags & NETLINK_IFLOOPBACK;
}

bool
netlink_interface_ispointtopoint(struct netlink_interface *iface) {
  return iface->flags & NETLINK_IFPOINTTOPOINT;
}

bool
netlink_interface_ismulticast(struct netlink_interface *iface) {
  return iface->flags & NETLINK_IFMULTICAST;
}


void
netlink_interfaces_free(struct netlink_interface **ifaces) {
  size_t nif;

  if (ifaces) {
    for (nif = 0; ifaces[nif]; nif++)
      netlink_interface_free(ifaces[nif]), ifaces[nif] = NULL;

    free(ifaces);
  }
}
