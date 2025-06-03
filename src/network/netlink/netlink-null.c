#include "netlink.h"


struct netlink_interface **
netlink_interfaces(void) {
  netlink_seterror(NETLINK_ENOSUPPORT);
  return NULL;
}

struct netlink_interface *
netlink_interfacebyname(const char *name) {
  netlink_seterror(NETLINK_ENOSUPPORT);
  return NULL;
}

struct netlink_interface *
netlink_interfacebyindex(uint32_t index) {
  netlink_seterror(NETLINK_ENOSUPPORT);
  return NULL;
}

void
netlink_interface_free(struct netlink_interface *iface) {
  netlink_seterror(NETLINK_ENOSUPPORT);
}
