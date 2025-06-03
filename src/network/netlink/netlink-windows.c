#include "netlink.h"

#include <collections/hashmap/hashmap.h>

// #include <network/sockets/sockets.h>
#include <winsock2.h>
#include <windows.h>

#include <netioapi.h>
#include <iphlpapi.h>

#include <strings/strings.h>
#include <string.h>

#include <wchar.h>

#include <stdlib.h>


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
netlink_interface_network_new(struct netlink_interface *iface) {
  iface->network.u32 = (uint32_t)(iface->addr.u32 & iface->netmask.u32);
}

static inline void
netlink_interface_netmask_new(struct netlink_interface *iface) {
  iface->netmask.u32 = htonl(/* INETV4_UBROADCAST */ 0xFFFFFFFF << (32 - iface->prefix));
}

static inline void
netlink_interface_broadcast_new(struct netlink_interface *iface) {
  iface->broadcast.u32 = (iface->addr.u32 | (iface->netmask.u32 ^ /* INETV4_UBROADCAST */ 0xFFFFFFFF));
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

static inline uint32_t
netlink_interface_flags(PIP_ADAPTER_ADDRESSES adapter_addr) {
  uint32_t flags = 0;

  if (adapter_addr->OperStatus == IfOperStatusUp)
    flags |= NETLINK_IFUP|NETLINK_IFRUNNING;

  switch (adapter_addr->IfType) {
    case IF_TYPE_ETHERNET_CSMACD:
    case IF_TYPE_ISO88025_TOKENRING:
    case IF_TYPE_IEEE80211:
    case IF_TYPE_IEEE1394:
      flags |= NETLINK_IFBROADCAST|NETLINK_IFMULTICAST;
      break;

    case IF_TYPE_PPP:
    case IF_TYPE_TUNNEL:
      flags |= NETLINK_IFPOINTTOPOINT|NETLINK_IFMULTICAST;
      break;

    case IF_TYPE_SOFTWARE_LOOPBACK:
      flags |= NETLINK_IFLOOPBACK|NETLINK_IFMULTICAST;
      break;

    case IF_TYPE_ATM:
      flags |= NETLINK_IFBROADCAST|NETLINK_IFPOINTTOPOINT|NETLINK_IFMULTICAST;
      break;
  }

  return flags;
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

static char *
netlink_wchar_tochar(const wchar_t *wstring) {
  char *string = NULL;
  size_t length;

  if ((length = wcstombs(NULL, wstring, 0)) == (size_t)-1)
    return NULL;

  if (!(string = (char *)calloc(length+1, sizeof(char))))
    return NULL;

  wcstombs(string, wstring, length);

  return string;
}

static struct netlink_interface **
netlink_interfaces_request(void) {
  struct netlink_interface **ifaces = NULL, *iface = NULL;
  netlink_interfaces_hashmap_t *hashmap = NULL;

  PIP_ADAPTER_ADDRESSES adapters_addrs = NULL, adapter_addr = NULL;
  PIP_ADAPTER_UNICAST_ADDRESS_LH unicast_addr = NULL;

// PMIB_IPADDRTABLE ipaddr_table = NULL;

  PMIB_IPFORWARD_TABLE2 ipforward_table = NULL;

// PIP_ADAPTER_INFO adapters_info = NULL, adapter_info = NULL;

  ULONG length = 0 /* 15000 */;
  ULONG entries;

/*
 *  IPHLPAPI_DLL_LINKAGE ULONG GetAdaptersAddresses(
 *    [in]      ULONG                 Family,
 *    [in]      ULONG                 Flags,
 *    [in]      PVOID                 Reserved,
 *    [in, out] PIP_ADAPTER_ADDRESSES AdapterAddresses,
 *    [in, out] PULONG                SizePointer
 *  );
 *
 *  system call needed to:
 *    @ adapter name
 *    @ adapter friendly name
 *    @ adapter flags
 *    @ mtu
 *    @ IPv4 address
 *    @ IPv4 prefix length
 *    @ IPv6 address
 *    @ IPv6 prefix length
 */
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &length) != ERROR_BUFFER_OVERFLOW)
    goto _return;

  if (!(adapters_addrs = (IP_ADAPTER_ADDRESSES *)calloc(1, length)))
    goto _return;

  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters_addrs, &length) != NO_ERROR)
    goto _return;

#ifdef NETLINK_WINDOWS_IPADDRTABLE
/*
 *  IPHLPAPI_DLL_LINKAGE DWORD GetIpAddrTable(
 *    [out]     PMIB_IPADDRTABLE pIpAddrTable,
 *    [in, out] PULONG           pdwSize,
 *    [in]      BOOL             bOrder
 *  );
 *
 *  system call needed to:
 *    @ broadcast address
 */
  if (GetIpAddrTable(NULL, &length, 0) != ERROR_INSUFFICIENT_BUFFER)
    goto _return;

  if (!(ipaddr_table = (PMIB_IPADDRTABLE)calloc(1, length)))
    goto _return;

  if (GetIpAddrTable(ipaddr_table, &length, 0) != NO_ERROR)
    goto _return;
#endif

#ifdef NETLINK_WINDOWS_ADPTER_INFO
/*
 *  IPHLPAPI_DLL_LINKAGE ULONG GetAdaptersInfo(
 *    [out]     PIP_ADAPTER_INFO AdapterInfo,
 *    [in, out] PULONG           SizePointer
 *  );
 *
 *  system call needed to:
 *    @ gateway address
 */
  if (GetAdaptersInfo(NULL, &length) != ERROR_BUFFER_OVERFLOW)
    goto _return;

  if (!(adapters_info = (PIP_ADAPTER_INFO)calloc(1, length)))
    goto _return;

  if (GetAdaptersInfo(adapters_info, &length) != NO_ERROR)
    goto _return;
