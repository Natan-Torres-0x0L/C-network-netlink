#include "netlink-errors.h"

#if defined __cplusplus && __cplusplus < 201103L
  #define thread_local __thread
#endif

#if !defined __cplusplus
  #define thread_local _Thread_local
#endif


thread_local netlink_error_t netlink_error;


const char *
netlink_getstrerror(netlink_error_t error) {
  switch (error) {
    case NETLINK_EINVALINTERFACE:
      return "invalid network interface";

    case NETLINK_ENOINTERFACE:
      return "no such network interface";

    case NETLINK_ENOSUPPORT:
      return "there is no support/implementation for the system";

    case NETLINK_ESYSCALL:
      return syscall_getstrerrno(syscall_geterrno());
  }

  return "unknown error";
}

netlink_error_t
netlink_geterror(void) {
  if (!netlink_error && syscall_geterrno())
    netlink_seterror(NETLINK_ESYSCALL);

  return netlink_error;
}

void
netlink_seterror(netlink_error_t error) {
  netlink_error = error;
}
