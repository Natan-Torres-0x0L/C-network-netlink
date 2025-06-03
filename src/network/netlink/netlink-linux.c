#include "netlink.h"

#include <collections/hashmap/hashmap.h>
#include <collections/queue/queue.h>

#include <network/sockets/sockets.h>

#include <linux/rtnetlink.h>
#include <linux/netlink.h>

#include <linux/if.h>

#include <strings/strings.h>
#include <string.h>

#include <stdlib.h>

#include <unistd.h>


struct __attribute__((packed)) netlink_message {
  struct nlmsghdr nlmsgh;
  struct rtgenmsg rtgenmsg;
};

struct netlink_response {
  struct nlmsghdr *nlmsgh;
  size_t length;
};

struct netlink_request {
// @ collection_queue_t<struct netlink_response>
  queue_t *responses;

  socket_t socket;

  uint16_t sequence;
  uint32_t pid;
};

static inline void netlink_interface_flags_new(struct netlink_interface *, uint32_t);

static inline void netlink_interface_network_new(struct netlink_interface *);
static inline void netlink_interface_netmask_new(struct netlink_interface *);

static inline void netlink_interface_network6_new(struct netlink_interface *);
static inline void netlink_interface_netmask6_new(struct netlink_interface *);

static struct netlink_request *netlink_request_new(void);
static void netlink_request_free(struct netlink_request *);


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

static void
netlink_request_response(struct netlink_request *request, netlink_interfaces_hashmap_t *hashmap) {
  struct netlink_response *response = NULL;
  queue_iterator_t response_iter = NULL;

  struct nlmsghdr *nlmsgh = NULL;
  size_t nlmsgh_length;

  struct rtattr *rtattr = NULL;
  size_t rtattr_length;

  for (response_iter = queue_begin(request->responses); response_iter; response_iter = queue_next(response_iter)) {
    response = (struct netlink_response *)queue_value(response_iter);

    for (nlmsgh = response->nlmsgh, nlmsgh_length = response->length; NLMSG_OK(nlmsgh, nlmsgh_length); nlmsgh = NLMSG_NEXT(nlmsgh, nlmsgh_length)) {
      struct netlink_interface *iface = NULL;

      switch (nlmsgh->nlmsg_type) {
        case RTM_NEWLINK: {
          struct ifinfomsg *ifinfomsg = (struct ifinfomsg *)NLMSG_DATA(nlmsgh);
          rtattr_length = IFLA_PAYLOAD(nlmsgh);

          if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)ifinfomsg->ifi_index)))
            continue;

          for (rtattr = (struct rtattr *)IFLA_RTA(ifinfomsg); RTA_OK(rtattr, rtattr_length); rtattr = RTA_NEXT(rtattr, rtattr_length))
            switch (rtattr->rta_type) {
              case IFLA_IFNAME:
                iface->name = string_new((char *)RTA_DATA(rtattr));
                break;

              case IFLA_ADDRESS:
                iface->mac = *(macieee802_t *)RTA_DATA(rtattr);
                break;

              case IFLA_MTU:
                iface->mtu = *(uint32_t *)RTA_DATA(rtattr);
                break;
            }

          netlink_interface_flags_new(iface, ifinfomsg->ifi_flags);
          break;
        }

        case RTM_NEWADDR: {
          struct ifaddrmsg *ifaddrmsg = (struct ifaddrmsg *)NLMSG_DATA(nlmsgh);
          rtattr_length = IFA_PAYLOAD(nlmsgh);

          if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)ifaddrmsg->ifa_index)))
            continue;

          for (rtattr = IFA_RTA(ifaddrmsg); RTA_OK(rtattr, rtattr_length); rtattr = RTA_NEXT(rtattr, rtattr_length))
            switch (rtattr->rta_type) {
              case IFA_ADDRESS:
                if (ifaddrmsg->ifa_family == AF_INET) {
                  iface->addr = *(inetv4_t *)RTA_DATA(rtattr);
                  iface->prefix = ifaddrmsg->ifa_prefixlen;

                  netlink_interface_netmask_new(iface);
                  netlink_interface_network_new(iface);
                }

                if (ifaddrmsg->ifa_family == AF_INET6) {
                  inetv6_t addr6 = *(inetv6_t *)RTA_DATA(rtattr);

                  if (!inetv6_islinklocal(&addr6) && !inetv6_isloopback(&addr6))
                    continue;

                  iface->addr6 = addr6;
                  iface->prefix6 = ifaddrmsg->ifa_prefixlen;

                  netlink_interface_network6_new(iface);
                  netlink_interface_netmask6_new(iface);
                }

                break;

              case IFA_BROADCAST:
                if (ifaddrmsg->ifa_family == AF_INET)
                  iface->broadcast = *(inetv4_t *)RTA_DATA(rtattr);

                break;
            }

          break;
        }

        case RTM_NEWROUTE: {
          struct rtmsg *rtmsg = (struct rtmsg *)NLMSG_DATA(nlmsgh);
          rtattr_length = RTM_PAYLOAD(nlmsgh);

          for (rtattr = RTM_RTA(rtmsg); RTA_OK(rtattr, rtattr_length); rtattr = RTA_NEXT(rtattr, rtattr_length))
            if (rtattr->rta_type == RTA_OIF) {
              iface = netlink_interfaces_hashmap_get(hashmap, *(uint32_t *)RTA_DATA(rtattr));
              break;
            }

          rtattr_length = RTM_PAYLOAD(nlmsgh);

          for (rtattr = RTM_RTA(rtmsg); RTA_OK(rtattr, rtattr_length); rtattr = RTA_NEXT(rtattr, rtattr_length))
            if (rtattr->rta_type == RTA_GATEWAY) {
              if (rtmsg->rtm_family == AF_INET)
                iface->gateway = *(inetv4_t *)RTA_DATA(rtattr);

              if (rtmsg->rtm_family == AF_INET6)
                iface->gateway6 = *(inetv6_t *)RTA_DATA(rtattr);

              break;
            }

          break;
        }
      }
    }
  }
}

