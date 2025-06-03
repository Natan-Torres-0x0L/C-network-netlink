#ifndef _NETWORK_NETLINK_NETLINK_ERRORS_H
#define _NETWORK_NETLINK_NETLINK_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <syscall-errno/syscall-errno.h>


typedef enum {
  NETLINK_EUNKNOWN = -5, NETLINK_ESYSCALL, NETLINK_ENOSUPPORT, NETLINK_ENOINTERFACE, NETLINK_EINVALINTERFACE,
} netlink_error_t;


extern const char *netlink_getstrerror(netlink_error_t);
extern netlink_error_t netlink_geterror(void);
extern void netlink_seterror(netlink_error_t);

#ifdef __cplusplus
}
#endif

#endif

