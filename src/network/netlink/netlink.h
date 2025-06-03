#ifndef _NETWORK_NETLINK_NETLINK_H
#define _NETWORK_NETLINK_NETLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined _WIN16 || defined _WIN32 || defined _WIN64 || defined __WIN32__ || defined __TOS_WIN__ || defined __WINDOWS__
  #include "netlink-windows.h"

#elif defined __linux__ || defined __linux
  #include "netlink-linux.h"

#elif defined __APPLE__ && defined __MACH__
  #include "netlink-darwin.h"

#elif defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__ || defined _SYSTYPE_BSD
  #include "netlink-bsd.h"

#else
  #include "netlink-null.h"

#endif

#include "netlink-errors.h"

#include <stdbool.h>

#define NETLINK_IFUP           0x0001
#define NETLINK_IFRUNNING      0x0002
#define NETLINK_IFBROADCAST    0x0004
#define NETLINK_IFLOOPBACK     0x0008
#define NETLINK_IFPOINTTOPOINT 0x0010
#define NETLINK_IFMULTICAST    0x0020


extern bool netlink_interface_isconnected(struct netlink_interface *);
extern bool netlink_interface_isup(struct netlink_interface *);
extern bool netlink_interface_isrunning(struct netlink_interface *);

extern bool netlink_interface_isbroadcast(struct netlink_interface *);
extern bool netlink_interface_isloopback(struct netlink_interface *);
extern bool netlink_interface_ispointtopoint(struct netlink_interface *);
extern bool netlink_interface_ismulticast(struct netlink_interface *);

extern struct netlink_interface *netlink_interfacebyname(const char *);
extern struct netlink_interface *netlink_interfacebyindex(uint32_t);

extern void netlink_interface_free(struct netlink_interface *);

extern struct netlink_interface **netlink_interfaces(void);
extern void netlink_interfaces_free(struct netlink_interface **);

#ifdef __cplusplus
}
#endif

#endif