#endif

/*
 *  IPHLPAPI_DLL_LINKAGE _NETIOAPI_SUCCESS_ NETIOAPI_API GetIpForwardTable2(
 *    [in]  ADDRESS_FAMILY        Family,
 *    [out] PMIB_IPFORWARD_TABLE2 *Table
 *  );
 *
 *  system call needed to:
 *    @ IPv4 gateway address
 *    @ IPv6 gateway address
 */
  if (GetIpForwardTable2(AF_UNSPEC, &ipforward_table) != NO_ERROR)
    goto _return;

  if (!(hashmap = netlink_interfaces_hashmap_new()))
    goto _return;

  for (adapter_addr = adapters_addrs; adapter_addr; adapter_addr = adapter_addr->Next) {
    if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)adapter_addr->IfIndex)))
      continue;

    if (adapter_addr->PhysicalAddressLength != 0)
      iface->mac = *((macieee802_t *)&adapter_addr->PhysicalAddress);

    iface->adapter_name = string_new(adapter_addr->AdapterName);
    iface->name = netlink_wchar_tochar(adapter_addr->FriendlyName);

    iface->flags = netlink_interface_flags(adapter_addr);

    iface->mtu = ((adapter_addr->Mtu > -1) ? (size_t)adapter_addr->Mtu : 0xFFFF);

    for (unicast_addr = (PIP_ADAPTER_UNICAST_ADDRESS_LH)adapter_addr->FirstUnicastAddress; unicast_addr; unicast_addr = unicast_addr->Next)
      if (unicast_addr->Address.lpSockaddr->sa_family == AF_INET) {
        iface->addr = *(inetv4_t *)&((struct sockaddr_in *)unicast_addr->Address.lpSockaddr)->sin_addr;
        iface->prefix = (uint8_t)unicast_addr->OnLinkPrefixLength;
        break;
      }

    netlink_interface_netmask_new(iface);
    netlink_interface_network_new(iface);

    netlink_interface_broadcast_new(iface);

    for (unicast_addr = (PIP_ADAPTER_UNICAST_ADDRESS_LH)adapter_addr->FirstUnicastAddress; unicast_addr; unicast_addr = unicast_addr->Next)
      if (unicast_addr->Address.lpSockaddr->sa_family == AF_INET6) {
        inetv6_t addr6 = *(inetv6_t *)&((struct sockaddr_in6 *)unicast_addr->Address.lpSockaddr)->sin6_addr;

        if (!inetv6_islinklocal(&addr6))
          continue;

        iface->addr6 = addr6;
        iface->prefix6 = (uint8_t)unicast_addr->OnLinkPrefixLength;
        break;
      }

    netlink_interface_netmask6_new(iface);
    netlink_interface_network6_new(iface);
  }

#ifdef NETLINK_WINDOWS_IPADDRTABLE
  for (entries = 0; entries < (ULONG)ipaddr_table->dwNumEntries; entries++) {
    if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)ipaddr_table->table[entries].dwIndex)))
      continue;

 // iface->netmask = *((inetv4_t *)&ipaddr_table->table[entries].dwMask);
 // iface->addr = *((inetv4_t *)&ipaddr_table->table[entries].dwAddr);

    if (iface->flags & NETLINK_IFBROADCAST)
      iface->broadcast.u32 = (iface->addr.u32 | (iface->netmask.u32 ^ 0xFFFFFFFF));

  // iface->network.u32 = (iface->addr.u32 & iface->netmask.u32);
  }
#endif

#ifdef NETLINK_WINDOWS_ADPTER_INFO
  for (adapter_info = adapters_info; adapter_info; adapter_info = adapter_info->Next) {
    if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)adapter_info->Index)))
      continue;

    inetv4_strtou8(adapter_info->GatewayList.IpAddress.String, &iface->gateway);
  }
#endif

  for (entries = 0; entries < ipforward_table->NumEntries; entries++) {
    MIB_IPFORWARD_ROW2 *entry = &ipforward_table->Table[entries];

    if (!(iface = netlink_interfaces_hashmap_get(hashmap, (uint32_t)entry->InterfaceIndex)))
      continue;

    if (entry->DestinationPrefix.PrefixLength == 0)
      switch (entry->NextHop.si_family) {
        case AF_INET:
          iface->gateway = *(inetv4_t *)&((struct sockaddr_in *)&entry->NextHop.Ipv4)->sin_addr;
          break;

        case AF_INET6:
          iface->gateway6 = *(inetv6_t *)&((struct sockaddr_in6 *)&entry->NextHop.Ipv6)->sin6_addr;
          break;
      }
  }

  ifaces = netlink_interfaces_new(hashmap);

_return:
  netlink_interfaces_hashmap_free(hashmap);

  free(adapters_addrs);
// free(ipaddr_table);
  FreeMibTable(ipforward_table);
// free(adapters_info);

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
      iface->adapter_name = string_new(iface->adapter_name);
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

  if (!(ifaces = netlink_interfaces()))
    return NULL;

  for (nif = 0; ifaces[nif]; nif++)
    if (ifaces[nif]->index == index) {
      if (!(iface = (struct netlink_interface *)calloc(1, sizeof(struct netlink_interface))))
        break;

      memcpy(iface, ifaces[nif], sizeof(struct netlink_interface));
      iface->adapter_name = string_new(iface->adapter_name);
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
    if (iface->adapter_name)
      free(iface->adapter_name), iface->adapter_name = NULL;
    if (iface->name)
      free(iface->name), iface->name = NULL;

    free(iface);
  }
}