static inline int
netlink_request_send(struct netlink_request *request, uint16_t type, uint8_t family) {
  struct sockaddr_nl netlink_kernel = { .nl_family = AF_NETLINK };

  struct netlink_message message = {
    .nlmsgh = {
      .nlmsg_len      = NLMSG_LENGTH(sizeof(struct rtgenmsg)),
      .nlmsg_type     = type,
      .nlmsg_flags    = NLM_F_REQUEST|NLM_F_DUMP, // |NLM_F_ROOT|NLM_F_MATCH,
      .nlmsg_seq      = ++request->sequence,
    },
    .rtgenmsg = {
      .rtgen_family = family,
    }
  };

  if (socket_sendto(request->socket, &message, message.nlmsgh.nlmsg_len, 0, (struct sockaddr *)&netlink_kernel, sizeof(struct sockaddr_nl)) < 0)
    return -1;

  return 1;
}

static int
netlink_request(struct netlink_request *request, uint16_t type, uint8_t family) {
  static struct sockaddr_nl netlink_user = {0};
  socklen_t netlink_user_size = sizeof(struct sockaddr_nl);

  struct netlink_response *response = NULL;
  struct nlmsghdr *nlmsgh = NULL;
  size_t nlmsgh_count, length;

  uint8_t buffer[8192 /* PAGE_SIZE */] = {0};
  ssize_t received;

/*
  struct iovec iovec = { .iov_base = buffer, .iov_len = sizeof(buffer) };

  struct msghdr msgh = {
    .msg_name    = &netlink_user,
    .msg_namelen = sizeof(struct sockaddr_nl),
    .msg_iov     = &iovec,
    .msg_iovlen  = 1,
  };
*/

  if (netlink_request_send(request, type, family) < 0)
    return -1;

  for (;;) {
 // if ((received = recvmsg(request->socket, &msgh, 0)) <= 0)
   // break;
    if ((received = socket_recvfrom(request->socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&netlink_user, &netlink_user_size, &SOCKET_NONBLOCKING)) <= 0)
      break;
    length = (size_t)received;

    for (nlmsgh = (struct nlmsghdr *)buffer, nlmsgh_count = 0; NLMSG_OK(nlmsgh, received); nlmsgh = NLMSG_NEXT(nlmsgh, received)) {
      if (nlmsgh->nlmsg_pid != request->pid || nlmsgh->nlmsg_seq != request->sequence)
        continue;

      nlmsgh_count++;
      if (nlmsgh->nlmsg_type == NLMSG_DONE)
        break;
    }

    if (!nlmsgh_count)
      continue;

    if (!(response = (struct netlink_response *)calloc(1, sizeof(struct netlink_response)+length)))
      continue;

    response->nlmsgh = (struct nlmsghdr *)memcpy(response+1, buffer, length);
    response->length = length;

    queue_push(request->responses, queue_lvalue(response, sizeof(struct netlink_response *)));
  }

  return 1;
}

static struct netlink_request *
netlink_request_new(void) {
  struct netlink_request *request = NULL;
  struct sockaddr_nl netlink_user = {
    .nl_family = AF_NETLINK,
    .nl_pid    = (uint32_t)getpid(),
  };

  if (!(request = (struct netlink_request *)calloc(1, sizeof(struct netlink_request))))
    goto _return;
  request->pid = netlink_user.nl_pid;

  if ((request->socket = socket_new(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) == SOCKET_INVALID)
    goto _return;

  if (socket_bind(request->socket, (struct sockaddr *)&netlink_user, sizeof(netlink_user)) < 0)
    goto _return;

  if (!(request->responses = queue_new(free)))
    goto _return;

  return request;

_return:
  netlink_request_free(request);
  return NULL;
}

static void
netlink_request_free(struct netlink_request *request) {
  if (request) {
    if (request->socket != SOCKET_INVALID)
      socket_close(request->socket);

    if (request->responses)
      queue_free(request->responses);

    free(request);
  }
}

static inline void
netlink_interface_network_new(struct netlink_interface *iface) {
  iface->network.u32 = (uint32_t)(iface->addr.u32 & iface->netmask.u32);
}

static inline void
netlink_interface_netmask_new(struct netlink_interface *iface) {
  iface->netmask.u32 = htonl(/* INETV4_UBROADCAST */ 0xFFFFFFFF << (32 - iface->prefix));
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
netlink_interface_netmask6_new(struct netlink_interface *iface) {
  uint8_t prefix = iface->prefix6, x11;

  for (x11 = 0; x11 < INETV6_SIZE; x11++)
    if (prefix >= 8)
      iface->netmask6.u8[x11] = 0xFF, prefix -= 8;
    else if (prefix > 0)
      iface->netmask6.u8[x11] = (uint8_t)(0xFF << (8 - prefix)), prefix = 0;
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

static struct netlink_interface **
netlink_interfaces_request(void) {
  netlink_interfaces_hashmap_t *hashmap = NULL;
  struct netlink_request *request = NULL;

  struct netlink_interface **ifaces = NULL;

  if (!(request = netlink_request_new()) || !(hashmap = netlink_interfaces_hashmap_new()))
    goto _return;

  if (netlink_request(request, RTM_GETLINK, AF_PACKET) == -1)
    goto _return;

  if (netlink_request(request, RTM_GETADDR, AF_UNSPEC) == -1)
    goto _return;

  if (netlink_request(request, RTM_GETROUTE, AF_UNSPEC) == -1)
    goto _return;

  netlink_request_response(request, hashmap);

  ifaces = netlink_interfaces_new(hashmap);

_return:
  netlink_interfaces_hashmap_free(hashmap);
  netlink_request_free(request);

  return ifaces;
}

struct netlink_interface **
netlink_interfaces(void) {
  struct netlink_interface **ifaces = NULL;

  if (!(ifaces = netlink_interfaces_request())) {
    netlink_seterror(NETLINK_ESYSCALL);
    return NULL;
  }

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
