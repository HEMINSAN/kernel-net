/*
 * 	NET3	Protocol independent device support routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dahinds@users.sourceforge.net>
 *		Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *		Adam Sulmicki <adam@cfar.umd.edu>
 *              Pekka Riikonen <priikone@poesidon.pspt.fi>
 *
 *	Changes:
 *              D.J. Barrow     :       Fixed bug where dev->refcnt gets set
 *              			to 2 if register_netdev gets called
 *              			before net_dev_init & also removed a
 *              			few lines of code in the process.
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant
 *					stunts to keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into
 *					drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *		Alan Cox	: 	Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before
 *					calling netif_rx. Saves a function
 *					call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *		Alan Cox	: 	Fixed nasty side effect of device close
 *					changes.
 *		Rudi Cilibrasi	:	Pass the right thing to
 *					set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to
 *					make it work out on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *		Andi Kleen	:	Fix error reporting for SIOCGIFCONF
 *	    Michael Chastain	:	Fix signed/unsigned for SIOCGIFCONF
 *		Cyrus Durgin	:	Cleaned for KMOD
 *		Adam Sulmicki   :	Bug Fix : Network Device Unload
 *					A network device unload needs to purge
 *					the backlog queue.
 *	Paul Rusty Russell	:	SIOCSIFNAME
 *              Pekka Riikonen  :	Netdev boot-time settings code
 *              Andrew Morton   :       Make unregister_netdevice wait
 *              			indefinitely on dev->refcnt
 * 		J Hadi Salim	:	- Backlog queue sampling
 *				        - netif_rx() feedback
 */
//网络设备注册、输入、输出等接口在该.c里面
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/if_bridge.h>
#include <linux/if_macvlan.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <net/checksum.h>
#include <net/xfrm.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/netpoll.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <net/wext.h>
#include <net/iw_handler.h>
#include <asm/current.h>
#include <linux/audit.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <trace/events/napi.h>
#include <linux/pci.h>

#include "net-sysfs.h"

/* Instead of increasing this, you should create a hash table. */
#define MAX_GRO_SKBS 8

/* This should be increased if a protocol with a bigger head is added. */
#define GRO_MAX_HEAD (MAX_HEADER + 128)

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 *
 *	Why 16. Because with 16 the only overlap we get on a hash of the
 *	low nibble of the protocol value is RARP/SNAP/X.25.
 *
 *      NOTE:  That is no longer true with the addition of VLAN tags.  Not
 *             sure which should go first, but I bet it won't make much
 *             difference if we are running VLANs.  The good news is that
 *             this protocol won't be in the list unless compiled in, so
 *             the average user (w/out VLANs) will not be adversely affected.
 *             --BLG
 *
 *		0800	IP
 *		8100    802.1Q VLAN
 *		0001	802.3
 *		0002	AX.25
 *		0004	802.2
 *		8035	RARP
 *		0005	SNAP
 *		0805	X.25
 *		0806	ARP
 *		8137	IPX
 *		0009	Localtalk
 *		86DD	IPv6
 */

#define PTYPE_HASH_SIZE	(16)
#define PTYPE_HASH_MASK	(PTYPE_HASH_SIZE - 1)

static DEFINE_SPINLOCK(ptype_lock);

/*
搜一下内核源代码，二层协议还真是多。。。
drivers/net/wan/hdlc.c: dev_add_pack(&hdlc_packet_type);  //ETH_P_HDLC    hdlc_rcv
drivers/net/wan/lapbether.c:
            dev_add_pack(&lapbeth_packet_type);         //ETH_P_DEC       lapbeth_rcv
drivers/net/wan/syncppp.c:
            dev_add_pack(&sppp_packet_type);            //ETH_P_WAN_PPP   sppp_rcv
drivers/net/bonding/bond_alb.c:  dev_add_pack(pk_type); //ETH_P_ARP       rlb_arp_recv
drivers/net/bonding/bond_main.c:dev_add_pack(pk_type);  //PKT_TYPE_LACPDU bond_3ad_lacpdu_recv
drivers/net/bonding/bond_main.c:dev_add_pack(pt);       //ETH_P_ARP       bond_arp_rcv
drivers/net/pppoe.c: dev_add_pack(&pppoes_ptype);       //ETH_P_PPP_SES   pppoe_rcv
drivers/net/pppoe.c: dev_add_pack(&pppoed_ptype);       //ETH_P_PPP_DISC  pppoe_disc_rcv
drivers/net/hamradio/bpqether.c:
                    dev_add_pack(&bpq_packet_type);     //ETH_P_BPQ       bpq_rcv
net/ipv4/af_inet.c:  dev_add_pack(&ip_packet_type);     //ETH_P_IP       ip_rcv
net/ipv4/arp.c:    dev_add_pack(&arp_packet_type);      //ETH_P_ARP       arp_rcv
net/ipv4/ipconfig.c:  dev_add_pack(&rarp_packet_type);  //ETH_P_RARP      ic_rarp_recv
net/ipv4/ipconfig.c:  dev_add_pack(&bootp_packet_type); //ETH_P_IP        ic_bootp_recv
net/llc/llc_core.c: dev_add_pack(&llc_packet_type);     //ETH_P_802_2     llc_rcv
net/llc/llc_core.c: dev_add_pack(&llc_tr_packet_type);  //ETH_P_TR_802_2  llc_rcv
net/x25/af_x25.c:  dev_add_pack(&x25_packet_type);    //ETH_P_X25      x25_lapb_receive_frame
net/8021q/vlan.c:  dev_add_pack(&vlan_packet_type);     //ETH_P_8021Q     vlan_skb_recv

这些不同协议的packet_type，有些是linux系统启动时挂上去的
比如处理ip协议的pakcet_type，就是在 inet_init()时挂上去的
还有些驱动模块加载的时候才加上去的
*/
//网卡驱动最后调用 netif_receive_skb ，从而执行func函数 
//网络抓包tcpdump也在二层实现，参考http://blog.csdn.net/jw212/article/details/6738497

//赋值的地方在dev_add_pack
//这些处理函数用来处理接收到的不同协议族的报文
static struct list_head ptype_base[PTYPE_HASH_SIZE];  

/*
混杂模式（Promiscuous Mode）是指一台机器能够接收所有经过它的数据流，而不论其目的地址是否是他。是相对于通常模式（又称“非混杂模式”）而言的。
这被网络管理员使用来诊断网络问题，但是也被无认证的想偷听网络通信（其可能包括密码和其它敏感的信息）的人利用。一个非路由选择节点在混杂模式下
一般仅能够在相同的冲突域（对以太网和无线局域网）内监控通信到和来自其它节点或环（对令牌环或FDDI），其是为什么网络交换被用于对抗恶意的混杂模式。　　混杂模式就是接收所有经过网卡的数据包，包括不是发给本机的包。默认情况下网卡只把发给本机的包（包括广播包）传递给上层程序，其它的包一律丢弃。
简单的讲,混杂模式就是指网卡能接受所有通过它的数据流，不管是什么格式，什么地址的。事实上，计算机收到数据包后，由网络层进行判断，确定是递交上层（传输层），还是丢弃，还是递交下层（数据链路层、MAC子层）转发。　　通常在需要用到抓包工具，例如ethereal、sniffer时，需要把网卡置于混杂模式，需要用到软件Winpcap。winpcap是windows平台下一个免费，公共的网络访问系统。开发winpcap这个项目的目的在于为win32应用程序提供访问网络底层的能力。
po->prot_hook.func = packet_rcv;

if (sock->type == SOCK_PACKET)
	po->prot_hook.func = packet_rcv_spkt;

ETH_P_ALL的注册在packet_create中，该函数是通过应用层的函数nSock == socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))系统调用的。ptype_all 链，这些为注册到内核的一些 sniffer，将上传给这些sniffer，另一个就是遍历 ptype_base，这个就是具体的协议类型
*/ //接收见__netif_receive_skb，发送见dev_queue_xmit_nit
static struct list_head ptype_all;// __read_mostly;	/* Taps */ //在net_dev_init中初始化

/*
 * The @dev_base_head list is protected by @dev_base_lock and the rtnl
 * semaphore.
 *
 * Pure readers hold dev_base_lock for reading, or rcu_read_lock()
 *
 * Writers must hold the rtnl semaphore while they loop through the
 * dev_base_head list, and hold dev_base_lock for writing when they do the
 * actual updates.  This allows pure readers to access the list even
 * while a writer is preparing to update it.
 *
 * To put it another way, dev_base_lock is held for writing only to
 * protect against pure readers; the rtnl semaphore provides the
 * protection against other writers.
 *
 * See, for example usages, register_netdevice() and
 * unregister_netdevice(), which must be called with the rtnl
 * semaphore held.
 */
DEFINE_RWLOCK(dev_base_lock);
EXPORT_SYMBOL(dev_base_lock);

static inline struct hlist_head *dev_name_hash(struct net *net, const char *name)
{
	unsigned hash = full_name_hash(name, strnlen(name, IFNAMSIZ));
	return &net->dev_name_head[hash_32(hash, NETDEV_HASHBITS)];
}

static inline struct hlist_head *dev_index_hash(struct net *net, int ifindex)
{
	return &net->dev_index_head[ifindex & (NETDEV_HASHENTRIES - 1)];
}

static inline void rps_lock(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	spin_lock(&sd->input_pkt_queue.lock);
#endif
}

static inline void rps_unlock(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	spin_unlock(&sd->input_pkt_queue.lock);
#endif
}

/* Device list insertion */
static int list_netdevice(struct net_device *dev)
{
	struct net *net = dev_net(dev);

	ASSERT_RTNL();

	write_lock_bh(&dev_base_lock);
	list_add_tail_rcu(&dev->dev_list, &net->dev_base_head);
	hlist_add_head_rcu(&dev->name_hlist, dev_name_hash(net, dev->name));
	hlist_add_head_rcu(&dev->index_hlist,
			   dev_index_hash(net, dev->ifindex));
	write_unlock_bh(&dev_base_lock);
	return 0;
}

/* Device list removal
 * caller must respect a RCU grace period before freeing/reusing dev
 */

/*
  * 将待注销的网络设备实例从全局struct net的
  * 链表dev_base_head及dev_name_head、dev_index_head
  * 散列表中移除。移除后不能阻止
  * 内核子系统使用该设备，他们仍然
  * 拥有指向该net_device结构实例的指针，
  * 只有当引用计数为0时才会真正释放
  * 实例。
  */
static void unlist_netdevice(struct net_device *dev)
{
	ASSERT_RTNL();

	/* Unlink dev from the device chain */
	write_lock_bh(&dev_base_lock);
	list_del_rcu(&dev->dev_list);
	hlist_del_rcu(&dev->name_hlist);
	hlist_del_rcu(&dev->index_hlist);
	write_unlock_bh(&dev_base_lock);
}

/*
 *	Our notifier list
 */
 //如图 1中所示，Linux的网络子系统一共有3个通知链：表示ipv4地址发生变化时的inetaddr_chain；表示ipv6地址发生变化的inet6addr_chain；还有表示设备注册、状态变化的netdev_chain。
//RAW_NOTIFIER_HEAD(netdev_chain);原始通知链（ Raw notifierchains ）：对通知链元素的回调函数没有任何限制，所有锁和保护机制都由调用者维护。对应的链表头：
//网络子系统就是该类型，通过以下宏实现head的初始化
//netdev_chain为原始raw通知连，通知事件函数为__raw_notifier_call_chain
//static RAW_NOTIFIER_HEAD(netdev_chain);
/*
Linux内核中各个子系统相互依赖，当其中某个子系统状态发生改变时，就必须使用一定的机制告知使用其服务的其他子系统，以便其他子系统采取相应的措施。
为满足这样的需求，内核实现了事件通知链机制（notificationchain）。
*/
/*
原子通知链（ Atomic notifier chains ）：通知链元素的回调函数（当事件发生时要执行的函数）在中断或原子操作上下文中运行，不允许阻塞。对应的链表头结构：
可阻塞通知链（ Blocking notifier chains ）：通知链元素的回调函数在进程上下文中运行，允许阻塞。对应的链表头：
原始通知链（ Raw notifierchains ）：对通知链元素的回调函数没有任何限制，所有锁和保护机制都由调用者维护。对应的链表头：
SRCU 通知链（ SRCU notifier chains ）：可阻塞通知链的一种变体。对应的链表头：

Linux的网络子系统一共有3个通知链：表示ipv4地址发生变化时的inetaddr_chain；表示ipv6地址发生变化的inet6addr_chain；还有表示设备注册、
状态变化的netdev_chain。
*/

struct raw_notifier_head netdev_chain =	RAW_NOTIFIER_INIT(netdev_chain) //在register_netdevice_notifier中

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the local softnet handler.
 */

DEFINE_PER_CPU_ALIGNED(struct softnet_data, softnet_data);
EXPORT_PER_CPU_SYMBOL(softnet_data);

#ifdef CONFIG_LOCKDEP
/*
 * register_netdevice() inits txq->_xmit_lock and sets lockdep class
 * according to dev->type
 */
static const unsigned short netdev_lock_type[] =
	{ARPHRD_NETROM, ARPHRD_ETHER, ARPHRD_EETHER, ARPHRD_AX25,
	 ARPHRD_PRONET, ARPHRD_CHAOS, ARPHRD_IEEE802, ARPHRD_ARCNET,
	 ARPHRD_APPLETLK, ARPHRD_DLCI, ARPHRD_ATM, ARPHRD_METRICOM,
	 ARPHRD_IEEE1394, ARPHRD_EUI64, ARPHRD_INFINIBAND, ARPHRD_SLIP,
	 ARPHRD_CSLIP, ARPHRD_SLIP6, ARPHRD_CSLIP6, ARPHRD_RSRVD,
	 ARPHRD_ADAPT, ARPHRD_ROSE, ARPHRD_X25, ARPHRD_HWX25,
	 ARPHRD_PPP, ARPHRD_CISCO, ARPHRD_LAPB, ARPHRD_DDCMP,
	 ARPHRD_RAWHDLC, ARPHRD_TUNNEL, ARPHRD_TUNNEL6, ARPHRD_FRAD,
	 ARPHRD_SKIP, ARPHRD_LOOPBACK, ARPHRD_LOCALTLK, ARPHRD_FDDI,
	 ARPHRD_BIF, ARPHRD_SIT, ARPHRD_IPDDP, ARPHRD_IPGRE,
	 ARPHRD_PIMREG, ARPHRD_HIPPI, ARPHRD_ASH, ARPHRD_ECONET,
	 ARPHRD_IRDA, ARPHRD_FCPP, ARPHRD_FCAL, ARPHRD_FCPL,
	 ARPHRD_FCFABRIC, ARPHRD_IEEE802_TR, ARPHRD_IEEE80211,
	 ARPHRD_IEEE80211_PRISM, ARPHRD_IEEE80211_RADIOTAP, ARPHRD_PHONET,
	 ARPHRD_PHONET_PIPE, ARPHRD_IEEE802154,
	 ARPHRD_VOID, ARPHRD_NONE};

static const char *const netdev_lock_name[] =
	{"_xmit_NETROM", "_xmit_ETHER", "_xmit_EETHER", "_xmit_AX25",
	 "_xmit_PRONET", "_xmit_CHAOS", "_xmit_IEEE802", "_xmit_ARCNET",
	 "_xmit_APPLETLK", "_xmit_DLCI", "_xmit_ATM", "_xmit_METRICOM",
	 "_xmit_IEEE1394", "_xmit_EUI64", "_xmit_INFINIBAND", "_xmit_SLIP",
	 "_xmit_CSLIP", "_xmit_SLIP6", "_xmit_CSLIP6", "_xmit_RSRVD",
	 "_xmit_ADAPT", "_xmit_ROSE", "_xmit_X25", "_xmit_HWX25",
	 "_xmit_PPP", "_xmit_CISCO", "_xmit_LAPB", "_xmit_DDCMP",
	 "_xmit_RAWHDLC", "_xmit_TUNNEL", "_xmit_TUNNEL6", "_xmit_FRAD",
	 "_xmit_SKIP", "_xmit_LOOPBACK", "_xmit_LOCALTLK", "_xmit_FDDI",
	 "_xmit_BIF", "_xmit_SIT", "_xmit_IPDDP", "_xmit_IPGRE",
	 "_xmit_PIMREG", "_xmit_HIPPI", "_xmit_ASH", "_xmit_ECONET",
	 "_xmit_IRDA", "_xmit_FCPP", "_xmit_FCAL", "_xmit_FCPL",
	 "_xmit_FCFABRIC", "_xmit_IEEE802_TR", "_xmit_IEEE80211",
	 "_xmit_IEEE80211_PRISM", "_xmit_IEEE80211_RADIOTAP", "_xmit_PHONET",
	 "_xmit_PHONET_PIPE", "_xmit_IEEE802154",
	 "_xmit_VOID", "_xmit_NONE"};

static struct lock_class_key netdev_xmit_lock_key[ARRAY_SIZE(netdev_lock_type)];
static struct lock_class_key netdev_addr_lock_key[ARRAY_SIZE(netdev_lock_type)];

static inline unsigned short netdev_lock_pos(unsigned short dev_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(netdev_lock_type); i++)
		if (netdev_lock_type[i] == dev_type)
			return i;
	/* the last key is used by default */
	return ARRAY_SIZE(netdev_lock_type) - 1;
}

static inline void netdev_set_xmit_lockdep_class(spinlock_t *lock,
						 unsigned short dev_type)
{
	int i;

	i = netdev_lock_pos(dev_type);
	lockdep_set_class_and_name(lock, &netdev_xmit_lock_key[i],
				   netdev_lock_name[i]);
}

static inline void netdev_set_addr_lockdep_class(struct net_device *dev)
{
	int i;

	i = netdev_lock_pos(dev->type);
	lockdep_set_class_and_name(&dev->addr_list_lock,
				   &netdev_addr_lock_key[i],
				   netdev_lock_name[i]);
}
#else
static inline void netdev_set_xmit_lockdep_class(spinlock_t *lock,
						 unsigned short dev_type)
{
}
static inline void netdev_set_addr_lockdep_class(struct net_device *dev)
{
}
#endif

/*******************************************************************************

		Protocol management and registration routines

*******************************************************************************/

/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 *
 *	BEWARE!!! Protocol handlers, mangling input packets,
 *	MUST BE last in hash buckets and checking protocol handlers
 *	MUST start from promiscuous ptype_all chain in net_bh.
 *	It is true now, do not change it.
 *	Explanation follows: if protocol handler, mangling packet, will
 *	be the first on list, it is not able to sense, that packet
 *	is cloned and should be copied-on-write, so that it will
 *	change it and subsequent readers will get broken packet.
 *							--ANK (980803)
 */

/**
 *	dev_add_pack - add packet handler
 *	@pt: packet type declaration
 *
 *	Add a protocol handler to the networking stack. The passed &packet_type
 *	is linked into kernel lists and may not be freed until it has been
 *	removed from the kernel lists.
 *
 *	This call does not sleep therefore it can not
 *	guarantee all CPU's that are in middle of receiving packets
 *	will see the new packet type (until the next received packet).
 */
/*
搜一下内核源代码，二层协议还真是多。。。
drivers/net/wan/hdlc.c: dev_add_pack(&hdlc_packet_type);  //ETH_P_HDLC    hdlc_rcv
drivers/net/wan/lapbether.c:
            dev_add_pack(&lapbeth_packet_type);         //ETH_P_DEC       lapbeth_rcv
drivers/net/wan/syncppp.c:
            dev_add_pack(&sppp_packet_type);            //ETH_P_WAN_PPP   sppp_rcv
drivers/net/bonding/bond_alb.c:  dev_add_pack(pk_type); //ETH_P_ARP       rlb_arp_recv
drivers/net/bonding/bond_main.c:dev_add_pack(pk_type);  //PKT_TYPE_LACPDU bond_3ad_lacpdu_recv
drivers/net/bonding/bond_main.c:dev_add_pack(pt);       //ETH_P_ARP       bond_arp_rcv
drivers/net/pppoe.c: dev_add_pack(&pppoes_ptype);       //ETH_P_PPP_SES   pppoe_rcv
drivers/net/pppoe.c: dev_add_pack(&pppoed_ptype);       //ETH_P_PPP_DISC  pppoe_disc_rcv
drivers/net/hamradio/bpqether.c:
                    dev_add_pack(&bpq_packet_type);     //ETH_P_BPQ       bpq_rcv
net/ipv4/af_inet.c:  dev_add_pack(&ip_packet_type);     //ETH_P_IP       ip_rcv
net/ipv4/arp.c:    dev_add_pack(&arp_packet_type);      //ETH_P_ARP       arp_rcv
net/ipv4/ipconfig.c:  dev_add_pack(&rarp_packet_type);  //ETH_P_RARP      ic_rarp_recv
net/ipv4/ipconfig.c:  dev_add_pack(&bootp_packet_type); //ETH_P_IP        ic_bootp_recv
net/llc/llc_core.c: dev_add_pack(&llc_packet_type);     //ETH_P_802_2     llc_rcv
net/llc/llc_core.c: dev_add_pack(&llc_tr_packet_type);  //ETH_P_TR_802_2  llc_rcv
net/x25/af_x25.c:  dev_add_pack(&x25_packet_type);    //ETH_P_X25      x25_lapb_receive_frame
net/8021q/vlan.c:  dev_add_pack(&vlan_packet_type);     //ETH_P_8021Q     vlan_skb_recv

这些不同协议的packet_type，有些是linux系统启动时挂上去的
比如处理ip协议的pakcet_type，就是在 inet_init()时挂上去的
还有些驱动模块加载的时候才加上去的
*///网卡驱动最后调用netif_receive_skb，从而执行func函数 
//网络抓包tcpdump也在二层实现，参考http://blog.csdn.net/jw212/article/details/6738497
/*
混杂模式（Promiscuous Mode）是指一台机器能够接收所有经过它的数据流，而不论其目的地址是否是他。是相对于通常模式（又称“非混杂模式”）而言的。
这被网络管理员使用来诊断网络问题，但是也被无认证的想偷听网络通信（其可能包括密码和其它敏感的信息）的人利用。一个非路由选择节点在混杂模式下
一般仅能够在相同的冲突域（对以太网和无线局域网）内监控通信到和来自其它节点或环（对令牌环或FDDI），其是为什么网络交换被用于对抗恶意的混杂模式。　　混杂模式就是接收所有经过网卡的数据包，包括不是发给本机的包。默认情况下网卡只把发给本机的包（包括广播包）传递给上层程序，其它的包一律丢弃。简单的讲,混杂模式就是指网卡能接受所有通过它的数据流，不管是什么格式，什么地址的。事实上，计算机收到数据包后，由网络层进行判断，确定是递交上层（传输层），还是丢弃，还是递交下层（数据链路层、MAC子层）转发。　　通常在需要用到抓包工具，例如ethereal、sniffer时，需要把网卡置于混杂模式，需要用到软件Winpcap。winpcap是windows平台下一个免费，公共的网络访问系统。开发winpcap这个项目的目的在于为win32应用程序提供访问网络底层的能力。

ETH_P_ALL的注册在packet_create中，该函数是通过应用层的函数nSock == socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))系统调用的。ptype_all 链，这些为注册到内核的一些 sniffer，将上传给这些sniffer，另一个就是遍历 ptype_base，这个就是具体的协议类型
*/
void dev_add_pack(struct packet_type *pt)
{
	int hash;

	spin_lock_bh(&ptype_lock);
	if (pt->type == htons(ETH_P_ALL))
		list_add_rcu(&pt->list, &ptype_all);
	else {
		hash = ntohs(pt->type) & PTYPE_HASH_MASK;
		list_add_rcu(&pt->list, &ptype_base[hash]);
	}
	spin_unlock_bh(&ptype_lock);
}
EXPORT_SYMBOL(dev_add_pack);

/**
 *	__dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *      The packet type might still be in use by receivers
 *	and must not be freed until after all the CPU's have gone
 *	through a quiescent state.
 */
void __dev_remove_pack(struct packet_type *pt)
{
	struct list_head *head;
	struct packet_type *pt1;

	spin_lock_bh(&ptype_lock);

	if (pt->type == htons(ETH_P_ALL))
		head = &ptype_all;
	else
		head = &ptype_base[ntohs(pt->type) & PTYPE_HASH_MASK];

	list_for_each_entry(pt1, head, list) {
		if (pt == pt1) {
			list_del_rcu(&pt->list);
			goto out;
		}
	}

	printk(KERN_WARNING "dev_remove_pack: %p not found.\n", pt);
out:
	spin_unlock_bh(&ptype_lock);
}
EXPORT_SYMBOL(__dev_remove_pack);

/**
 *	dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *	This call sleeps to guarantee that no CPU is looking at the packet
 *	type after return.
 */
void dev_remove_pack(struct packet_type *pt)
{
	__dev_remove_pack(pt);

	synchronize_net();
}
EXPORT_SYMBOL(dev_remove_pack);

/******************************************************************************

		      Device Boot-time Settings Routines

*******************************************************************************/

/* Boot time configuration table */
static struct netdev_boot_setup dev_boot_setup[NETDEV_BOOT_SETUP_MAX];

/**
 *	netdev_boot_setup_add	- add new setup entry
 *	@name: name of the device
 *	@map: configured settings for the device
 *
 *	Adds new setup entry to the dev_boot_setup list.  The function
 *	returns 0 on error and 1 on success.  This is a generic routine to
 *	all netdevices.
 */
static int netdev_boot_setup_add(char *name, struct ifmap *map)
{
	struct netdev_boot_setup *s;
	int i;

	s = dev_boot_setup;
	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++) {
		if (s[i].name[0] == '\0' || s[i].name[0] == ' ') {
			memset(s[i].name, 0, sizeof(s[i].name));
			strlcpy(s[i].name, name, IFNAMSIZ);
			memcpy(&s[i].map, map, sizeof(s[i].map));
			break;
		}
	}

	return i >= NETDEV_BOOT_SETUP_MAX ? 0 : 1;
}

/**
 *	netdev_boot_setup_check	- check boot time settings
 *	@dev: the netdevice
 *
 * 	Check boot time settings for the device.
 *	The found settings are set for the device to be used
 *	later in the device probing.
 *	Returns 0 if no settings found, 1 if they are.
 */
int netdev_boot_setup_check(struct net_device *dev)
{
	struct netdev_boot_setup *s = dev_boot_setup;
	int i;

	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++) {
		if (s[i].name[0] != '\0' && s[i].name[0] != ' ' &&
		    !strcmp(dev->name, s[i].name)) {
			dev->irq 	= s[i].map.irq;
			dev->base_addr 	= s[i].map.base_addr;
			dev->mem_start 	= s[i].map.mem_start;
			dev->mem_end 	= s[i].map.mem_end;
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(netdev_boot_setup_check);


/**
 *	netdev_boot_base	- get address from boot time settings
 *	@prefix: prefix for network device
 *	@unit: id for network device
 *
 * 	Check boot time settings for the base address of device.
 *	The found settings are set for the device to be used
 *	later in the device probing.
 *	Returns 0 if no settings found.
 */
unsigned long netdev_boot_base(const char *prefix, int unit)
{
	const struct netdev_boot_setup *s = dev_boot_setup;
	char name[IFNAMSIZ];
	int i;

	sprintf(name, "%s%d", prefix, unit);

	/*
	 * If device already registered then return base of 1
	 * to indicate not to probe for this interface
	 */
	if (__dev_get_by_name(&init_net, name))
		return 1;

	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++)
		if (!strcmp(name, s[i].name))
			return s[i].map.base_addr;
	return 0;
}

/*
 * Saves at boot time configured settings for any netdevice.
 */
int __init netdev_boot_setup(char *str)
{
	int ints[5];
	struct ifmap map;

	str = get_options(str, ARRAY_SIZE(ints), ints);
	if (!str || !*str)
		return 0;

	/* Save settings */
	memset(&map, 0, sizeof(map));
	if (ints[0] > 0)
		map.irq = ints[1];
	if (ints[0] > 1)
		map.base_addr = ints[2];
	if (ints[0] > 2)
		map.mem_start = ints[3];
	if (ints[0] > 3)
		map.mem_end = ints[4];

	/* Add new entry to the list */
	return netdev_boot_setup_add(str, &map);
}

__setup("netdev=", netdev_boot_setup);

/*******************************************************************************

			    Device Interface Subroutines

*******************************************************************************/

/**
 *	__dev_get_by_name	- find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *
 *	Find an interface by name. Must be called under RTNL semaphore
 *	or @dev_base_lock. If the name is found a pointer to the device
 *	is returned. If the name is not found then %NULL is returned. The
 *	reference counters are not incremented so the caller must be
 *	careful with locks.
 */

struct net_device *__dev_get_by_name(struct net *net, const char *name)
{
	struct hlist_node *p;
	struct net_device *dev;
	struct hlist_head *head = dev_name_hash(net, name);

	hlist_for_each_entry(dev, p, head, name_hlist)
		if (!strncmp(dev->name, name, IFNAMSIZ))
			return dev;

	return NULL;
}
EXPORT_SYMBOL(__dev_get_by_name);

/**
 *	dev_get_by_name_rcu	- find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *
 *	Find an interface by name.
 *	If the name is found a pointer to the device is returned.
 * 	If the name is not found then %NULL is returned.
 *	The reference counters are not incremented so the caller must be
 *	careful with locks. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_name_rcu(struct net *net, const char *name)
{
	struct hlist_node *p;
	struct net_device *dev;
	struct hlist_head *head = dev_name_hash(net, name);

	hlist_for_each_entry_rcu(dev, p, head, name_hlist)
		if (!strncmp(dev->name, name, IFNAMSIZ))
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_get_by_name_rcu);

/**
 *	dev_get_by_name		- find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *
 *	Find an interface by name. This can be called from any
 *	context and does its own locking. The returned handle has
 *	the usage count incremented and the caller must use dev_put() to
 *	release it when it is no longer needed. %NULL is returned if no
 *	matching device is found.
 */

struct net_device *dev_get_by_name(struct net *net, const char *name)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(net, name);
	if (dev)
		dev_hold(dev);
	rcu_read_unlock();
	return dev;
}
EXPORT_SYMBOL(dev_get_by_name);

/**
 *	__dev_get_by_index - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold either the RTNL semaphore
 *	or @dev_base_lock.
 */

struct net_device *__dev_get_by_index(struct net *net, int ifindex)
{
	struct hlist_node *p;
	struct net_device *dev;
	struct hlist_head *head = dev_index_hash(net, ifindex);

	hlist_for_each_entry(dev, p, head, index_hlist)
		if (dev->ifindex == ifindex)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(__dev_get_by_index);

/**
 *	dev_get_by_index_rcu - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_index_rcu(struct net *net, int ifindex)
{
	struct hlist_node *p;
	struct net_device *dev;
	struct hlist_head *head = dev_index_hash(net, ifindex);

	hlist_for_each_entry_rcu(dev, p, head, index_hlist)
		if (dev->ifindex == ifindex)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_get_by_index_rcu);


/**
 *	dev_get_by_index - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns NULL if the device
 *	is not found or a pointer to the device. The device returned has
 *	had a reference added and the pointer is safe until the user calls
 *	dev_put to indicate they have finished with it.
 */

struct net_device *dev_get_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, ifindex);
	if (dev)
		dev_hold(dev);
	rcu_read_unlock();
	return dev;
}
EXPORT_SYMBOL(dev_get_by_index);

/**
 *	dev_getbyhwaddr - find a device by its hardware address
 *	@net: the applicable net namespace
 *	@type: media type of device
 *	@ha: hardware address
 *
 *	Search for an interface by MAC address. Returns NULL if the device
 *	is not found or a pointer to the device. The caller must hold the
 *	rtnl semaphore. The returned device has not had its ref count increased
 *	and the caller must therefore be careful about locking
 *
 *	BUGS:
 *	If the API was consistent this would be __dev_get_by_hwaddr
 */

struct net_device *dev_getbyhwaddr(struct net *net, unsigned short type, char *ha)
{
	struct net_device *dev;

	ASSERT_RTNL();

	for_each_netdev(net, dev)
		if (dev->type == type &&
		    !memcmp(dev->dev_addr, ha, dev->addr_len))
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_getbyhwaddr);
/*
  * 获取网络设备
  */
struct net_device *__dev_getfirstbyhwtype(struct net *net, unsigned short type)
{
	struct net_device *dev;

	ASSERT_RTNL();
	for_each_netdev(net, dev)
		if (dev->type == type)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(__dev_getfirstbyhwtype);

struct net_device *dev_getfirstbyhwtype(struct net *net, unsigned short type)
{
	struct net_device *dev, *ret = NULL;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev)
		if (dev->type == type) {
			dev_hold(dev);
			ret = dev;
			break;
		}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(dev_getfirstbyhwtype);

/**
 *	dev_get_by_flags - find any device with given flags
 *	@net: the applicable net namespace
 *	@if_flags: IFF_* values
 *	@mask: bitmask of bits in if_flags to check
 *
 *	Search for any interface with the given flags. Returns NULL if a device
 *	is not found or a pointer to the device. The device returned has
 *	had a reference added and the pointer is safe until the user calls
 *	dev_put to indicate they have finished with it.
 */
    /*
      * 根据标志获取网络设备
      */

struct net_device *dev_get_by_flags(struct net *net, unsigned short if_flags,
				    unsigned short mask)
{
	struct net_device *dev, *ret;

	ret = NULL;
	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		if (((dev->flags ^ if_flags) & mask) == 0) {
			dev_hold(dev);
			ret = dev;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(dev_get_by_flags);

/**
 *	dev_valid_name - check if name is okay for network device
 *	@name: name string
 *
 *	Network device names need to be valid file names to
 *	to allow sysfs to work.  We also disallow any kind of
 *	whitespace.
 *///检查网络设备名是否有效
int dev_valid_name(const char *name)
{
	if (*name == '\0')
		return 0;
	if (strlen(name) >= IFNAMSIZ)
		return 0;
	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return 0;

	while (*name) {
		if (*name == '/' || isspace(*name))
			return 0;
		name++;
	}
	return 1;
}
EXPORT_SYMBOL(dev_valid_name);

/**
 *	__dev_alloc_name - allocate a name for a device
 *	@net: network namespace to allocate the device name in
 *	@name: name format string
 *	@buf:  scratch buffer and result name string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. It scans list of devices to build up a free map, then chooses
 *	the first empty slot. The caller must hold the dev_base or rtnl lock
 *	while allocating the name and adding the device in order to avoid
 *	duplicates.
 *	Limited to bits_per_byte * page size devices (ie 32K on most platforms).
 *	Returns the number of the unit assigned or a negative errno code.
 */

static int __dev_alloc_name(struct net *net, const char *name, char *buf)
{
	int i = 0;
	const char *p;
	const int max_netdevices = 8*PAGE_SIZE;
	unsigned long *inuse;
	struct net_device *d;

         /* 
        * 在注册网络设备时，这个地方的调用有些重复。
        * register_netdev中已计算过一次 '%'的位置。
        *
        */
	p = strnchr(name, IFNAMSIZ-1, '%');
	if (p) {
		/*
		 * Verify the string as this thing may have come from
		 * the user.  There must be either one "%d" and no other "%"
		 * characters.
		 */
		 /*
		 * 格式字符串只支持"name%d"的格式，所以
		 * 如果下个字符不是'd'或者，在之后还有
		 * '%'格式串，则说明name字符串不合法
		 *
		 */
		if (p[1] != 'd' || strchr(p + 2, '%'))
			return -EINVAL;

		/* Use one page as a bit array of possible slots */
		inuse = (unsigned long *) get_zeroed_page(GFP_ATOMIC);
		if (!inuse)
			return -ENOMEM;

		for_each_netdev(net, d) {
                     /* 
                      * 如果d->name中的字符串前缀(不包括后面的数字)
                      * 和name中的前缀(不包括"%d")不相同，或者d->name中
                      * 前缀之后没有数字，则返回值为0，否则返回的
                      * 是读到的数字的个数，这里应该是1。如果前缀
                      * 不同或者没有数字，新注册的设备肯定不会和d
                      * 名称冲突，也就不需要做后面的解决冲突的操作了
                      */
			if (!sscanf(d->name, name, &i))
				continue;
                     /* 这里i存储是d的设备id */
			if (i < 0 || i >= max_netdevices)
				continue;

			/*  avoid cases where sscanf is not exact inverse of printf */
			snprintf(buf, IFNAMSIZ, name, i);
                    /* 
                     * 通过前面的sscanf调用知道，d->name和name的前缀相同,
                     * 并且d->name后面是数字，但是在d->name中的字符串有
                     * 可能在数字后面还有英文字符，例如"eth1n",所以这里
                     * 要再进行一次比对，只有在d->name的字符串是以数字结尾
                     * 时才需要继续进行冲突处理
                     */
			if (!strncmp(buf, d->name, IFNAMSIZ))
				set_bit(i, inuse);
		}

		i = find_first_zero_bit(inuse, max_netdevices);
		free_page((unsigned long) inuse);
	}

	/*
	  * 将找到的可用设备ID和名称前缀输出
	  * 到buf中，也就是上层传递过来的用来
	  * 存储设备名称的内存。
	  */
	snprintf(buf, IFNAMSIZ, name, i);
       /* 
        * 通过名称查找是否已存在名称相同的设备，
        * 如果没有找到，则说明找到的设备id是唯一的
        */
	if (!__dev_get_by_name(net, buf))
		return i;

	/* It is possible to run out of possible slots
	 * when the name is long and there isn't enough space left
	 * for the digits, or if all bits are used.
	 */
	return -ENFILE;
}

/**
 *	dev_alloc_name - allocate a name for a device
 *	@dev: device
 *	@name: name format string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. It scans list of devices to build up a free map, then chooses
 *	the first empty slot. The caller must hold the dev_base or rtnl lock
 *	while allocating the name and adding the device in order to avoid
 *	duplicates.
 *	Limited to bits_per_byte * page size devices (ie 32K on most platforms).
 *	Returns the number of the unit assigned or a negative errno code.
 */

int dev_alloc_name(struct net_device *dev, const char *name)
{
	char buf[IFNAMSIZ];
	struct net *net;
	int ret;

	BUG_ON(!dev_net(dev));
	net = dev_net(dev);
	ret = __dev_alloc_name(net, name, buf);
	if (ret >= 0)
		strlcpy(dev->name, buf, IFNAMSIZ);
	return ret;
}
EXPORT_SYMBOL(dev_alloc_name);

static int dev_get_valid_name(struct net_device *dev, const char *name, bool fmt)
{
	struct net *net;

	BUG_ON(!dev_net(dev));
	net = dev_net(dev);

	if (!dev_valid_name(name))
		return -EINVAL;

	if (fmt && strchr(name, '%'))
		return dev_alloc_name(dev, name);
	else if (__dev_get_by_name(net, name))
		return -EEXIST;
	else if (dev->name != name)
		strlcpy(dev->name, name, IFNAMSIZ);

	return 0;
}

/**
 *	dev_change_name - change name of a device
 *	@dev: device
 *	@newname: name (or format string) must be at least IFNAMSIZ
 *
 *	Change name of a device, can pass format strings "eth%d".
 *	for wildcarding.
 */
int dev_change_name(struct net_device *dev, const char *newname)
{
	char oldname[IFNAMSIZ];
	int err = 0;
	int ret;
	struct net *net;

	ASSERT_RTNL();
	BUG_ON(!dev_net(dev));

	net = dev_net(dev);
	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (strncmp(newname, dev->name, IFNAMSIZ) == 0)
		return 0;

	memcpy(oldname, dev->name, IFNAMSIZ);

	err = dev_get_valid_name(dev, newname, 1);
	if (err < 0)
		return err;

rollback:
	ret = device_rename(&dev->dev, dev->name);
	if (ret) {
		memcpy(dev->name, oldname, IFNAMSIZ);
		return ret;
	}

	write_lock_bh(&dev_base_lock);
	hlist_del(&dev->name_hlist);
	write_unlock_bh(&dev_base_lock);

	synchronize_rcu();

	write_lock_bh(&dev_base_lock);
	hlist_add_head_rcu(&dev->name_hlist, dev_name_hash(net, dev->name));
	write_unlock_bh(&dev_base_lock);

	ret = call_netdevice_notifiers(NETDEV_CHANGENAME, dev);
	ret = notifier_to_errno(ret);

	if (ret) {
		/* err >= 0 after dev_alloc_name() or stores the first errno */
		if (err >= 0) {
			err = ret;
			memcpy(dev->name, oldname, IFNAMSIZ);
			goto rollback;
		} else {
			printk(KERN_ERR
			       "%s: name change rollback failed: %d.\n",
			       dev->name, ret);
		}
	}

	return err;
}

/**
 *	dev_set_alias - change ifalias of a device
 *	@dev: device
 *	@alias: name up to IFALIASZ
 *	@len: limit of bytes to copy from info
 *
 *	Set ifalias for a device,
 */
int dev_set_alias(struct net_device *dev, const char *alias, size_t len)
{
	ASSERT_RTNL();

	if (len >= IFALIASZ)
		return -EINVAL;

	if (!len) {
		if (dev->ifalias) {
			kfree(dev->ifalias);
			dev->ifalias = NULL;
		}
		return 0;
	}

	dev->ifalias = krealloc(dev->ifalias, len + 1, GFP_KERNEL);
	if (!dev->ifalias)
		return -ENOMEM;

	strlcpy(dev->ifalias, alias, len+1);
	return len;
}


/**
 *	netdev_features_change - device changes features
 *	@dev: device to cause notification
 *
 *	Called to indicate a device has changed features.
 */
void netdev_features_change(struct net_device *dev)
{
	call_netdevice_notifiers(NETDEV_FEAT_CHANGE, dev);
}
EXPORT_SYMBOL(netdev_features_change);

/**
 *	netdev_state_change - device changes state
 *	@dev: device to cause notification
 *
 *	Called to indicate a device has changed state. This function calls
 *	the notifier chains for netdev_chain and sends a NEWLINK message
 *	to the routing socket.
 */
void netdev_state_change(struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		call_netdevice_notifiers(NETDEV_CHANGE, dev);
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0);
	}
}
EXPORT_SYMBOL(netdev_state_change);

int netdev_bonding_change(struct net_device *dev, unsigned long event)
{
	return call_netdevice_notifiers(event, dev);
}
EXPORT_SYMBOL(netdev_bonding_change);

/**
 *	dev_load 	- load a network module
 *	@net: the applicable net namespace
 *	@name: name of interface
 *
 *	If a network interface is not present and the process has suitable
 *	privileges this function loads the module. If module loading is not
 *	available in this kernel then it becomes a nop.
 */

void dev_load(struct net *net, const char *name)
{
	struct net_device *dev;
	int no_module;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(net, name);
	rcu_read_unlock();
	no_module = !dev;
	if (no_module && capable(CAP_NET_ADMIN))
		no_module = request_module("netdev-%s", name);
	if (no_module && capable(CAP_SYS_MODULE)) {
		if (!request_module("%s", name))
			pr_err("Loading kernel module for a network device "
"with CAP_SYS_MODULE (deprecated).  Use CAP_NET_ADMIN and alias netdev-%s "
"instead\n", name);
	}
}
EXPORT_SYMBOL(dev_load);

/**
 *	dev_open	- prepare an interface for use.
 *	@dev:	device to open
 *
 *	Takes a device from down to up state. The device's private open
 *	function is invoked and then the multicast lists are loaded. Finally
 *	the device is moved into the up state and a %NETDEV_UP message is
 *	sent to the netdev notifier chain.
 *
 *	Calling this function on an active interface is a nop. On a failure
 *	a negative errno code is returned.
 */
/*
  * 设备一旦注册后即可使用，但必须在用户
  * 或用户空间应用程序使能后才可以收发数据
  * 因为注册到系统中的网络设备，其初始
  * 状态是关闭的，此时是不能传输数据的，必须
  * 激活后，网络设备才能进行数据的传输。在
  * 应用层，可以通过ifconfig up命令(最终是通过ioctl
  * 的SIOCSIFFLAGS)来激活网络设备。而SIOCIFFLAGS命令
  * 是通过dev_change_flags()调用dev_open()来激活网络设备。
  * dev_open()将网络设备从关闭状态转到激活状态，
  * 并发送一个NETDEV_UP消息到网络设备状态改变
  * 通知链上。
  *////ic_dev_ioctl->dev_ioctl->dev_ifsioc->dev_change_flags
int __dev_open(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int ret;

	ASSERT_RTNL();

	/*
	 *	Is it already up?
	 */
	/*
	  * 如果网络设备已经启用，则无需再记性操作。
	  */
	if (dev->flags & IFF_UP)
		return 0;

	/*
	 *	Is it even present?
	 */
	/*
	  * 如果网络设备已经挂起，则不能被激活。
	  */
	if (!netif_device_present(dev))
		return -ENODEV;

	/*
	  * 发送NETDEV_PRE_UP事件通知
	  */
	ret = call_netdevice_notifiers(NETDEV_PRE_UP, dev);
	ret = notifier_to_errno(ret);
	if (ret)
		return ret;

	/*
	 *	Call device private open method
	 */
	/*
	  * 设备网络设备的启用状态标志。如果
	  * 实现open函数，则根据具体硬件注册
	  * 系统资源，使能硬件，并对设备作
	  * 其他的一些设置。
	  */
	set_bit(__LINK_STATE_START, &dev->state);

	if (ops->ndo_validate_addr)
		ret = ops->ndo_validate_addr(dev);

	if (!ret && ops->ndo_open)
		ret = ops->ndo_open(dev);

	/*
	 *	If it went open OK then:
	 */

	/*
	  * 如果启用网络设备成功，则设置网络
	  * 设备的已启用标志，并更新组播地址列表
	  * 到网络设备中，网络设备设置为传递状态。
	  * 调用dev_activate()初始化用于流量控制的排队
	  * 规则，并启动定时器。如果用户没有
	  * 配置流量可能根治，则指定为默认的
	  * 先进先出(FIFO)队列。最后，发送NETDEV_UP
	  * 消息到网络设备状态改变通知链上，以
	  * 通知对网络设备感兴趣的其他内核组件。
	  */
	if (ret)
		clear_bit(__LINK_STATE_START, &dev->state);
	else {
		/*
		 *	Set the flags.
		 */
		dev->flags |= IFF_UP;

		/*
		 *	Enable NET_DMA
		 */
		net_dmaengine_get();

		/*
		 *	Initialize multicasting status
		 */
		dev_set_rx_mode(dev);

		/*
		 *	Wakeup transmit queue engine
		 */
		dev_activate(dev);

		/*
		 *	... and announce new interface.
		 */
		call_netdevice_notifiers(NETDEV_UP, dev);
	}

	return ret;
}

/**
 *	dev_open	- prepare an interface for use.
 *	@dev:	device to open
 *
 *	Takes a device from down to up state. The device's private open
 *	function is invoked and then the multicast lists are loaded. Finally
 *	the device is moved into the up state and a %NETDEV_UP message is
 *	sent to the netdev notifier chain.
 *
 *	Calling this function on an active interface is a nop. On a failure
 *	a negative errno code is returned.
 *////ic_dev_ioctl->dev_ioctl->dev_ifsioc->dev_change_flags
int dev_open(struct net_device *dev)
{
	int ret;

	/*
	 *	Is it already up?
	 */
	if (dev->flags & IFF_UP)
		return 0;

	/*
	 *	Open device
	 */
	ret = __dev_open(dev);
	if (ret < 0)
		return ret;

	/*
	 *	... and announce new interface.
	 */
	rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP|IFF_RUNNING);
	call_netdevice_notifiers(NETDEV_UP, dev);

	return ret;
}
EXPORT_SYMBOL(dev_open);

static int __dev_c333lose(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	ASSERT_RTNL();
	might_sleep();

	/*
	 *	Tell people we are going down, so that they can
	 *	prepare to death, when device is still operating.
	 */
	call_netdevice_notifiers(NETDEV_GOING_DOWN, dev);

	clear_bit(__LINK_STATE_START, &dev->state);

	/* Synchronize to scheduled poll. We cannot touch poll list,
	 * it can be even on different cpu. So just clear netif_running().
	 *
	 * dev->stop() will invoke napi_disable() on all of it's
	 * napi_struct instances on this device.
	 */
	smp_mb__after_clear_bit(); /* Commit netif_running(). */

	dev_deactivate(dev);

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 *
	 *	We allow it to be called even after a DETACH hot-plug
	 *	event.
	 */
	if (ops->ndo_stop)
		ops->ndo_stop(dev);

	/*
	 *	Device is now down.
	 */

	dev->flags &= ~IFF_UP;

	/*
	 *	Shutdown NET_DMA
	 */
	net_dmaengine_put();

	return 0;
}

/**
 *	dev_close - shutdown an interface.
 *	@dev: device to shutdown
 *
 *	This function moves an active device into down state. A
 *	%NETDEV_GOING_DOWN is sent to the netdev notifier chain. The device
 *	is then deactivated and finally a %NETDEV_DOWN is sent to the notifier
 *	chain.
 */
/*
  * 网络设备一旦关闭后就不能传输数据了。网络
  * 设备能被用户命令明确地活被其他事件隐含地
  * 禁止。在应用层，可以通过ifconfig down命令(最终
  * 是通过ioctl()的SIOCSIFFLAGS)来关闭网络设备，或者
  * 在网络设备注销时被禁止。
  * SIOCSIFFLAGS命令通过dev_change_flags()，根据网络设备
  * 当前的状态来确定调用dev_close()关闭网络设备。
  * dev_close()将网络设备从激活状态转换到关闭状态，
  * 并发送NETDEV_GOING_DOWN和NETDEV_DOWN消息到网络
  * 设备状态改变通知链上。
  *///卸载模块的时候也会调用该函数
int __dev_close(struct net_device *dev)///ic_dev_ioctl->dev_ioctl->dev_ifsioc->dev_change_flags
{
	const struct net_device_ops *ops = dev->netdev_ops;
	ASSERT_RTNL();

	might_sleep();

	/*
	  * 若网络设备未启用，则无需再进行操作。
	  */
	if (!(dev->flags & IFF_UP))
		return 0;

	/*
	 *	Tell people we are going down, so that they can
	 *	prepare to death, when device is still operating.
	 */
	/*
	  * 在关闭网络设备之前，发送NETDEV_GOING_DOWN消息
	  * 到网络设备状态改变通知链上，以便通知
	  * 对设备禁止感兴趣的内核组件
	  */
	call_netdevice_notifiers(NETDEV_GOING_DOWN, dev);

	/*
	  * 将网络设备设置为禁止传递数据包
	  * 状态，设置对应标志。
	  */
	clear_bit(__LINK_STATE_START, &dev->state);

	/* Synchronize to scheduled poll. We cannot touch poll list,
	 * it can be even on different cpu. So just clear netif_running().
	 *
	 * dev->stop() will invoke napi_disable() on all of it's
	 * napi_struct instances on this device.
	 */
	smp_mb__after_clear_bit(); /* Commit netif_running(). */

	/*
	  * 调用dev_deactivate()禁止出口队列规则，确保
	  * 该设备不再用于传输，并停止不再需要
	  * 的监控定时器。
	  */
	dev_deactivate(dev);

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 *
	 *	We allow it to be called even after a DETACH hot-plug
	 *	event.
	 */
	if (ops->ndo_stop)
		ops->ndo_stop(dev);

	/*
	 *	Device is now down.
	 */
	/*
	  * 成功关闭网络设备后去掉已启用标志
	  */
	dev->flags &= ~IFF_UP;

	/*
	 * Tell people we are down
	 */
	/*
	  * 完成关闭设备后，发送NETDEV_DOWN消息到
	  * 网络设备状态改变通知链上，通知
	  * 对设备禁止感兴趣的内核组件。
	  */
	call_netdevice_notifiers(NETDEV_DOWN, dev);

	/*
	 *	Shutdown NET_DMA
	 */
	net_dmaengine_put();

	return 0;
}

/**
 *	dev_close - shutdown an interface.
 *	@dev: device to shutdown
 *
 *	This function moves an active device into down state. A
 *	%NETDEV_GOING_DOWN is sent to the netdev notifier chain. The device
 *	is then deactivated and finally a %NETDEV_DOWN is sent to the notifier
 *	chain.
 */
int dev_c22lose(struct net_device *dev)
{
	if (!(dev->flags & IFF_UP))
		return 0;

	__dev_close(dev);

	/*
	 * Tell people we are down
	 */
	rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP|IFF_RUNNING); //通过dev事件通知链RTM_NEWLINK通知给应用程序，该dev注销了
	call_netdevice_notifiers(NETDEV_DOWN, dev);

	return 0;
}
EXPORT_SYMBOL(dev_close);


/**
 *	dev_disable_lro - disable Large Receive Offload on a device
 *	@dev: device
 *
 *	Disable Large Receive Offload (LRO) on a net device.  Must be
 *	called under RTNL.  This is needed if received packets may be
 *	forwarded to another interface.
 */
void dev_disable_lro(struct net_device *dev)
{
	if (dev->ethtool_ops && dev->ethtool_ops->get_flags &&
	    dev->ethtool_ops->set_flags) {
		u32 flags = dev->ethtool_ops->get_flags(dev);
		if (flags & ETH_FLAG_LRO) {
			flags &= ~ETH_FLAG_LRO;
			dev->ethtool_ops->set_flags(dev, flags);
		}
	}
	WARN_ON(dev->features & NETIF_F_LRO);
}
EXPORT_SYMBOL(dev_disable_lro);


static int dev_boot_phase = 1;//0标识网络设备初始化已完成

/*
 *	Device change register/unregister. These are not inline or static
 *	as we export them to the world.
 */

/**
 *	register_netdevice_notifier - register a network notifier block
 *	@nb: notifier
 *
 *	Register a notifier to be called when network device events occur.
 *	The notifier passed is linked into the kernel structures and must
 *	not be reused until it has been unregistered. A negative errno code
 *	is returned on a failure.
 *
 * 	When registered all registration and up events are replayed
 *	to the new notifier to allow device to have a race free
 *	view of the network device list.
 */ //内核组件对由register_netdevice_notifier 和 unregister_netdevice_notifier分别注册、注销的通知链中的事件感兴趣。
//yang 将处理网络设备事件的函数注册到netdev_chain通知链中  事件通知链(notifier chain)
?/注册时间通知连实际上就是把nb添加到netdev_chain链表中，然后让所有的dev设备执行nb->notifier_call()中的事件函数。可以参考pppoe_init

/*
Linux内核中各个子系统相互依赖，当其中某个子系统状态发生改变时，就必须使用一定的机制告知使用其服务的其他子系统，以便其他子系统采取相应的措施。
为满足这样的需求，内核实现了事件通知链机制（notificationchain）。
*/
/*
Linux的网络子系统一共有3个通知链：表示ipv4地址发生变化时的inetaddr_chain；表示ipv6地址发生变化的inet6addr_chain；还有表示设备注册、
状态变化的netdev_chain。

通知链技术可以概括为：事件的被通知者将事件发生时应该执行的操作通过函数指针方式保存在链表（通知链）中，
然后当事件发生时通知者依次执行链表中每一个元素的回调函数完成通知
*/
int register_netdevice_notifier(struct notifier_block *nb)//和call_netdevice_notifiers配合使用
{
	struct net_device *dev;
	struct net_device *last;
	struct net *net;
	int err;

	rtnl_lock();
	err = raw_notifier_chain_register(&netdev_chain, nb);//按照nb->priority优先级把nb加入到netdev_chain链表中
	if (err)
		goto unlock;
	if (dev_boot_phase)
		goto unlock;
	for_each_net(net) { /* 注意这里让所有的dev设备都执行了一遍nb->notifier_call， 而call_netdevice_notifiers是让netdev_chain中的所有节点notifer_call执行一遍*/
		for_each_netdev(net, dev) {
			err = nb->notifier_call(nb, NETDEV_REGISTER, dev);
			err = notifier_to_errno(err);
			if (err)
				goto rollback;

			if (!(dev->flags & IFF_UP))
				continue;

			nb->notifier_call(nb, NETDEV_UP, dev);
		}
	}

unlock:
	rtnl_unlock();
	return err;

rollback:
	last = dev;
	for_each_net(net) {
		for_each_netdev(net, dev) {
			if (dev == last)
				break;

			if (dev->flags & IFF_UP) {
				nb->notifier_call(nb, NETDEV_GOING_DOWN, dev);
				nb->notifier_call(nb, NETDEV_DOWN, dev);
			}
			nb->notifier_call(nb, NETDEV_UNREGISTER, dev);
			nb->notifier_call(nb, NETDEV_UNREGISTER_BATCH, dev);
		}
	}

	raw_notifier_chain_unregister(&netdev_chain, nb);
	goto unlock;
}
EXPORT_SYMBOL(register_netdevice_notifier);

/**
 *	unregister_netdevice_notifier - unregister a network notifier block
 *	@nb: notifier
 *
 *	Unregister a notifier previously registered by
 *	register_netdevice_notifier(). The notifier is unlinked into the
 *	kernel structures and may then be reused. A negative errno code
 *	is returned on a failure.
 */

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	int err;

	rtnl_lock();
	err = raw_notifier_chain_unregister(&netdev_chain, nb);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier);

/**
 *	call_netdevice_notifiers - call all network notifier blocks
 *      @val: value passed unmodified to notifier function
 *      @dev: net_device pointer passed unmodified to notifier function
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 通知链技术可以概括为：事件的被通知者将事件发生时应该执行的操作通过函数指针方式保存在链表（通知链）中，
 然后当事件发生时通知者依次执行链表中每一个元素的回调函数完成通知
 */
int call_netdevice_notifiers(unsigned long val, struct net_device *dev)//和register_netdevice_notifier配合使用
{
	ASSERT_RTNL();
	return raw_notifier_call_chain(&netdev_chain, val, dev);
}

/* When > 0 there are consumers of rx skb time stamps */
static atomic_t netstamp_needed = ATOMIC_INIT(0);

void net_enable_timestamp(void)
{
	atomic_inc(&netstamp_needed);
}
EXPORT_SYMBOL(net_enable_timestamp);

void net_disable_timestamp(void)
{
	atomic_dec(&netstamp_needed);
}
EXPORT_SYMBOL(net_disable_timestamp);

static inline void net_timestamp_set(struct sk_buff *skb)
{
	if (atomic_read(&netstamp_needed))
		__net_timestamp(skb);
	else
		skb->tstamp.tv64 = 0;
}

static inline void net_timestamp_check(struct sk_buff *skb)
{
	if (!skb->tstamp.tv64 && atomic_read(&netstamp_needed))
		__net_timestamp(skb);
}

/**
 * dev_forward_skb - loopback an skb to another netif
 *
 * @dev: destination network device
 * @skb: buffer to forward
 *
 * return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped, but freed)
 *
 * dev_forward_skb can be used for injecting an skb from the
 * start_xmit function of one device into the receive queue
 * of another device.
 *
 * The receiving device may be in another namespace, so
 * we have to clear all information in the skb that could
 * impact namespace isolation.
 */
int dev_forward_skb(struct net_device *dev, struct sk_buff *skb)
{
	skb_orphan(skb);
	nf_reset(skb);

	if (!(dev->flags & IFF_UP) ||
	    (skb->len > (dev->mtu + dev->hard_header_len + VLAN_HLEN))) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}
	skb_set_dev(skb, dev);
	skb->tstamp.tv64 = 0;
	skb->pkt_type = PACKET_HOST;
	skb->protocol = eth_type_trans(skb, dev);
	return netif_rx(skb);
}
EXPORT_SYMBOL_GPL(dev_forward_skb);

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */
/*
  * 对于通过socket(AF_PACKET， SOCK_RAW，htons(ETH_P_ALL))创建
  * 的原始套接字，不但可以接收从外部输入的数据包，
  * 而且对于由本地输出的数据包，如果满足条件，也同样
  * 可以接收。
  * dev_queue_xmit_nit()就是用来接收由本地输出的数据包，在链路层
  * 的输出过程中，会调用此函数，将满足条件的数据包输入
  * 到RAW套接字。
  * @skb:待输出的数据包，如果满足条件，则输入到原始套接字
  * @dev:输出数据包的网络设备，如果满足条件，则从该网络
  *          设备输入到原始套接字
  */
static void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev)
{
	struct packet_type *ptype;

#ifdef CONFIG_NET_CLS_ACT
	/*
	  * 记录数据包输入的时间戳
	  */
	if (!(skb->tstamp.tv64 && (G_TC_FROM(skb->tc_verd) & AT_INGRESS)))
		net_timestamp(skb);
#else
	net_timestamp(skb);
#endif

	rcu_read_lock();
	/*
	  * 遍历ptype_all链表，查找所有符合输入条件的
	  * 原始套接字，并循环将数据包输入到满足条件
	  * 的套接字
	  */
	list_for_each_entry_rcu(ptype, &ptype_all, list) {
		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		/*
		  * 数据包的输出设备与套接字的输入设备相符
		  * 或者套接字不指定输入设备，并且该数据包
		  * 不是由当前用于比较的套接字输出的(由
		  * 原始套接字输出的数据包不会再次输入给自己)，
		  * 此时该原始套接字满足条件，数据包可以输入
		  */ /*注意这里并没有要求ptype->type == type，所以接收到的包只要有注册ETH_P_ALL协议，所有的包都会走到deliver_skb*/
		if ((ptype->dev == dev || !ptype->dev) &&
		    (ptype->af_packet_priv == NULL ||
		     (struct sock *)ptype->af_packet_priv != skb->sk)) {
			/*
			  * 由于该数据包时额外输入到这个原始套接字的，
			  * 因此需要克隆一个数据包。
			  */
			struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
			if (!skb2)
				break;

			/* skb->nh should be correctly
			   set by sender, so that the second statement is
			   just protection against buggy protocols.
			 */
			/*
			  * 校验数据包是否有效
			  */
			skb_reset_mac_header(skb2);

			if (skb_network_header(skb2) < skb2->data ||
			    skb2->network_header > skb2->tail) {
				if (net_ratelimit())
					printk(KERN_CRIT "protocol %04x is "
					       "buggy, dev %s\n",
					       skb2->protocol, dev->name);
				skb_reset_network_header(skb2);
			}

			/*
			  * 将数据包输入到原始套接字
			  */
			skb2->transport_header = skb2->network_header;
			skb2->pkt_type = PACKET_OUTGOING;
			ptype->func(skb2, skb->dev, ptype, skb->dev);
		}
	}
	rcu_read_unlock();
}

/*
 * Routine to help set real_num_tx_queues. To avoid skbs mapped to queues
 * greater then real_num_tx_queues stale skbs on the qdisc must be flushed.
 */
void netif_set_real_num_tx_queues(struct net_device *dev, unsigned int txq)
{
	unsigned int real_num = dev->real_num_tx_queues;

	if (unlikely(txq > dev->num_tx_queues))
		;
	else if (txq > real_num)
		dev->real_num_tx_queues = txq;
	else if (txq < real_num) {
		dev->real_num_tx_queues = txq;
		qdisc_reset_all_tx_gt(dev, txq);
	}
}
EXPORT_SYMBOL(netif_set_real_num_tx_queues);

//把Qdisc中的数据放入cpu sd的output_queue_tailp输出队列，将队列加入发送软中断NET_TX_SOFTIRQ的处理队列，当软中断被执行时，队列又会继续发送数据包。__netif_reschedule
/*
由于软中断被激活，软中断的优先级仅次于硬中断，这样就保证了队列会被及时的运行，即保证了数据包会被及时的发送。
*///激活发送软件中的，最终调用net_tx_action
//dev_queue_xmit -> __dev_xmit_skb -> __qdisc_run最终调用到该函数，把流控对象Qdisc添加到CPU软中断的output_queue
static inline void __netif_reschedule(struct Qdisc *q)
{
	struct softnet_data *sd;
	unsigned long flags;

    /*
      * 将网络设备链接到softnet_data中的output_queu
      * 队列上，然后激活网络输出软中断对该
      * 队列进行处理。
      */
	local_irq_save(flags);
	sd = &__get_cpu_var(softnet_data);
	q->next_sched = NULL;
	////在net_dev_init中，sd->output_queue_tailp = &sd->output_queue;所以相当于把q添加到了output_queue队列中
	*sd->output_queue_tailp = q;
	sd->output_queue_tailp = &q->next_sched;
	raise_softirq_irqoff(NET_TX_SOFTIRQ); //激活发送软件中的，最终调用net_tx_action
	local_irq_restore(flags);
}

/*
  * 激活数据包输出软中断有多个接口，而
  * __netif_schedule()是最常用的。
  *///激活发送软件中的，最终调用net_tx_action
void __netif_schedule(struct Qdisc *q)
{
	/*
	  * 如果输出网络设备没有处于流量
	  * 控制的调度中，则调用__netif_reschedule()
	  * 激活输出软中断
	  */
	if (!test_and_set_bit(__QDISC_STATE_SCHED, &q->state))
		__netif_reschedule(q);
}

/*
void __netif_schedule(struct Qdisc *q)
{
	if (!test_and_set_bit(__QDISC_STATE_SCHED, &q->state))
		__netif_reschedule(q);
}
*/

EXPORT_SYMBOL(__netif_schedule);

void dev_kfree_skb_irq(struct sk_buff *skb)
{
	if (atomic_dec_and_test(&skb->users)) {
		struct softnet_data *sd;
		unsigned long flags;

		local_irq_save(flags);
		sd = &__get_cpu_var(softnet_data);
		skb->next = sd->completion_queue;
		sd->completion_queue = skb;
		raise_softirq_irqoff(NET_TX_SOFTIRQ);
		local_irq_restore(flags);
	}
}
EXPORT_SYMBOL(dev_kfree_skb_irq);

void dev_kfree_skb_any(struct sk_buff *skb)
{
	if (in_irq() || irqs_disabled())
		dev_kfree_skb_irq(skb);
	else
		dev_kfree_skb(skb);
}
EXPORT_SYMBOL(dev_kfree_skb_any);


/**
 * netif_device_detach - mark device as removed
 * @dev: network device
 *
 * Mark device as removed from system and therefore no longer available.
 */
void netif_device_detach(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
		netif_tx_stop_all_queues(dev);
	}
}
EXPORT_SYMBOL(netif_device_detach);

/**
 * netif_device_attach - mark device as attached
 * @dev: network device
 *
 * Mark device as attached from system and restart if needed.
 */
void netif_device_attach(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
		netif_tx_wake_all_queues(dev);
		__netdev_watchdog_up(dev);
	}
}
EXPORT_SYMBOL(netif_device_attach);

static bool can_checksum_protocol(unsigned long features, __be16 protocol)
{
	return ((features & NETIF_F_NO_CSUM) ||
		((features & NETIF_F_V4_CSUM) &&
		 protocol == htons(ETH_P_IP)) ||
		((features & NETIF_F_V6_CSUM) &&
		 protocol == htons(ETH_P_IPV6)) ||
		((features & NETIF_F_FCOE_CRC) &&
		 protocol == htons(ETH_P_FCOE)));
}

static bool dev_can_checksum(struct net_device *dev, struct sk_buff *skb)
{
	if (can_checksum_protocol(dev->features, skb->protocol))
		return true;

	if (skb->protocol == htons(ETH_P_8021Q)) {
		struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
		if (can_checksum_protocol(dev->features & dev->vlan_features,
					  veh->h_vlan_encapsulated_proto))
			return true;
	}

	return false;
}

/**
 * skb_dev_set -- assign a new device to a buffer
 * @skb: buffer for the new device
 * @dev: network device
 *
 * If an skb is owned by a device already, we have to reset
 * all data private to the namespace a device belongs to
 * before assigning it a new device.
 */
#ifdef CONFIG_NET_NS
void skb_set_dev(struct sk_buff *skb, struct net_device *dev)
{
	skb_dst_drop(skb);
	if (skb->dev && !net_eq(dev_net(skb->dev), dev_net(dev))) {
		secpath_reset(skb);
		nf_reset(skb);
		skb_init_secmark(skb);
		skb->mark = 0;
		skb->priority = 0;
		skb->nf_trace = 0;
		skb->ipvs_property = 0;
#ifdef CONFIG_NET_SCHED
		skb->tc_index = 0;
#endif
	}
	skb->dev = dev;
}
EXPORT_SYMBOL(skb_set_dev);
#endif /* CONFIG_NET_NS */

/*
 * Invalidate hardware checksum when packet is to be mangled, and
 * complete checksum manually on outgoing path.
 */
int skb_checksum_help(struct sk_buff *skb)
{
	__wsum csum;
	int ret = 0, offset;

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		goto out_set_summed;

	if (unlikely(skb_shinfo(skb)->gso_size)) {
		/* Let GSO fix up the checksum. */
		goto out_set_summed;
	}

	offset = skb->csum_start - skb_headroom(skb);
	BUG_ON(offset >= skb_headlen(skb));
	csum = skb_checksum(skb, offset, skb->len - offset, 0);

	offset += skb->csum_offset;
	BUG_ON(offset + sizeof(__sum16) > skb_headlen(skb));

	if (skb_cloned(skb) &&
	    !skb_clone_writable(skb, offset + sizeof(__sum16))) {
		ret = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
		if (ret)
			goto out;
	}

	*(__sum16 *)(skb->data + offset) = csum_fold(csum);
out_set_summed:
	skb->ip_summed = CHECKSUM_NONE;
out:
	return ret;
}
EXPORT_SYMBOL(skb_checksum_help);

/**
 *	skb_gso_segment - Perform segmentation on skb.
 *	@skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 *
 *	This function segments the given skb and returns a list of segments.
 *
 *	It may return NULL if the skb requires no segmentation.  This is
 *	only possible when GSO is used for verifying header integrity.
 */
/*
 * skb_gso_segment()的作用是分段GSO段，返回通过skb->next链接在一起
 * 的段，如果返回NULL，则表示GSO段没有进行分段，参数说明如下：
 * @skb，待分割的GSO数据包
 * @features，输出网络设备支持的GSO特性
 */
struct sk_buff *skb_gso_segment(struct sk_buff *skb, int features)
{
	struct sk_buff *segs = ERR_PTR(-EPROTONOSUPPORT);
	struct packet_type *ptype;
	__be16 type = skb->protocol;
	int err;

    /*
     * 在GSO软分段之前，先去掉以太网帧首部
     */
	skb_reset_mac_header(skb);
	skb->mac_len = skb->network_header - skb->mac_header;
	__skb_pull(skb, skb->mac_len);

    /*
     * 如果待分割的SKB包是克隆的，则需重新分配SKB的
     * 线性数据区
     */
	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL)) {
		struct net_device *dev = skb->dev;
		struct ethtool_drvinfo info = {};

		if (dev && dev->ethtool_ops && dev->ethtool_ops->get_drvinfo)
			dev->ethtool_ops->get_drvinfo(dev, &info);

		WARN(1, "%s: caps=(0x%lx, 0x%lx) len=%d data_len=%d "
			"ip_summed=%d",
		     info.driver, dev ? dev->features : 0L,
		     skb->sk ? skb->sk->sk_route_caps : 0L,
		     skb->len, skb->data_len, skb->ip_summed);

		if (skb_header_cloned(skb) &&
		    (err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
			return ERR_PTR(err);
	}

    /*
     * 根据输出报文的协议类型查找与之对应的GSO接口。如果支持
     * GSO接口，则去掉IP首部，然后再调用gso_segment接口对
     * 大段进行分割，返回相应错误码
     */
	rcu_read_lock();
	list_for_each_entry_rcu(ptype,
			&ptype_base[ntohs(type) & PTYPE_HASH_MASK], list) { //如果是IPV4包，这里为参考ip_packet_type
		if (ptype->type == type && !ptype->dev && ptype->gso_segment) {
			if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL)) {
				err = ptype->gso_send_check(skb);
				segs = ERR_PTR(err);
				if (err || skb_gso_ok(skb, features))
					break;
				__skb_push(skb, (skb->data -
						 skb_network_header(skb)));
			}
			segs = ptype->gso_segment(skb, features);//inet_gso_segment
			break;
		}
	}
	rcu_read_unlock();

    /*
     * 无论是否完成GSO分段，最终都需重新添加以太网帧首部
     */
	__skb_push(skb, skb->data - skb_mac_header(skb));

    /*
     * 返回相应错误码
     */
	return segs;
}
EXPORT_SYMBOL(skb_gso_segment);

/* Take action when hardware reception checksum errors are detected. */
#ifdef CONFIG_BUG
void netdev_rx_csum_fault(struct net_device *dev)
{
	if (net_ratelimit()) {
		printk(KERN_ERR "%s: hw csum failure.\n",
			dev ? dev->name : "<unknown>");
		dump_stack();
	}
}
EXPORT_SYMBOL(netdev_rx_csum_fault);
#endif

/* Actually, we should eliminate this check as soon as we know, that:
 * 1. IOMMU is present and allows to map all the memory.
 * 2. No high memory really exists on this machine.
 */

static int illegal_highdma(struct net_device *dev, struct sk_buff *skb)
{
#ifdef CONFIG_HIGHMEM
	int i;
	if (!(dev->features & NETIF_F_HIGHDMA)) {
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
			if (PageHighMem(skb_shinfo(skb)->frags[i].page))
				return 1;
	}

	if (PCI_DMA_BUS_IS_PHYS) {
		struct device *pdev = dev->dev.parent;

		if (!pdev)
			return 0;
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			dma_addr_t addr = page_to_phys(skb_shinfo(skb)->frags[i].page);
			if (!pdev->dma_mask || addr + PAGE_SIZE - 1 > *pdev->dma_mask)
				return 1;
		}
	}
#endif
	return 0;
}

/*
 * GSO段经分段后所得到的段通过skb->next链接在一起。当释放
 * GSO段的时候，需要将这些链接在一起的段同时释放，为此需要
 * 一个特定的分段GSO段析构函数---dev_gso_skb_destructor()
 * 而原先的析构函数需将其保存在SKB中作为GSO控制块的
 * dev_gso_cb结构中。
 */
struct dev_gso_cb {
	void (*destructor)(struct sk_buff *skb);
};

#define DEV_GSO_CB(skb) ((struct dev_gso_cb *)(skb)->cb)

//gso分段见dev_queue_xmit->dev_gso_segment
static void dev_gso_skb_destructor(struct sk_buff *skb)
{
	struct dev_gso_cb *cb;

    /*
     * 删除并释放除第一个之外的SKB
     */
	do {
		struct sk_buff *nskb = skb->next;

		skb->next = nskb->next;
		nskb->next = NULL;
		kfree_skb(nskb);
	} while (skb->next);

    /*
     * 最后调用原先的析构函数释放第一个SKB
     */
	cb = DEV_GSO_CB(skb);
	if (cb->destructor)
		cb->destructor(skb);
}


/**
 *	dev_gso_segment - Perform emulated hardware segmentation on skb.
 *	@skb: buffer to segment
 *
 *	This function segments the given skb and stores the list of segments
 *	in skb->next.
 */
/*
 * dev_gso_segment()通过调用skb_gso_segment()来分割GSO段
 *///这里的数据时经过__skb_linearize拉直的数据
static int dev_gso_segment(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct sk_buff *segs;
    /*
     * 获取输出网络设备的聚合分散I/O特性
     */
	int features = dev->features & ~(illegal_highdma(dev, skb) ?
					 NETIF_F_SG : 0);

    /*
     * 根据输出网络设备的聚合分散I/O特性，对段进行软GSO分段，
     * 分割后得到的段通过skb->next链接在一起。
     */
	segs = skb_gso_segment(skb, features);

	/* Verifying header integrity only. */
	if (!segs)
		return 0;

	if (IS_ERR(segs))
		return PTR_ERR(segs);

    /*
     * 分段成功后，需保存SKB原来的析构函数，然后重新设置
     * 为特定的分段GSO段析构函数dev_gso_skb_destrutor().
     */
	skb->next = segs;
	DEV_GSO_CB(skb)->destructor = skb->destructor;
	skb->destructor = dev_gso_skb_destructor;

	return 0;
}

/*
 * Try to orphan skb early, right before transmission by the device.
 * We cannot orphan skb if tx timestamp is requested, since
 * drivers need to call skb_tstamp_tx() to send the timestamp.
 */
static inline void skb_orphan_try(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	if (sk && !skb_tx(skb)->flags) {
		/* skb_tx_hash() wont be able to get sk.
		 * We copy sk_hash into skb->rxhash
		 */
		if (!skb->rxhash)
			skb->rxhash = sk->sk_hash;
		skb_orphan(skb);
	}
}

/*
  * dev_hard_start_xmit()将待输出的数据包提交给网络设备的
  * 输出接口，完成数据包的输出。
  */ //走到这里的SKB,通过ip_local_out走到这里,走到这里的SKB在ip_local_out中已经把IP层及其以上各层已经封装完毕。该函数后开始走二层封装
int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
			struct netdev_queue *txq)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int rc;

	/*
	  * 如果输出是单个数据包，通常情况下都是
	  * 输出单独数据包。
	  */
	if (likely(!skb->next)) {
		/*
		  * 如果应用层通过socket(AF_PACKET，SOCK_RAW，htons(ETH_P_ALL))
		  * 创建的原始套接字，则需发送一份数据包给这样
		  * 的套接字。
		  */
		if (!list_empty(&ptype_all))
			dev_queue_xmit_nit(skb, dev);

		/*
		  * 如果待输出数据包是GSO数据包，但网络设备
		  * 不支持相应的特性，则调用dev_gso_segment()对
		  * GSO数据包进行软分割。如果经分割后仍是
		  * 一个数据包，则直接调用网络设备的hard_start_xmit
		  * 接口输出数据包。然而，通常一个GSO数据包经
		  * 软分割，会生成多个链接起来的数据包，如果
		  * 是这样的话就需跳转到gso标签处，逐个处理
		  * 数据包。
		  */ //见dev_gso_segment
		if (netif_needs_gso(dev, skb)) {
			if (unlikely(dev_gso_segment(skb)))
				goto out_kfree_skb;
			if (skb->next)
				goto gso;
		}

		/*
		 * If device doesnt need skb->dst, release it right now while
		 * its hot in this cpu cache
		 */
		if (dev->priv_flags & IFF_XMIT_DST_RELEASE)
			skb_dst_drop(skb);

		/*
		 * e1000网络设备驱动中为e1000_xmit_frame()
		 */
		rc = ops->ndo_start_xmit(skb, dev); //在该函数中封装MAC层
		if (rc == NETDEV_TX_OK)
			txq_trans_update(txq);
		/*
		 * TODO: if skb_orphan() was called by
		 * dev->hard_start_xmit() (for example, the unmodified
		 * igb driver does that; bnx2 doesn't), then
		 * skb_tx_software_timestamp() will be unable to send
		 * back the time stamp.
		 *
		 * How can this be prevented? Always create another
		 * reference to the socket before calling
		 * dev->hard_start_xmit()? Prevent that skb_orphan()
		 * does anything in dev->hard_start_xmit() by clearing
		 * the skb destructor before the call and restoring it
		 * afterwards, then doing the skb_orphan() ourselves?
		 */
		return rc;
	}

gso:
	/*
	  * 当一个GSO数据包经过软分割，生成
	  * 多个链接起来的数据包后，需逐个
	  * 处理数据包。调用网络设备的ndo_start_xmit
	  * 接口(e100网络设备驱动中为e100_xmit_frame())
	  * 输出数据包，如果发生错误，则返回
	  * 相应错误码。
	  */
	do {
		struct sk_buff *nskb = skb->next;

		skb->next = nskb->next;
		nskb->next = NULL;
		rc = ops->ndo_start_xmit(nskb, dev);
		if (unlikely(rc != NETDEV_TX_OK)) {
			nskb->next = skb->next;
			skb->next = nskb;
			return rc;
		}
		txq_trans_update(txq);
		if (unlikely(netif_tx_queue_stopped(txq) && skb->next))
			return NETDEV_TX_BUSY;
	} while (skb->next);

	/*
	  * 成功发送了所有的数据包，需恢复
	  * SKB原先的析构函数。
	  */
	skb->destructor = DEV_GSO_CB(skb)->destructor;

/*
  * 如果调用dev_gso_segment()对GSO数据包进行
  * 软分割失败，会跳转到此丢弃数据包。
  */
out_kfree_skb:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static u32 hashrnd __read_mostly;

u16 skb_tx_hash(const struct net_device *dev, const struct sk_buff *skb)
{
	u32 hash;

	if (skb_rx_queue_recorded(skb)) {
		hash = skb_get_rx_queue(skb);
		while (unlikely(hash >= dev->real_num_tx_queues))
			hash -= dev->real_num_tx_queues;
		return hash;
	}

	if (skb->sk && skb->sk->sk_hash)
		hash = skb->sk->sk_hash;
	else
		hash = (__force u16) skb->protocol ^ skb->rxhash;
	hash = jhash_1word(hash, hashrnd);

	return (u16) (((u64) hash * dev->real_num_tx_queues) >> 32);
}
EXPORT_SYMBOL(skb_tx_hash);

static inline u16 dev_cap_txqueue(struct net_device *dev, u16 queue_index)
{
	if (unlikely(queue_index >= dev->real_num_tx_queues)) {
		if (net_ratelimit()) {
			pr_warning("%s selects TX queue %d, but "
				"real number of TX queues is %d\n",
				dev->name, queue_index, dev->real_num_tx_queues);
		}
		return 0;
	}
	return queue_index;
}

/*
//选择一个发送队列，如果设备提供了select_queue回调函数就使用它，否则由内核选择一个队列     
//大部分驱动都不会设置多个队列，而是在调用alloc_etherdev分配net_device时将队列个数设置为1     
//也就是只有一个队列 
*/
static struct netdev_queue *dev_pick_tx(struct net_device *dev,
					struct sk_buff *skb)
{
	int queue_index;
	struct sock *sk = skb->sk;

	queue_index = sk_tx_queue_get(sk);
	if (queue_index < 0) {
		const struct net_device_ops *ops = dev->netdev_ops;

		if (ops->ndo_select_queue) {
			queue_index = ops->ndo_select_queue(dev, skb);
			queue_index = dev_cap_txqueue(dev, queue_index);
		} else {
			queue_index = 0;
			if (dev->real_num_tx_queues > 1)
				queue_index = skb_tx_hash(dev, skb);

			if (sk) {
				struct dst_entry *dst = rcu_dereference_check(sk->sk_dst_cache, 1);

				if (dst && skb_dst(skb) == dst)
					sk_tx_queue_set(sk, queue_index);
			}
		}
	}

	skb_set_queue_mapping(skb, queue_index);
	return netdev_get_tx_queue(dev, queue_index);
}

static inline int __dev_xmit_skb(struct sk_buff *skb, struct Qdisc *q,
				 struct net_device *dev,
				 struct netdev_queue *txq)
{
	spinlock_t *root_lock = qdisc_lock(q);
	int rc;

	spin_lock(root_lock);
	if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED, &q->state))) {
		kfree_skb(skb);//如果这个队列是未运行的，那么释放这个数据包
		rc = NET_XMIT_DROP;
	} else if ((q->flags & TCQ_F_CAN_BYPASS) && !qdisc_qlen(q) &&
		   !test_and_set_bit(__QDISC_STATE_RUNNING, &q->state)) {
		/* //如果一个队列是位运行的，说明这个队列里面没有数据包，此时可以直接发送这个包
		 * This is a work-conserving queue; there are no old skbs
		 * waiting to be sent out; and the qdisc is not running -
		 * xmit the skb directly.
		 */
		if (!(dev->priv_flags & IFF_XMIT_DST_RELEASE))
			skb_dst_force(skb);
		__qdisc_update_bstats(q, skb->len);
		if (sch_direct_xmit(skb, q, dev, txq, root_lock))
			__qdisc_run(q);//试图直接发送数据包，如果没有发送成功，或者队列中还有待发数据包，返回值会大于0，那么，此时需要激活这个队列。
		else
			clear_bit(__QDISC_STATE_RUNNING, &q->state);

		rc = NET_XMIT_SUCCESS;
	} else {//如果已经有CPU在运行这个队列，那么字节返回，因为一个队列只能由一个CPU运行。则直接把SKB入队
		skb_dst_force(skb);
		rc = qdisc_enqueue_root(skb, q);//则直接把SKB入队,参考pfifo_qdisc_ops
		qdisc_run(q);
	}
	spin_unlock(root_lock);

	return rc;
}

/*
 * Returns true if either:
 *	1. skb has frag_list and the device doesn't support FRAGLIST, or
 *	2. skb is fragmented and the device does not support SG, or if
 *	   at least one of fragments is in highmem and device does not
 *	   support DMA from it.
 */
static inline int skb_needs_linearize(struct sk_buff *skb,
				      struct net_device *dev)
{
	return (skb_has_frags(skb) && !(dev->features & NETIF_F_FRAGLIST)) ||
	       (skb_shinfo(skb)->nr_frags && (!(dev->features & NETIF_F_SG) ||
					      illegal_highdma(dev, skb)));
}

static DEFINE_PER_CPU(int, xmit_recursion);
#define RECURSION_LIMIT 10

/**
 *	dev_queue_xmit - transmit a buffer
 *	@skb: buffer to transmit
 *
 *	Queue a buffer for transmission to a network device. The caller must
 *	have set the device and priority and built the buffer before calling
 *	this function. The function can be called from an interrupt.
 *
 *	A negative errno code is returned on a failure. A success does not
 *	guarantee the frame will be transmitted as it may be dropped due
 *	to congestion or traffic shaping.
 *
 * -----------------------------------------------------------------------------------
 *      I notice this method can also return errors from the queue disciplines,
 *      including NET_XMIT_DROP, which is a positive value.  So, errors can also
 *      be positive.
 *
 *      Regardless of the return value, the skb is consumed, so it is currently
 *      difficult to retry a send to this method.  (You can bump the ref count
 *      before sending to hold a reference for retry if you are careful.)
 *
 *      When calling this method, interrupts MUST be enabled.  This is because
 *      the BH enable code must have IRQs enabled so that it will not deadlock.
 *          --BLG
 协议栈向设备发送数据包时都需调用该函数，该函数对SKB进行排队，最终由底层设备驱动程序进行传输
 */
    
    /**
     *  dev_queue_xmit - transmit a buffer
     *  @skb: buffer to transmit
     *
     *  Queue a buffer for transmission to a network device. The caller must
     *  have set the device and priority and built the buffer before calling
     *  this function. The function can be called from an interrupt.
     *
     *  A negative errno code is returned on a failure. A success does not
     *  guarantee the frame will be transmitted as it may be dropped due
     *  to congestion or traffic shaping.
     *
     * -----------------------------------------------------------------------------------
     *      I notice this method can also return errors from the queue disciplines,
     *      including NET_XMIT_DROP, which is a positive value.  So, errors can also
     *      be positive.
     *
     *      Regardless of the return value, the skb is consumed, so it is currently
     *      difficult to retry a send to this method.  (You can bump the ref count
     *      before sending to hold a reference for retry if you are careful.)
     *
     *      When calling this method, interrupts MUST be enabled.  This is because
     *      the BH enable code must have IRQs enabled so that it will not deadlock.
     *          --BLG
     */
 /*
  * 网络接口口核心层向网络协议层提供的统一
  * 的发送接口，无论IP，还是ARP协议，以及其它
  * 各种底层协议，通过这个函数把要发送的数据
  * 传递给网络接口核心层
  * 
  * update:
  *   若支持流量控制，则将待输出的数据包根据规则
  * 加入到输出网络队列中排队，并在合适的时机激活
  * 网络设备输出软中断，依次将报文从队列中取出通过
  * 网络设备输出。若不支持流量控制，则直接将数据包
  * 从网络设备输出。
  *   如果提交失败，则返回相应的错误码，然而返回
  * 成功也并不能确保数据包被成功发送，因为有可能
  * 由于拥塞而导致流量控制机制将数据包丢弃。
  *   调用dev_queue_xmit()函数输出数据包，前提是必须启用
  * 中断，只有启用中断之后才能激活下半部。
  */ //到这里的skb可能有以下三种:支持GSO(FRAGLIST类型的聚合分散I/O数据包, 对于SG类型的聚合分散I/O数据包), 或者是非GSO的SKB，但这里的skb是在ip_finish_output中分片后的skb
int dev_queue_xmit(struct sk_buff *skb) //通过ip_local_out走到这里,走到这里的SKB起IP层及其以上各层已经封装完毕。
{
    struct net_device *dev = skb->dev;
    struct netdev_queue *txq;
    struct Qdisc *q;
    int rc = -ENOMEM;

    /* GSO will handle the following emulations directly. */
    /*
      * 如果是GSO数据包，且网络设备支持
      * GSO数据包的处理，则跳转到
      * gso标签处对GSO数据包直接处理。
      */
    if (netif_needs_gso(dev, skb))
        goto gso;

    /*
      * 对于FRAGLIST类型的聚合分散I/O数据包，
      * 如果输出网络设备不支持FRAGLIST类型的
      * 聚合分散I/O(目前只有回环设备支持)，
      * 则需将其线性化。若线性化失败，则
      * 丢弃数据包，发送失败。
      //如果发送的数据包是分片 但网卡不支持skb的碎片列表,则需要调用函数__skb_linearize把这些碎片重组到一个完整的skb中
      */
    if (skb_has_frags(skb) &&
        !(dev->features & NETIF_F_FRAGLIST) &&
        __skb_linearize(skb))
        goto out_kfree_skb;

    /* Fragmented skb is linearized if device does not support SG,
     * or if at least one of fragments is in highmem and device
     * does not support DMA from it.
     */
    /*
      * 对于SG类型的聚合分散I/O数据包，如果
      * 输出网络设备不支持SG类型的聚合分散I/O，
      * 则需将其线性化。如果网络设备不支持
      * 在高端内存使用DMA，但高端内存中有分片，
      * 此时也需要将数据包线性化。若线性化失败，
      * 则丢弃该数据包，发送失败。
       //如果要发送的数据包使用了分散/聚合i/o 但网卡不支持或分片中至少有一个在高端内存中,并且网卡不支持dma,则同样需要调用函数__skb_linearize
       进行线性化处理 
      */
    if (skb_shinfo(skb)->nr_frags &&
        (!(dev->features & NETIF_F_SG) || illegal_highdma(dev, skb)) &&
        __skb_linearize(skb))
        goto out_kfree_skb;

    /* If packet is not checksummed and device does not support
     * checksumming for this protocol, complete checksumming here.
     */
    /*
      * 如果待输出的数据包由硬件来执行校验和
      * (尚未执行校验和)，但网络设备不支持
      * 硬件执行校验和，不支持对IP报文执行
      * 校验和，则在此处计算校验和。若
      * 校验和失败，则丢弃数据包，发送失败。
      */
    if (skb->ip_summed == CHECKSUM_PARTIAL) {
        skb_set_transport_header(skb, skb->csum_start -
                          skb_headroom(skb));
        if (!dev_can_checksum(dev, skb) && skb_checksum_help(skb))
            goto out_kfree_skb;
    }

gso:
    /* Disable soft irqs for various locks below. Also
     * stops preemption for RCU.
     */
    rcu_read_lock_bh();

    /* 获取dev设备上的排队规程，如果执行了tc qdisc add dev eth0 就会找到对应的Qdisc */
    txq = dev_pick_tx(dev, skb);
    /*
      * 获取输出网络设备的排队规程。rcu_dereference()在
      * RCU读临界部分中取出一个RCU保护的指针。在
      * 需要内存屏障的体系中进行内存屏障，目前
      * 只有Alpha体系需要。
      */
    q = rcu_dereference(txq->qdisc); //实际上就是获取net_device -> netdev_queue  也就是该dev设备的跟qdisc

#ifdef CONFIG_NET_CLS_ACT
    /*
      * 与包分类器相关
      */
    skb->tc_verd = SET_TC_AT(skb->tc_verd, AT_EGRESS);
#endif
    /*
      * 如果获取的排队规程定义了"入队"操作，
      * 说明启用了QoS。
      */ /*如果这个设备启动了TC,那么把数据包压入队列  见tc_modify_qdisc中的qdisc_graft*/ 
    if (q->enqueue) {//则对这个数据包进行QoS处理。 /* qos源码分析参考<TC流速流量控制分析> */  //alloc_netdev_mq可以看出开辟的q的空间为空的，如果不赋值的话
    //进入出口流控的函数为dev_queue_xmit(); 如果是入口流控, 数据只是刚从网卡设备中收到, 还未交到网络上层处理, 
    //不过网卡的入口流控不是必须的, 缺省情况下并不进行流控，进入入口流控函数为ing_filter()函数，该函数被skb_receive_skb()调用。
        /*
          * 将待发送的数据包按排队规则插入到
          * 队列，然后进行流量控制，调度队列
          * 输出数据包，完成后返回。
          */
        rc = __dev_xmit_skb(skb, q, dev, txq);
        goto out;//数据包入队后，整个入队流程就结束了
    }

    /* The device has no queue. Common case for software devices:
       loopback, all the sorts of tunnels...

       Really, it is unlikely that netif_tx_lock protection is necessary
       here.  (f.e. loopback and IP tunnels are clean ignoring statistics
       counters.)
       However, it is possible, that they rely on protection
       made by us here.

       Check this and shot the lock. It is not prone from deadlocks.
       Either shot noqueue qdisc, it is even simpler 8)
     */
    /*
      * 如果设备已打开但未启用QoS，则直接输出
      * 数据包。
      */
    if (dev->flags & IFF_UP) {
        int cpu = smp_processor_id(); /* ok because BHs are off */

        /*
          * HARD_TX_LOCK/HARD_TX_UNLOCK是一对操作，
          * 在这两个操作之间不能再次调用
          * dev_queue_xmit接口。因此如果正在用
          * 该网络设备发送数据包的CPU又
          * 调用dev_queue_xmit()输出数据包，则
          * 说明代码有bug，需输出警告信息。
          *   否则，首先需加锁，以防止其他CPU
          * 的并发操作，然后在网络设备处于开启
          * 状态时，调用dev_hard_start_xmit()输出数据包
          * 到网络设备。
          */
        if (txq->xmit_lock_owner != cpu) {

            HARD_TX_LOCK(dev, txq, cpu);

            if (!netif_tx_queue_stopped(txq)) {
                rc = NET_XMIT_SUCCESS;
                if (!dev_hard_start_xmit(skb, dev, txq)) {
                    HARD_TX_UNLOCK(dev, txq);
                    goto out;
                }
            }
            HARD_TX_UNLOCK(dev, txq);
            if (net_ratelimit())
                printk(KERN_CRIT "Virtual device %s asks to "
                       "queue packet!\n", dev->name);
        } else {
            /* Recursion is detected! It is possible,
             * unfortunately */
            if (net_ratelimit())
                printk(KERN_CRIT "Dead loop on virtual device "
                       "%s, fix it urgently!\n", dev->name);
        }
    }

    /*
      * 如果网络设备处于关闭状态，则返回
      * 相应的错误码。
      */
    rc = -ENETDOWN;
    rcu_read_unlock_bh();

/*
  * 凡跳转到此处的都是输出数据包时出现错误的，
  * 如聚合分散I/O数据包线性化失败，丢弃数据包。
  */
out_kfree_skb:
    kfree_skb(skb);
    return rc;
out:
    /*
      * 完成数据包输出后，返回相应结果。
      */
    rcu_read_unlock_bh();
    return rc;
}

int dev_queue_x11mit(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	struct Qdisc *q;
	int rc = -ENOMEM;

	/* GSO will handle the following emulations directly. */
	if (netif_needs_gso(dev, skb))
		goto gso;

	/* Convert a paged skb to linear, if required */
	if (skb_needs_linearize(skb, dev) && __skb_linearize(skb))
		goto out_kfree_skb;

	/* If packet is not checksummed and device does not support
	 * checksumming for this protocol, complete checksumming here.
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		skb_set_transport_header(skb, skb->csum_start -
					      skb_headroom(skb));
		if (!dev_can_checksum(dev, skb) && skb_checksum_help(skb))
			goto out_kfree_skb;
	}

gso:
	/* Disable soft irqs for various locks below. Also
	 * stops preemption for RCU.
	 */
	rcu_read_lock_bh();

	txq = dev_pick_tx(dev, skb);
	q = rcu_dereference_bh(txq->qdisc);

#ifdef CONFIG_NET_CLS_ACT
	skb->tc_verd = SET_TC_AT(skb->tc_verd, AT_EGRESS);
#endif
	if (q->enqueue) {
		rc = __dev_xmit_skb(skb, q, dev, txq);
		goto out;
	}

	/* The device has no queue. Common case for software devices:
	   loopback, all the sorts of tunnels...

	   Really, it is unlikely that netif_tx_lock protection is necessary
	   here.  (f.e. loopback and IP tunnels are clean ignoring statistics
	   counters.)
	   However, it is possible, that they rely on protection
	   made by us here.

	   Check this and shot the lock. It is not prone from deadlocks.
	   Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags & IFF_UP) {
		int cpu = smp_processor_id(); /* ok because BHs are off */

		if (txq->xmit_lock_owner != cpu) {

			if (__this_cpu_read(xmit_recursion) > RECURSION_LIMIT)
				goto recursion_alert;

			HARD_TX_LOCK(dev, txq, cpu);

			if (!netif_tx_queue_stopped(txq)) {
				__this_cpu_inc(xmit_recursion);
				rc = dev_hard_start_xmit(skb, dev, txq);
				__this_cpu_dec(xmit_recursion);
				if (dev_xmit_complete(rc)) {
					HARD_TX_UNLOCK(dev, txq);
					goto out;
				}
			}
			HARD_TX_UNLOCK(dev, txq);
			if (net_ratelimit())
				printk(KERN_CRIT "Virtual device %s asks to "
				       "queue packet!\n", dev->name);
		} else {
			/* Recursion is detected! It is possible,
			 * unfortunately
			 */
recursion_alert:
			if (net_ratelimit())
				printk(KERN_CRIT "Dead loop on virtual device "
				       "%s, fix it urgently!\n", dev->name);
		}
	}

	rc = -ENETDOWN;
	rcu_read_unlock_bh();

out_kfree_skb:
	kfree_skb(skb);
	return rc;
out:
	rcu_read_unlock_bh();
	return rc;
}
EXPORT_SYMBOL(dev_queue_xmit);


/*=======================================================================
			Receiver routines
  =======================================================================*/

int netdev_max_backlog __read_mostly = 1000;
int netdev_tstamp_prequeue __read_mostly = 1;
int netdev_budget __read_mostly = 300;
int weight_p __read_mostly = 64;            /* old backlog weight */

/* Called with irq disabled */
//它的作用就是网卡的数据链表添加到poll_list里，然后开启软中断, 
//函数__raise_softirq_irqoff最终会调用wakeup_softirqd(void)。
/*
这是NAPI方式，把dev设备添加到了poll_list链表中。
每个网络设备（MAC层）都有自己的net_device数据结构，这个结构上有napi_struct。
每当收到数据包时，网络设备驱动会把自己的napi_struct挂到CPU私有变量上。
这样在软中断时，net_rx_action会遍历cpu私有变量的poll_list，
执行上面所挂的napi_struct结构的poll钩子函数,将数据包从驱动传到网络协议栈。
NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。

非NAPI的napi_struct结构是默认的，也就是per cpu的softnet_data->backlog，
起poll钩子函数为process_backlog
*/
static inline void ____napi_schedule(struct softnet_data *sd,
				     struct napi_struct *napi)
{
	list_add_tail(&napi->poll_list, &sd->poll_list);
	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
}

#ifdef CONFIG_RPS

/* One global table that all flow-based protocols share. */
struct rps_sock_flow_table *rps_sock_flow_table __read_mostly;
EXPORT_SYMBOL(rps_sock_flow_table);

/*
 * get_rps_cpu is called from netif_receive_skb and returns the target
 * CPU from the RPS map of the receiving queue for a given skb.
 * rcu_read_lock must be held on entry.
 */
static int get_rps_cpu(struct net_device *dev, struct sk_buff *skb,
		       struct rps_dev_flow **rflowp)
{
	struct ipv6hdr *ip6;
	struct iphdr *ip;
	struct netdev_rx_queue *rxqueue;
	struct rps_map *map;
	struct rps_dev_flow_table *flow_table;
	struct rps_sock_flow_table *sock_flow_table;
	int cpu = -1;
	u8 ip_proto;
	u16 tcpu;
	u32 addr1, addr2, ihl;
	union {
		u32 v32;
		u16 v16[2];
	} ports;

	if (skb_rx_queue_recorded(skb)) {
		u16 index = skb_get_rx_queue(skb);
		if (unlikely(index >= dev->num_rx_queues)) {
			WARN_ONCE(dev->num_rx_queues > 1, "%s received packet "
				"on queue %u, but number of RX queues is %u\n",
				dev->name, index, dev->num_rx_queues);
			goto done;
		}
		rxqueue = dev->_rx + index;
	} else
		rxqueue = dev->_rx;

	if (!rxqueue->rps_map && !rxqueue->rps_flow_table)
		goto done;

	if (skb->rxhash)
		goto got_hash; /* Skip hash computation on packet header */

	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		if (!pskb_may_pull(skb, sizeof(*ip)))
			goto done;

		ip = (struct iphdr *) skb->data;
		ip_proto = ip->protocol;
		addr1 = (__force u32) ip->saddr;
		addr2 = (__force u32) ip->daddr;
		ihl = ip->ihl;
		break;
	case __constant_htons(ETH_P_IPV6):
		if (!pskb_may_pull(skb, sizeof(*ip6)))
			goto done;

		ip6 = (struct ipv6hdr *) skb->data;
		ip_proto = ip6->nexthdr;
		addr1 = (__force u32) ip6->saddr.s6_addr32[3];
		addr2 = (__force u32) ip6->daddr.s6_addr32[3];
		ihl = (40 >> 2);
		break;
	default:
		goto done;
	}
	switch (ip_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_DCCP:
	case IPPROTO_ESP:
	case IPPROTO_AH:
	case IPPROTO_SCTP:
	case IPPROTO_UDPLITE:
		if (pskb_may_pull(skb, (ihl * 4) + 4)) {
			ports.v32 = * (__force u32 *) (skb->data + (ihl * 4));
			if (ports.v16[1] < ports.v16[0])
				swap(ports.v16[0], ports.v16[1]);
			break;
		}
	default:
		ports.v32 = 0;
		break;
	}

	/* get a consistent hash (same value on both flow directions) */
	if (addr2 < addr1)
		swap(addr1, addr2);
	skb->rxhash = jhash_3words(addr1, addr2, ports.v32, hashrnd);
	if (!skb->rxhash)
		skb->rxhash = 1;

got_hash:
	flow_table = rcu_dereference(rxqueue->rps_flow_table);
	sock_flow_table = rcu_dereference(rps_sock_flow_table);
	if (flow_table && sock_flow_table) {
		u16 next_cpu;
		struct rps_dev_flow *rflow;

		rflow = &flow_table->flows[skb->rxhash & flow_table->mask];
		tcpu = rflow->cpu;

		next_cpu = sock_flow_table->ents[skb->rxhash &
		    sock_flow_table->mask];

		/*
		 * If the desired CPU (where last recvmsg was done) is
		 * different from current CPU (one in the rx-queue flow
		 * table entry), switch if one of the following holds:
		 *   - Current CPU is unset (equal to RPS_NO_CPU).
		 *   - Current CPU is offline.
		 *   - The current CPU's queue tail has advanced beyond the
		 *     last packet that was enqueued using this table entry.
		 *     This guarantees that all previous packets for the flow
		 *     have been dequeued, thus preserving in order delivery.
		 */
		if (unlikely(tcpu != next_cpu) &&
		    (tcpu == RPS_NO_CPU || !cpu_online(tcpu) ||
		     ((int)(per_cpu(softnet_data, tcpu).input_queue_head -
		      rflow->last_qtail)) >= 0)) {
			tcpu = rflow->cpu = next_cpu;
			if (tcpu != RPS_NO_CPU)
				rflow->last_qtail = per_cpu(softnet_data,
				    tcpu).input_queue_head;
		}
		if (tcpu != RPS_NO_CPU && cpu_online(tcpu)) {
			*rflowp = rflow;
			cpu = tcpu;
			goto done;
		}
	}

	map = rcu_dereference(rxqueue->rps_map);
	if (map) {
		tcpu = map->cpus[((u64) skb->rxhash * map->len) >> 32];

		if (cpu_online(tcpu)) {
			cpu = tcpu;
			goto done;
		}
	}

done:
	return cpu;
}

/* Called from hardirq (IPI) context */
static void rps_trigger_softirq(void *data)
{
	struct softnet_data *sd = data;

	____napi_schedule(sd, &sd->backlog);
	sd->received_rps++;
}

#endif /* CONFIG_RPS */

/*
 * Check if this softnet_data structure is another cpu one
 * If yes, queue it to our IPI list and return 1
 * If no, return 0
 */
static int rps_ipi_queued(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *mysd = &__get_cpu_var(softnet_data);

	if (sd != mysd) {
		sd->rps_ipi_next = mysd->rps_ipi_list;
		mysd->rps_ipi_list = sd;

		__raise_softirq_irqoff(NET_RX_SOFTIRQ);
		return 1;
	}
#endif /* CONFIG_RPS */
	return 0;
}

/*
 * enqueue_to_backlog is called to queue an skb to a per CPU backlog
 * queue (may be a remote CPU queue).
 */

/* 队列中.在中断轮询的时候,软中断总函数do_softirq()直接到达网卡的接收软中断函数net_rx_action()，
   在此函数中调用queue->backlog_dev.poll=process_backlog;即process_backlog()函数，它将queue->input_pkt_queue
   队列中的数据向上层协议传输，比如网络层的ip协议等。
*/
/*
            非NAPI方式                                              NAPI方式NAPI方式(NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。使用参考:网口收发包以及NAPI_huwei_10_新浪博客.htm)

                                        IRQ
                                         |
                  _______________________|_____________________________
                  |                                                     |
             netif_rx                                            napi_schedule
 上半部           |                                                     | 
             enqueue_to_backlog                                  __napi_schedule
                  |                                                     |           
            skb加入input_pkt_queue 中                           napi_struct加入poll_list中
            softnet_data->backlog加入poll_list中                                      | 
                   |____________________________________________________| 
                                             |
                                        net_rx_action
下半部                                       |
                      _______________________|_____________________________
                      |                                                     |
            porcess_backlog->__netif_receive_skb                驱动poll方法->napi_gro_receive->netif_receive_skb->__netif_receive_skb

*/
//通过硬件中断接收SKB，然后在硬件中断中继续执行下面的函数。
static int enqueue_to_backlog(struct sk_buff *skb, int cpu,
			      unsigned int *qtail)
{
	struct softnet_data *sd;
	unsigned long flags;

	sd = &per_cpu(softnet_data, cpu);

	local_irq_save(flags);//关中断，当该SKB添加到输入队列input_pkt_queue后打开中断，继续从硬件中断中接收输入然后放入该接收队列中

	rps_lock(sd);
	if (skb_queue_len(&sd->input_pkt_queue) <= netdev_max_backlog) {  /* 空间已有存储的数据帧 */
        
		if (skb_queue_len(&sd->input_pkt_queue)) {
enqueue:
           /* 
           队列中.在中断轮询的时候,软中断总函数do_softirq()
           直接到达网卡的接收软中断函数 net_rx_action()，
           在此函数中调用queue->backlog_dev.poll=process_backlog;
           即process_backlog()函数，它将queue->input_pkt_queue
           队列中的数据向上层协议传输，比如网络层的ip协议等。
        	*/
			__skb_queue_tail(&sd->input_pkt_queue, skb);  /* 挂softnet_data输入队列 */ //net_rx_action中会对包的个数，以及软中断处理时间进行限制
            
			input_queue_tail_incr_save(sd, qtail);
			rps_unlock(sd);
			local_irq_restore(flags);//打开中断，当该SKB添加到输入队列input_pkt_queue后打开中断，继续从硬件中断中接收输入然后放入该接收队列中
			return NET_RX_SUCCESS;
		}

		/* Schedule NAPI for backlog device
		 * We can use non atomic operation since we own the queue lock
		 */
		if (!__test_and_set_bit(NAPI_STATE_SCHED, &sd->backlog.state)) {
			if (!rps_ipi_queued(sd))
			    /* &sd->backlog加入napi->poll_list，backlog即函数process_backlog */
				//这里就会调用net_dev_init中的->backlog_dev.poll=process_backlog
				//从而到 process_backlog 中执行
				____napi_schedule(sd, &sd->backlog); 
		}
		goto enqueue;
	}

	sd->dropped++;
	rps_unlock(sd);

	local_irq_restore(flags);

	kfree_skb(skb);
	return NET_RX_DROP;
}

/**
 *	netif_rx	-	post buffer to the network code
 *	@skb: buffer to post
 *
 *	This function receives a packet from a device driver and queues it for
 *	the upper (protocol) levels to process.  It always succeeds. The buffer
 *	may be dropped during processing for congestion control or by the
 *	protocol layers.
 *
 *	return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped)
 *
 */
//当底层设备驱动程序接收一个报文时，就会通过调用netif_rx将报文的SKB上传至网络层。
/*
在netif_rx函数中会调用netif_rx_schedule, 然后该函数又会去调用__netif_rx_schedule
在函数__netif_rx_schedule中会去触发软中断NET_RX_SOFTIRQ, 也即是去调用net_rx_action.
然后在net_rx_action函数中会去调用设备的poll函数, 它是设备自己注册的.
在设备的poll函数中, 会去调用netif_receive_skb函数,  在该函数中有下面一条语句 pt_prev->func, 此处的func为一个函数指针, 在之前的注册中设置为ip_rcv.
因此, 就完成了从链路层上传到网络层的这一个过程了.
*/ //非NAPI方式，从驱动硬件中断中调用这个netif_rx函数，而NAPI方式从硬件中断中调用napi_schedule激活软中断, 参考 数据包接收系列 — NAPI的原理和实现 http://blog.csdn.net/zhangskd/article/details/21627963

/*
            非NAPI方式                                              NAPI方式NAPI方式(NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。使用参考:网口收发包以及NAPI_huwei_10_新浪博客.htm)

                                        IRQ
                                         |
                  _______________________|_____________________________
                  |                                                     |
             netif_rx                                            napi_schedule
 上半部           |                                                     | 
             enqueue_to_backlog                                  __napi_schedule
                  |                                                     |           
            skb加入input_pkt_queuem中                           napi_struct加入poll_list中
            softnet_data->backlog加入poll_list中                                      | 
                   |____________________________________________________| 
                                             |
                                        net_rx_action
下半部                                       |
                      _______________________|_____________________________
                      |                                                     |
            process_backlog->__netif_receive_skb                驱动poll方法->napi_gro_receive->netif_receive_skb->__netif_receive_skb

*/
int netif_rx(struct sk_buff *skb)
{
	int ret;

	/* if netpoll wants it, pretend we never saw it */
	if (netpoll_rx(skb))
		return NET_RX_DROP;

	if (netdev_tstamp_prequeue)
		net_timestamp_check(skb);

#ifdef CONFIG_RPS
	{
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu;

		preempt_disable();
		rcu_read_lock();

		cpu = get_rps_cpu(skb->dev, skb, &rflow);
		if (cpu < 0)
			cpu = smp_processor_id();	// just return 0

		//这里面的数据再 process_backlog
		ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);

		rcu_read_unlock();
		preempt_enable();
	}
#else
	{
		unsigned int qtail;
		// add to input_pkt_queue and softirq is triggered
		ret = enqueue_to_backlog(skb, get_cpu(), &qtail);
		put_cpu();
	}
#endif
	return ret;
}
EXPORT_SYMBOL(netif_rx);

int netif_rx_ni(struct sk_buff *skb)
{
	int err;

	preempt_disable();
	err = netif_rx(skb);
	if (local_softirq_pending())
		do_softirq();
	preempt_enable();

	return err;
}
EXPORT_SYMBOL(netif_rx_ni);

/*
  * net_tx_action()是数据包输出软中断的例程，
  * 一旦激活便会遍历output_queue队列中
  * 待处理的输出网络设备，然后调用
  * qdisc_run()在合适的时机发送数据包。
  * 数据包输出软中断通常有netif_schedule()激活。
  */ //qos tc 流量控制的时候会用到
static void net_tx_action(struct softirq_action *h)
{
	struct softnet_data *sd = &__get_cpu_var(softnet_data);

	/*
	  * 如果当前CPU的softnet_data中存在已完成
	  * 输出待释放的数据包，则遍历
	  * completion_queue队列，释放该队列中所有
	  * 数据包
	  */
	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_disable();
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_enable();

		while (clist) {
			struct sk_buff *skb = clist;
			clist = clist->next;

			WARN_ON(atomic_read(&skb->users));
			__kfree_skb(skb);
		}
	}

	/*
	  * 如果当前CPU的softnet_data中存在待处理的输出网络
	  * 设备，则遍历output_queue队列，调用qdisc_run()来发送
	  * 数据包或者再次调度数据包输出软中断，在
	  * 合适的时机发送数据包。
	  */
	if (sd->output_queue) {
		struct Qdisc *head;

		local_irq_disable();
		head = sd->output_queue;
		sd->output_queue = NULL;
		local_irq_enable();

		while (head) {
			struct Qdisc *q = head;
			spinlock_t *root_lock;

			head = head->next_sched;

			root_lock = qdisc_lock(q);
			if (spin_trylock(root_lock)) {
				smp_mb__before_clear_bit();
				clear_bit(__QDISC_STATE_SCHED,
					  &q->state);
				qdisc_run(q);
				spin_unlock(root_lock);
			} else {		// couldn't dequeue and send out this pkt now
				if (!test_bit(__QDISC_STATE_DEACTIVATED,
					      &q->state)) {		// qdisc is active
					__netif_reschedule(q);		// re-scheding it again
				} 
				else {		// qdisc is not running
					smp_mb__before_clear_bit();
					clear_bit(__QDISC_STATE_SCHED,
						  &q->state);
				}
			}
		}
	}
}

static inline int deliver_skb(struct sk_buff *skb,
			      struct packet_type *pt_prev,
			      struct net_device *orig_dev)
{
	atomic_inc(&skb->users);
	return pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
}

#if defined(CONFIG_BRIDGE) || defined (CONFIG_BRIDGE_MODULE)

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
/* This hook is defined here for ATM LANE */
int (*br_fdb_test_addr_hook)(struct net_device *dev,
			     unsigned char *addr) __read_mostly;
EXPORT_SYMBOL_GPL(br_fdb_test_addr_hook);
#endif

/*
 * If bridge module is loaded call bridging hook.
 *  returns NULL if packet was consumed.
 */ //这是一个函数指针
struct sk_buff *(*br_handle_frame_hook)(struct net_bridge_port *p, struct sk_buff *skb) ;//__read_mostly;
EXPORT_SYMBOL_GPL(br_handle_frame_hook);

static inline struct sk_buff *handle_bridge(struct sk_buff *skb,
					    struct packet_type **pt_prev, int *ret,
					    struct net_device *orig_dev)
{
	struct net_bridge_port *port;

	if (skb->pkt_type == PACKET_LOOPBACK ||
	    (port = rcu_dereference(skb->dev->br_port)) == NULL)
		return skb;

	if (*pt_prev) {
		*ret = deliver_skb(skb, *pt_prev, orig_dev);
		*pt_prev = NULL;
	}

	return br_handle_frame_hook(port, skb); //br_handle_frame_hook = br_handle_frame;
}
#else
#define handle_bridge(skb, pt_prev, ret, orig_dev)	(skb)
#endif

#if defined(CONFIG_MACVLAN) || defined(CONFIG_MACVLAN_MODULE)
struct sk_buff *(*macvlan_handle_frame_hook)(struct macvlan_port *p,
					     struct sk_buff *skb) __read_mostly;
EXPORT_SYMBOL_GPL(macvlan_handle_frame_hook);

static inline struct sk_buff *handle_macvlan(struct sk_buff *skb,
					     struct packet_type **pt_prev,
					     int *ret,
					     struct net_device *orig_dev)
{
	struct macvlan_port *port;

	port = rcu_dereference(skb->dev->macvlan_port);
	if (!port)
		return skb;

	if (*pt_prev) {
		*ret = deliver_skb(skb, *pt_prev, orig_dev);
		*pt_prev = NULL;
	}
	return macvlan_handle_frame_hook(port, skb);
}
#else
#define handle_macvlan(skb, pt_prev, ret, orig_dev)	(skb)
#endif

#ifdef CONFIG_NET_CLS_ACT
/* TODO: Maybe we should just force sch_ingress to be compiled in
 * when CONFIG_NET_CLS_ACT is? otherwise some useless instructions
 * a compare and 2 stores extra right now if we dont have it on
 * but have CONFIG_NET_CLS_ACT
 * NOTE: This doesnt stop any functionality; if you dont have
 * the ingress scheduler, you just cant add policies on ingress.
 *
 */
static int ing_filter(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	u32 ttl = G_TC_RTTL(skb->tc_verd);
	struct netdev_queue *rxq;
	int result = TC_ACT_OK;
	struct Qdisc *q;

	if (MAX_RED_LOOP < ttl++) {
		printk(KERN_WARNING
		       "Redir loop detected Dropping packet (%d->%d)\n",
		       skb->skb_iif, dev->ifindex);
		return TC_ACT_SHOT;
	}

	skb->tc_verd = SET_TC_RTTL(skb->tc_verd, ttl);
	skb->tc_verd = SET_TC_AT(skb->tc_verd, AT_INGRESS);

	rxq = &dev->rx_queue;

	q = rxq->qdisc;
	if (q != &noop_qdisc) {
		spin_lock(qdisc_lock(q));
		if (likely(!test_bit(__QDISC_STATE_DEACTIVATED, &q->state)))
			result = qdisc_enqueue_root(skb, q); //ingress_qdisc_ops
		spin_unlock(qdisc_lock(q));
	}

	return result;
}

/*
进入出口流控的函数为dev_queue_xmit(); 如果是入口流控, 数据只是刚从网卡设备中收到, 还未交到网络上层处理, 不过网卡的入口流控不是必须的, 
缺省情况下并不进行流控，进入入口流控函数为ing_filter()函数，该函数被skb_receive_skb()调用。
*///需要编译内核的时候，编译CONFIG_NET_CLS_ACT
static inline struct sk_buff *handle_ing(struct sk_buff *skb,
					 struct packet_type **pt_prev,
					 int *ret, struct net_device *orig_dev)
{
	if (skb->dev->rx_queue.qdisc == &noop_qdisc)
		goto out;

	if (*pt_prev) {
		*ret = deliver_skb(skb, *pt_prev, orig_dev);
		*pt_prev = NULL;
	} else {
		/* Huh? Why does turning on AF_PACKET affect this? */
		skb->tc_verd = SET_TC_OK2MUNGE(skb->tc_verd);
	}

	switch (ing_filter(skb)) {
	case TC_ACT_SHOT:
	case TC_ACT_STOLEN:
		kfree_skb(skb);
		return NULL;
	}

out:
	skb->tc_verd = 0;
	return skb;
}
#endif

/*
 * 	netif_nit_deliver - deliver received packets to network taps
 * 	@skb: buffer
 *
 * 	This function is used to deliver incoming packets to network
 * 	taps. It should be used when the normal netif_receive_skb path
 * 	is bypassed, for example because of VLAN acceleration.
 */
void netif_nit_deliver(struct sk_buff *skb)
{
	struct packet_type *ptype;

	if (list_empty(&ptype_all))
		return;

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb->mac_len = skb->network_header - skb->mac_header;

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, &ptype_all, list) {
		if (!ptype->dev || ptype->dev == skb->dev)
			deliver_skb(skb, ptype, skb->dev);
	}
	rcu_read_unlock();
}

static inline void skb_bond_set_mac_by_master(struct sk_buff *skb,
					      struct net_device *master)
{
	if (skb->pkt_type == PACKET_HOST) {
		u16 *dest = (u16 *) eth_hdr(skb)->h_dest;

		memcpy(dest, master->dev_addr, ETH_ALEN);
	}
}

/* On bonding slaves other than the currently active slave, suppress
 * duplicates except for 802.3ad ETH_P_SLOW, alb non-mcast/bcast, and
 * ARP on active-backup slaves with arp_validate enabled.
 */
int __skb_bond_should_drop(struct sk_buff *skb, struct net_device *master)
{
	struct net_device *dev = skb->dev;

	if (master->priv_flags & IFF_MASTER_ARPMON)
		dev->last_rx = jiffies;

	if ((master->priv_flags & IFF_MASTER_ALB) && master->br_port) {
		/* Do address unmangle. The local destination address
		 * will be always the one master has. Provides the right
		 * functionality in a bridge.
		 */
		skb_bond_set_mac_by_master(skb, master);
	}

	if (dev->priv_flags & IFF_SLAVE_INACTIVE) {
		if ((dev->priv_flags & IFF_SLAVE_NEEDARP) &&
		    skb->protocol == __cpu_to_be16(ETH_P_ARP))
			return 0;

		if (master->priv_flags & IFF_MASTER_ALB) {
			if (skb->pkt_type != PACKET_BROADCAST &&
			    skb->pkt_type != PACKET_MULTICAST)
				return 0;
		}
		if (master->priv_flags & IFF_MASTER_8023AD &&
		    skb->protocol == __cpu_to_be16(ETH_P_SLOW))
			return 0;

		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(__skb_bond_should_drop);

/*
            非NAPI方式                                              NAPI方式(NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。使用参考:网口收发包以及NAPI_huwei_10_新浪博客.htm)

                                        IRQ
                                         |
                  _______________________|_____________________________
                  |                                                     |
             netif_rx                                            napi_schedule
 上半部           |                                                     | 
             enqueue_to_backlog                                  __napi_schedule
                  |                                                     |           
            skb加入input_pkt_queuem中                           napi_struct加入poll_list中
            softnet_data->softnet_data->backlog加入poll_list中                                      | 
                   |____________________________________________________| 
                                             |
                                        net_rx_action
下半部                                       |
                      _______________________|_____________________________
                      |                                                     |
            process_backlog->__netif_receive_skb                驱动poll方法->napi_gro_receive->netif_receive_skb->__netif_receive_skb

*/
static int __netif_receive_skb(struct sk_buff *skb)
{
	struct packet_type *ptype, *pt_prev;
	struct net_device *orig_dev;
	struct net_device *master;
	struct net_device *null_or_orig;
	struct net_device *orig_or_bond;
	int ret = NET_RX_DROP;
	__be16 type;

	if (!netdev_tstamp_prequeue)
		net_timestamp_check(skb);

	if (vlan_tx_tag_present(skb) && vlan_hwaccel_do_receive(skb))
		return NET_RX_SUCCESS;

	/* if we've gotten here through NAPI, check netpoll */
	if (netpoll_receive_skb(skb))
		return NET_RX_DROP;

	if (!skb->skb_iif)
		skb->skb_iif = skb->dev->ifindex;

	/*
	 * bonding note: skbs received on inactive slaves should only
	 * be delivered to pkt handlers that are exact matches.  Also
	 * the deliver_no_wcard flag will be set.  If packet handlers
	 * are sensitive to duplicate packets these skbs will need to
	 * be dropped at the handler.  The vlan accel path may have
	 * already set the deliver_no_wcard flag.
	 */
	null_or_orig = NULL;
	orig_dev = skb->dev;
	master = ACCESS_ONCE(orig_dev->master);
	if (skb->deliver_no_wcard)
		null_or_orig = orig_dev;
	else if (master) {
		if (skb_bond_should_drop(skb, master)) {
			skb->deliver_no_wcard = 1;
			null_or_orig = orig_dev; /* deliver only exact match */
		} else
			skb->dev = master;
	}

	__get_cpu_var(softnet_data).processed++;

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb->mac_len = skb->network_header - skb->mac_header;

	pt_prev = NULL;

	rcu_read_lock();

#ifdef CONFIG_NET_CLS_ACT
	if (skb->tc_verd & TC_NCLS) {
		skb->tc_verd = CLR_TC_NCLS(skb->tc_verd);
		goto ncls;
	}
#endif
    /*    
    po->prot_hook.func = packet_rcv;
    if (sock->type == SOCK_PACKET)
        po->prot_hook.func = packet_rcv_spkt;
    */
	list_for_each_entry_rcu(ptype, &ptype_all, list) {  // net_dev_init dev_add_pack
	    /*
	    注意这里并没有要求ptype->type == type，所以接收到的包
	    只要有注册 ETH_P_ALL 协议，所有的包都会走到 deliver_skb
	    */
		if (ptype->dev == null_or_orig || ptype->dev == skb->dev ||
		    ptype->dev == orig_dev) { //上面的 paket_type.type 为 ETH_P_ALL    
		    if (pt_prev)
		        /* 
		        这是执行上一次遍历中的，所以如果只注册一个ETH_P_ALL
		        的话则pt_prev->func会在这个循环外的 deliver_skb 中执行
		        */
				//此函数最终调用 paket_type.func() 
				// packet_rcv_spkt 或者 packet_rcv
				ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = ptype;
		}
	}

#ifdef CONFIG_NET_CLS_ACT
	skb = handle_ing(skb, &pt_prev, &ret, orig_dev);
	if (!skb)
		goto out;
ncls:
#endif

    /* 
    若编译内核时选上BRIDGE，下面会执行网桥模块
    //调用函数指针 br_handle_frame_hook(skb), 在动态模块 linux_2_6_24/net/bridge/br.c中
       //br_handle_frame_hook = br_handle_frame;
       //所以实际函数 br_handle_frame。
       //注意：在此网桥模块里初始化 skb->pkt_type 为 PACKET_HOST、PACKET_OTHERHOST
       见函数br_init
    */
	skb = handle_bridge(skb, &pt_prev, &ret, orig_dev);
	if (!skb)
		goto out;

	/*
	编译内核时选上MAC_VLAN模块，下面才会执行
	调用 macvlan_handle_frame_hook(skb), 在动态模块linux_2_6_24/drivers/net/macvlan.c中
	macvlan_handle_frame_hook = macvlan_handle_frame; 
	所以实际函数为 macvlan_handle_frame
	此函数里会初始化 skb->pkt_type 为 PACKET_BROADCAST / PACKET_MULTICAST / PACKET_HOST
	*/
	skb = handle_macvlan(skb, &pt_prev, &ret, orig_dev);
	if (!skb)
		goto out;

	/*
	 * Make sure frames received on VLAN interfaces stacked on
	 * bonding interfaces still make their way to any base bonding
	 * device that may have registered for a specific ptype.  The
	 * handler may have to adjust skb->dev and orig_dev.
	 */
	orig_or_bond = orig_dev;
	if ((skb->dev->priv_flags & IFF_802_1Q_VLAN) &&
	    (vlan_dev_real_dev(skb->dev)->priv_flags & IFF_BONDING)) {
		orig_or_bond = vlan_dev_real_dev(skb->dev);
	}
    
    /*
    最后 type = skb->protocol; &ptype_base[ntohs(type)&15]
    处理ptype_base[ntohs(type)&15]上的所有的 packet_type->func()
    根据第二层不同协议来进入不同的钩子函数，重要的有：ip_rcv() arp_rcv()
    ip_recv见inet_init里面的dev_add_pack(&ip_packet_type);
    */
    //skb->protocol用来表示此SKB包含的数据所支持的L3层协议是什么. 
    //如0x0800代表IP，0x0806代表ARP 在驱动程序中已经获取了该值
	type = skb->protocol; 
    
	list_for_each_entry_rcu(ptype,
			&ptype_base[ntohs(type) & PTYPE_HASH_MASK], list) {
		if (ptype->type == type && (ptype->dev == null_or_orig ||
		     ptype->dev == skb->dev || ptype->dev == orig_dev ||
		     ptype->dev == orig_or_bond)) {
			if (pt_prev)
			    // 这是执行上一次遍历中的，所以如果只注册一个ETH_P_ALL的话
			    // 则pt_prev->func会在这个循环外的deliver_skb中执行
				ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = ptype;
		}
	}

	if (pt_prev) {
		ret = pt_prev->func(skb, skb->dev, pt_prev, orig_dev);	// !!!!! ip_rcv
	} else {
		kfree_skb(skb);
		/* Jamal, now you will not able to escape explaining
		 * me how you were going to use this. :-)
		 */
		ret = NET_RX_DROP;
	}

out:
	rcu_read_unlock();
	return ret;
}

/**
 *	netif_receive_skb - process receive buffer from network
 *	@skb: buffer to process
 *
 *	netif_receive_skb() is the main receive data processing function.
 *	It always succeeds. The buffer may be dropped during processing
 *	for congestion control or by the protocol layers.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 *
 *	Return values (usually ignored):
 *	NET_RX_SUCCESS: no congestion
 *	NET_RX_DROP: packet was dropped  进入二层协议处理函数
 */ //    netif_receive_skb是链路层接收数据报的最后一站。它根据注册在全局数组ptype_all和ptype_base里的网络层数据报类型，把数据报递交给不同的网络层协议的接收函数(INET域中主要是ip_rcv和arp_rcv)。
/*
在netif_receive_skb()函数中，可以看出处理的是像ARP、IP这些链路层以上的协议，那么，链路层报头是在哪里去掉的呢？答案是网卡驱动中，
在调用netif_receive_skb()前，
本篇文章来源于 Linux公社网站(www.linuxidc.com)  原文链接：http://www.linuxidc.com/Linux/2011-05/36065.htm
*/
/*
接收数据包的下半部处理流程为：
net_rx_action // 软中断
    |--> process_backlog() // 默认poll
               |--> __netif_receive_skb() // L2处理函数
                            |--> ip_rcv() // L3入口

*/

/*
            非NAPI方式                                NAPI方式
            									(NAPI的napi_struct是自己构造的，
            									该结构上的poll钩子函数也是自己定义的)

                                        IRQ
                                         |
                  _______________________|____________________________	
                  |                                                  |
             netif_rx                                          napi_schedule
 上半部       		  |                                                  | 
             enqueue_to_backlog                               __napi_schedule
                  |                                                  |           
            skb加入input_pkt_queuem中                        napi_struct加入poll_list中
            softnet_data->backlog加入poll_list中                        | 
                   |_________________________________________________| 
                                             |
                                        net_rx_action
下半部                                          |
                      _______________________|_________________
                      |                                       |
            porcess_backlog                驱动poll方法->napi_gro_receive
			->__netif_receive_skb			->netif_receive_skb->__netif_receive_skb
*/

int netif_receive_skb(struct sk_buff *skb)
{
	if (netdev_tstamp_prequeue)
		net_timestamp_check(skb);

#ifdef CONFIG_RPS
	{
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu, ret;

		rcu_read_lock();

		cpu = get_rps_cpu(skb->dev, skb, &rflow);

		if (cpu >= 0) {
			ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			rcu_read_unlock();
		} else {
			rcu_read_unlock();
			ret = __netif_receive_skb(skb);
		}

		return ret;
	}
#else
	return __netif_receive_skb(skb);
#endif
}
EXPORT_SYMBOL(netif_receive_skb);

/* Network device is going away, flush any packets still pending
 * Called with irqs disabled.
 */
static void flush_backlog(void *arg)
{
	struct net_device *dev = arg;
	struct softnet_data *sd = &__get_cpu_var(softnet_data);
	struct sk_buff *skb, *tmp;

	rps_lock(sd);
	skb_queue_walk_safe(&sd->input_pkt_queue, skb, tmp) {
		if (skb->dev == dev) {
			__skb_unlink(skb, &sd->input_pkt_queue);
			kfree_skb(skb);
			input_queue_head_incr(sd);
		}
	}
	rps_unlock(sd);

	skb_queue_walk_safe(&sd->process_queue, skb, tmp) {
		if (skb->dev == dev) {
			__skb_unlink(skb, &sd->process_queue);
			kfree_skb(skb);
			input_queue_head_incr(sd);
		}
	}
}

static int napi_gro_complete(struct sk_buff *skb)
{
	struct packet_type *ptype;
	__be16 type = skb->protocol;
	struct list_head *head = &ptype_base[ntohs(type) & PTYPE_HASH_MASK];
	int err = -ENOENT;

	if (NAPI_GRO_CB(skb)->count == 1) {
		skb_shinfo(skb)->gso_size = 0;
		goto out;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, head, list) {
		if (ptype->type != type || ptype->dev || !ptype->gro_complete)
			continue;

		err = ptype->gro_complete(skb);
		break;
	}
	rcu_read_unlock();

	if (err) {
		WARN_ON(&ptype->list == head);
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

out:
	return netif_receive_skb(skb);
}

static void napi_gro_flush(struct napi_struct *napi)
{
	struct sk_buff *skb, *next;

	for (skb = napi->gro_list; skb; skb = next) {
		next = skb->next;
		skb->next = NULL;
		napi_gro_complete(skb);
	}

	napi->gro_count = 0;
	napi->gro_list = NULL;
}

enum gro_result dev_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	struct sk_buff **pp = NULL;
	struct packet_type *ptype;
	__be16 type = skb->protocol;
	struct list_head *head = &ptype_base[ntohs(type) & PTYPE_HASH_MASK];
	int same_flow;
	int mac_len;
	enum gro_result ret;

	if (!(skb->dev->features & NETIF_F_GRO) || netpoll_rx_on(skb))
		goto normal;

	if (skb_is_gso(skb) || skb_has_frags(skb))
		goto normal;

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, head, list) {
		if (ptype->type != type || ptype->dev || !ptype->gro_receive)
			continue;

		skb_set_network_header(skb, skb_gro_offset(skb));
		mac_len = skb->network_header - skb->mac_header;
		skb->mac_len = mac_len;
		NAPI_GRO_CB(skb)->same_flow = 0;
		NAPI_GRO_CB(skb)->flush = 0;
		NAPI_GRO_CB(skb)->free = 0;

		pp = ptype->gro_receive(&napi->gro_list, skb);
		break;
	}
	rcu_read_unlock();

	if (&ptype->list == head)
		goto normal;

	same_flow = NAPI_GRO_CB(skb)->same_flow;
	ret = NAPI_GRO_CB(skb)->free ? GRO_MERGED_FREE : GRO_MERGED;

	if (pp) {
		struct sk_buff *nskb = *pp;

		*pp = nskb->next;
		nskb->next = NULL;
		napi_gro_complete(nskb);
		napi->gro_count--;
	}

	if (same_flow)
		goto ok;

	if (NAPI_GRO_CB(skb)->flush || napi->gro_count >= MAX_GRO_SKBS)
		goto normal;

	napi->gro_count++;
	NAPI_GRO_CB(skb)->count = 1;
	skb_shinfo(skb)->gso_size = skb_gro_len(skb);
	skb->next = napi->gro_list;
	napi->gro_list = skb;
	ret = GRO_HELD;

pull:
	if (skb_headlen(skb) < skb_gro_offset(skb)) {
		int grow = skb_gro_offset(skb) - skb_headlen(skb);

		BUG_ON(skb->end - skb->tail < grow);

		memcpy(skb_tail_pointer(skb), NAPI_GRO_CB(skb)->frag0, grow);

		skb->tail += grow;
		skb->data_len -= grow;

		skb_shinfo(skb)->frags[0].page_offset += grow;
		skb_shinfo(skb)->frags[0].size -= grow;

		if (unlikely(!skb_shinfo(skb)->frags[0].size)) {
			put_page(skb_shinfo(skb)->frags[0].page);
			memmove(skb_shinfo(skb)->frags,
				skb_shinfo(skb)->frags + 1,
				--skb_shinfo(skb)->nr_frags * sizeof(skb_frag_t));
		}
	}

ok:
	return ret;

normal:
	ret = GRO_NORMAL;
	goto pull;
}
EXPORT_SYMBOL(dev_gro_receive);

static gro_result_t
__napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	struct sk_buff *p;

	for (p = napi->gro_list; p; p = p->next) {
		NAPI_GRO_CB(p)->same_flow =
			(p->dev == skb->dev) &&
			!compare_ether_header(skb_mac_header(p),
					      skb_gro_mac_header(skb));
		NAPI_GRO_CB(p)->flush = 0;
	}

	return dev_gro_receive(napi, skb);
}

gro_result_t napi_skb_finish(gro_result_t ret, struct sk_buff *skb)
{
	switch (ret) {
	case GRO_NORMAL:
		if (netif_receive_skb(skb))
			ret = GRO_DROP;
		break;

	case GRO_DROP:
	case GRO_MERGED_FREE:
		kfree_skb(skb);
		break;

	case GRO_HELD:
	case GRO_MERGED:
		break;
	}

	return ret;
}
EXPORT_SYMBOL(napi_skb_finish);

void skb_gro_reset_offset(struct sk_buff *skb)
{
	NAPI_GRO_CB(skb)->data_offset = 0;
	NAPI_GRO_CB(skb)->frag0 = NULL;
	NAPI_GRO_CB(skb)->frag0_len = 0;

	if (skb->mac_header == skb->tail &&
	    !PageHighMem(skb_shinfo(skb)->frags[0].page)) {
		NAPI_GRO_CB(skb)->frag0 =
			page_address(skb_shinfo(skb)->frags[0].page) +
			skb_shinfo(skb)->frags[0].page_offset;
		NAPI_GRO_CB(skb)->frag0_len = skb_shinfo(skb)->frags[0].size;
	}
}
EXPORT_SYMBOL(skb_gro_reset_offset);
/*
            非NAPI方式                                              NAPI方式

                                        IRQ
                                         |
                  _______________________|_____________________________
                  |                                                     |
             netif_rx                                            napi_schedule
 上半部           |                                                     | 
             enqueue_to_backlog                                  __napi_schedule
                  |                                                     |           
            skb加入input_pkt_queuem中                           napi_struct加入poll_list中
            backlog加入poll_list中                                      | 
                   |____________________________________________________| 
                                             |
                                        net_rx_action
下半部                                       |
                      _______________________|_____________________________
                      |                                                     |
            porcess_backlog->__netif_receive_skb                驱动poll方法->napi_gro_receive->netif_receive_skb->__netif_receive_skb

*/
gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	skb_gro_reset_offset(skb);

	return napi_skb_finish(__napi_gro_receive(napi, skb), skb);
}
EXPORT_SYMBOL(napi_gro_receive);

void napi_reuse_skb(struct napi_struct *napi, struct sk_buff *skb)
{
	__skb_pull(skb, skb_headlen(skb));
	skb_reserve(skb, NET_IP_ALIGN - skb_headroom(skb));
	skb->dev = napi->dev;
	skb->skb_iif = 0;

	napi->skb = skb;
}
EXPORT_SYMBOL(napi_reuse_skb);

struct sk_buff *napi_get_frags(struct napi_struct *napi)
{
	struct sk_buff *skb = napi->skb;

	if (!skb) {
		skb = netdev_alloc_skb_ip_align(napi->dev, GRO_MAX_HEAD);
		if (skb)
			napi->skb = skb;
	}
	return skb;
}
EXPORT_SYMBOL(napi_get_frags);

gro_result_t napi_frags_finish(struct napi_struct *napi, struct sk_buff *skb,
			       gro_result_t ret)
{
	switch (ret) {
	case GRO_NORMAL:
	case GRO_HELD:
		skb->protocol = eth_type_trans(skb, skb->dev);

		if (ret == GRO_HELD)
			skb_gro_pull(skb, -ETH_HLEN);
		else if (netif_receive_skb(skb))
			ret = GRO_DROP;
		break;

	case GRO_DROP:
	case GRO_MERGED_FREE:
		napi_reuse_skb(napi, skb);
		break;

	case GRO_MERGED:
		break;
	}

	return ret;
}
EXPORT_SYMBOL(napi_frags_finish);

struct sk_buff *napi_frags_skb(struct napi_struct *napi)
{
	struct sk_buff *skb = napi->skb;
	struct ethhdr *eth;
	unsigned int hlen;
	unsigned int off;

	napi->skb = NULL;

	skb_reset_mac_header(skb);
	skb_gro_reset_offset(skb);

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*eth);
	eth = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, hlen)) {
		eth = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!eth)) {
			napi_reuse_skb(napi, skb);
			skb = NULL;
			goto out;
		}
	}

	skb_gro_pull(skb, sizeof(*eth));

	/*
	 * This works because the only protocols we care about don't require
	 * special handling.  We'll fix it up properly at the end.
	 */
	skb->protocol = eth->h_proto;

out:
	return skb;
}
EXPORT_SYMBOL(napi_frags_skb);

gro_result_t napi_gro_frags(struct napi_struct *napi)
{
	struct sk_buff *skb = napi_frags_skb(napi);

	if (!skb)
		return GRO_DROP;

	return napi_frags_finish(napi, skb, __napi_gro_receive(napi, skb));
}
EXPORT_SYMBOL(napi_gro_frags);

/*
 * net_rps_action sends any pending IPI's for rps.
 * Note: called with local irq disabled, but exits with local irq enabled.
 */
static void net_rps_action_and_irq_enable(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *remsd = sd->rps_ipi_list;

	if (remsd) {
		sd->rps_ipi_list = NULL;

		local_irq_enable();

		/* Send pending IPI's to kick RPS processing on remote cpus. */
		while (remsd) {
			struct softnet_data *next = remsd->rps_ipi_next;

			if (cpu_online(remsd->cpu))
				__smp_call_function_single(remsd->cpu,
							   &remsd->csd, 0);
			remsd = next;
		}
	} else
#endif
		local_irq_enable();
}
/*
  * process_backlog()在非NAPI方式下，虚拟网络设备的
  * 轮询函数。当虚拟网络设备backlog_dev添加到
  * 网络设备轮询队列后，在数据包输入软中断
  * 中会调用process_backlog()进行数据包的输入。
  * @napi:进行轮询的虚拟的网络设备对应的结构
  * @budget:在数据包输入软中断中，网络设备读取
  *               报文的配额。
  */
/*
接收数据包的下半部处理流程为：
net_rx_action // 软中断
    |--> process_backlog() // 默认poll
               |--> __netif_receive_skb() // L2处理函数
                            |--> ip_rcv() // L3入口

*/
/*
            非NAPI方式                                              NAPI方式(NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。使用参考:网口收发包以及NAPI_huwei_10_新浪博客.htm)

                                        IRQ
                                         |
                  _______________________|_____________________________
                  |                                                     |
             netif_rx                                            napi_schedule
 上半部           |                                                     | 
             enqueue_to_backlog                                  __napi_schedule
                  |                                                     |           
            skb加入input_pkt_queuem中                           napi_struct加入poll_list中
            softnet_data->backlog加入poll_list中                                      | 
                   |____________________________________________________| 
                                             |
                                        net_rx_action
下半部                                       |
                      _______________________|_____________________________
                      |                                                     |
            process_backlog->__netif_receive_skb                驱动poll方法->napi_gro_receive->netif_receive_skb->__netif_receive_skb

*/
//赋值的地方见 net_dev_init, sd->backlog.poll = process_backlog;  
//执行该函数的地方在 net_rx_action(struct softirq_action *h)
static int process_backlog(struct napi_struct *napi, int quota)
{
	int work = 0;
	struct softnet_data *sd = container_of(napi, struct softnet_data, backlog);

#ifdef CONFIG_RPS
	/* Check if we have pending ipi, its better to send them now,
	 * not waiting net_rx_action() end.
	 */
	if (sd->rps_ipi_list) {
		local_irq_disable();
		net_rps_action_and_irq_enable(sd);
	}
#endif
	napi->weight = weight_p;
	local_irq_disable();
	while (work < quota) {
		struct sk_buff *skb;
		unsigned int qlen;

		while ((skb = __skb_dequeue(&sd->process_queue))) { 
			//在下面的skb_queue_splice_tail_init中，被放到了process_queue中
			local_irq_enable();
			  /* 
               * 分析分组类型，以便根据分组
               * 类型将分组传递给网络层的接收函数，
               * 即传递到网络系统的更高一层.为此，
               * 该函数遍历有可能负责当前分组类型的所有
               * 网络层函数，一一调用 deliver_skb
               * 
               * update:
               *   将当前报文传递到上层协议栈
               */
			__netif_receive_skb(skb);
			local_irq_disable();
			input_queue_head_incr(sd);
			if (++work >= quota) {
				local_irq_enable();
				return work;
			}
		}

		rps_lock(sd);
		qlen = skb_queue_len(&sd->input_pkt_queue);
		if (qlen) 
			//把从input_pkt_queue链表中的取出的所有skb信息添加到process_queue中，
			//然后从新初始化input_pkt_queue，见process_backlog
			skb_queue_splice_tail_init(&sd->input_pkt_queue, &sd->process_queue);	
										/* 
										 *把 sd->input_pkt_queue 链表中的节点添加到
										 * sd->process_queue 的尾部。然后初始化
										 * sd->input_pkt_queue 链表 
										 */

		if (qlen < quota - work) {
			/*
			 * Inline a custom version of __napi_complete().
			 * only current cpu owns and manipulates this napi,
			 * and NAPI_STATE_SCHED is the only possible flag set on backlog.
			 * we can use a plain write instead of clear_bit(),
			 * and we dont need an smp_mb() memory barrier.
			 */
			list_del(&napi->poll_list);
			napi->state = 0;

			quota = work + qlen;
		}
		rps_unlock(sd);
	}
	local_irq_enable();

	return work;
}

/**
 * __napi_schedule - schedule for receive
 * @n: entry to schedule
 *
 * The entry's receive function will be scheduled to run
 */
void __napi_schedule(struct napi_struct *n)
{
	unsigned long flags;

	local_irq_save(flags);
	____napi_schedule(&__get_cpu_var(softnet_data), n);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(__napi_schedule);

void __napi_complete(struct napi_struct *n)
{
	BUG_ON(!test_bit(NAPI_STATE_SCHED, &n->state));
	BUG_ON(n->gro_list);

	list_del(&n->poll_list);
	smp_mb__before_clear_bit();
	clear_bit(NAPI_STATE_SCHED, &n->state);
}
EXPORT_SYMBOL(__napi_complete);

void napi_complete(struct napi_struct *n)
{
	unsigned long flags;

	/*
	 * don't let napi dequeue from the cpu poll list
	 * just in case its running on a different cpu
	 */
	if (unlikely(test_bit(NAPI_STATE_NPSVC, &n->state)))
		return;

	napi_gro_flush(n);
	local_irq_save(flags);
	__napi_complete(n);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(napi_complete);

void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
		    int (*poll)(struct napi_struct *, int), int weight)
{
	INIT_LIST_HEAD(&napi->poll_list);
	napi->gro_count = 0;
	napi->gro_list = NULL;
	napi->skb = NULL;
	napi->poll = poll;
	napi->weight = weight;
	list_add(&napi->dev_list, &dev->napi_list);
	napi->dev = dev;
#ifdef CONFIG_NETPOLL
	spin_lock_init(&napi->poll_lock);
	napi->poll_owner = -1;
#endif
	set_bit(NAPI_STATE_SCHED, &napi->state);
}
EXPORT_SYMBOL(netif_napi_add);

void netif_napi_del(struct napi_struct *napi)
{
	struct sk_buff *skb, *next;

	list_del_init(&napi->dev_list);
	napi_free_frags(napi);

	for (skb = napi->gro_list; skb; skb = next) {
		next = skb->next;
		skb->next = NULL;
		kfree_skb(skb);
	}

	napi->gro_list = NULL;
	napi->gro_count = 0;
}
EXPORT_SYMBOL(netif_napi_del);

//报文接收软中断的处理函数net_rx_action详解：
/*
接收数据包的下半部处理流程为：
net_rx_action // 软中断 //net_rx_action中会对包的个数，以及软中断处理时间进行限制
    |--> process_backlog() // 默认poll
               |--> __netif_receive_skb() // L2处理函数
                            |--> ip_rcv() // L3入口

*///open_softirq(NET_RX_SOFTIRQ, net_rx_action);,在net_dev_init中注册该软件中断
/*
//非NAPI方式，从驱动硬件中断中调用这个netif_rx函数，而NAPI方式从硬件中断中调用napi_schedule, 
 //参考 数据包接收系列 — NAPI的原理和实现 http://blog.csdn.net/zhangskd/article/details/21627963
 //不管NAPI还是非NAPI最终都调用net_rx_action
*/

/*
            非NAPI方式                                              NAPI方式(NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。使用参考:网口收发包以及NAPI_huwei_10_新浪博客.htm)

                                        IRQ
                                         |
                  _______________________|_____________________________
                  |                                                     |
             netif_rx                                            napi_schedule
 上半部           |                                                     | 
             enqueue_to_backlog                                  __napi_schedule
                  |                                                     |           
            skb加入input_pkt_queuem中                           napi_struct加入poll_list中
            softnet_data->backlog加入poll_list中                                      | 
                   |____________________________________________________| 
                                             |
                                        net_rx_action
下半部                                       |
                      _______________________|_____________________________
                      |                                                     |
            process_backlog->__netif_receive_skb                驱动poll方法->napi_gro_receive->netif_receive_skb->__netif_receive_skb

*/
//接收过程哪些函数处于上半部，哪些函数处于下半部，
//参考 数据包接收系列 — NAPI的原理和实现 
//http://blog.csdn.net/zhangskd/article/details/21627963
static void net_rx_action(struct softirq_action *h) 
{
	struct softnet_data *sd = &__get_cpu_var(softnet_data);
	unsigned long time_limit = jiffies + 2;  /*设置软中断处理程序一次允许的最大执行时间为2个jiffies*/
	int budget = netdev_budget; /*设置软中断接收函数一次最多处理的报文个数为 300 */
	void *have;

	local_irq_disable();

    /*  
    NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。
    非NAPI的napi_struct结构是默认的，也就是per cpu的softnet_data>backlog，
    起poll钩子函数为 process_backlog
    */
	while (!list_empty(&sd->poll_list)) {
		struct napi_struct *n;
		int work, weight;

		/* If softirq window is exhuasted then punt.
		 * Allow this to run for 2 jiffies since which will allow
		 * an average latency of 1.5/HZ.
		 */

		 /*
         如果处理报文超出一次处理最大的个数 或 允许时间超过最大时间
         就停止执行,   跳到 softnet_break 处
		 */
		if (unlikely(budget <= 0 || time_after(jiffies, time_limit)))
			goto softnet_break;
        
        /*
        使能本地中断，上面判断list为空已完成，
        下面调用NAPI的轮询函数是在硬中断开启的情况下执行
        */
		local_irq_enable();

		/* Even though interrupts have been re-enabled, this
		 * access is safe because interrupts can only add new
		 * entries to the tail of this list, and only ->poll()
		 * calls can remove this head entry from the list.
		 */
    
        /* 
        取得softnet_data pool_list 链表上的一个napi,        
        即使现在硬中断抢占软中断，会把一个napi挂到pool_list的尾端            
        软中断只会从pool_list 头部移除一个pool_list，这样不存在临界区
        */
		n = list_first_entry(&sd->poll_list, struct napi_struct, poll_list);

		have = netpoll_poll_lock(n);

		weight = n->weight;  /*用weighe 记录napi 一次轮询允许处理的最大报文数*/

		/* This NAPI_STATE_SCHED test is for avoiding a race
		 * with netpoll's poll_napi().  Only the entity which
		 * obtains the lock and sees NAPI_STATE_SCHED set will
		 * actually make the ->poll() call.  Therefore we avoid
		 * accidently calling ->poll() when NAPI is not scheduled.
		 */
		work = 0;  /* work 记录一个napi总共处理的报文数*/
		
		if (test_bit(NAPI_STATE_SCHED, &n->state)) {/*如果取得的napi状态是被调度的，就执行napi的轮询处理函数*/
            /*  
            NAPI的napi_struct是自己构造的，该结构上的poll钩子函数也是自己定义的。
            非NAPI的napi_struct结构是默认的，也就是per cpu的softnet_data>backlog，
            起poll钩子函数为process_backlog
            */
			work = n->poll(n, weight);	// process_backlog
			trace_napi_poll(n);
		}

		WARN_ON_ONCE(work > weight);

		budget -= work;  /*预算减去已经处理的报文数*/
            
        /*
        禁止本地CPU 的中断，下面会有把没执行完的NAPI挂到softnet_data      尾部的操作，
        和硬中断存在临界区。同时while循环时判断list是否为空时也要禁止硬中断抢占
        */
		local_irq_disable();

		/* Drivers must not modify the NAPI state if they
		 * consume the entire weight.  In such cases this code
		 * still "owns" the NAPI instance and therefore can
		 * move the instance around on the list at-will.
		 */
            
        /*
        如果napi 一次轮询处理的报文数正好等于允许处理的最大数,
        说明一次轮询没处理完全部需要处理的报文
        */
		if (unlikely(work == weight)) {
			if (unlikely(napi_disable_pending(n))) { 
				/*如果napi已经被禁用，就把napi 从 softnet_data 的pool_list 上移除*/
				local_irq_enable();
				napi_complete(n);
				local_irq_disable();
			} else  /*否则，把napi 移到 pool_list 的尾端*/
				list_move_tail(&n->poll_list, &sd->poll_list);
		}

		netpoll_poll_unlock(have);
	}


/*
如果处理时间超时，或处理的报文数到了最多允许处理的个数，
说明还有napi 上有报文需要处理，调度软中断。否则，
说明这次软中断处理完全部的napi上的需要处理的报文，不再需要调度软中断了
*/

out:
	net_rps_action_and_irq_enable(sd);

#ifdef CONFIG_NET_DMA
	/*
	 * There may not be any more sk_buffs coming right now, so push
	 * any pending DMA copies to hardware
	 */
	dma_issue_pending_all();
#endif

	return;

softnet_break: //读取时间到或者从一个napi中读取的报文数达到最大值
	sd->time_squeeze++;
	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
	goto out;
}

static gifconf_func_t *gifconf_list[NPROTO];

/**
 *	register_gifconf	-	register a SIOCGIF handler
 *	@family: Address family
 *	@gifconf: Function handler
 *
 *	Register protocol dependent address dumping routines. The handler
 *	that is passed must not be freed or reused until it has been replaced
 *	by another handler.
 */
int register_gifconf(unsigned int family, gifconf_func_t *gifconf)
{
	if (family >= NPROTO)
		return -EINVAL;
	gifconf_list[family] = gifconf;
	return 0;
}
EXPORT_SYMBOL(register_gifconf);


/*
 *	Map an interface index to its name (SIOCGIFNAME)
 */

/*
 *	We need this ioctl for efficient implementation of the
 *	if_indextoname() function required by the IPv6 API.  Without
 *	it, we would have to search all the interfaces to find a
 *	match.  --pb
 */

static int dev_ifname(struct net *net, struct ifreq __user *arg)
{
	struct net_device *dev;
	struct ifreq ifr;

	/*
	 *	Fetch the caller's info block.
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, ifr.ifr_ifindex);
	if (!dev) {
		rcu_read_unlock();
		return -ENODEV;
	}

	strcpy(ifr.ifr_name, dev->name);
	rcu_read_unlock();

	if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		return -EFAULT;
	return 0;
}

/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size eventually, and there is nothing I can do about it.
 *	Thus we will need a 'compatibility mode'.
 */

static int dev_ifconf(struct net *net, char __user *arg)
{
	struct ifconf ifc;
	struct net_device *dev;
	char __user *pos;
	int len;
	int total;
	int i;

	/*
	 *	Fetch the caller's info block.
	 */

	if (copy_from_user(&ifc, arg, sizeof(struct ifconf)))
		return -EFAULT;

	pos = ifc.ifc_buf;
	len = ifc.ifc_len;

	/*
	 *	Loop over the interfaces, and write an info block for each.
	 */

	total = 0;
	for_each_netdev(net, dev) {
		for (i = 0; i < NPROTO; i++) {
			if (gifconf_list[i]) {
				int done;
				if (!pos)
					done = gifconf_list[i](dev, NULL, 0);
				else
					done = gifconf_list[i](dev, pos + total,
							       len - total);
				if (done < 0)
					return -EFAULT;
				total += done;
			}
		}
	}

	/*
	 *	All done.  Write the updated control block back to the caller.
	 */
	ifc.ifc_len = total;

	/*
	 * 	Both BSD and Solaris return 0 here, so we do too.
	 */
	return copy_to_user(arg, &ifc, sizeof(struct ifconf)) ? -EFAULT : 0;
}

#ifdef CONFIG_PROC_FS
/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */
void *dev_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct net *net = seq_file_net(seq);
	loff_t off;
	struct net_device *dev;

	rcu_read_lock();
	if (!*pos)
		return SEQ_START_TOKEN;

	off = 1;
	for_each_netdev_rcu(net, dev)
		if (off++ == *pos)
			return dev;

	return NULL;
}

void *dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net_device *dev = (v == SEQ_START_TOKEN) ?
				  first_net_device(seq_file_net(seq)) :
				  next_net_device((struct net_device *)v);

	++*pos;
	return rcu_dereference(dev);
}

void dev_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static void dev_seq_printf_stats(struct seq_file *seq, struct net_device *dev)
{
	const struct net_device_stats *stats = dev_get_stats(dev);

	seq_printf(seq, "%6s: %7lu %7lu %4lu %4lu %4lu %5lu %10lu %9lu "
		   "%8lu %7lu %4lu %4lu %4lu %5lu %7lu %10lu\n",
		   dev->name, stats->rx_bytes, stats->rx_packets,
		   stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors +
		    stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->rx_compressed, stats->multicast,
		   stats->tx_bytes, stats->tx_packets,
		   stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors +
		    stats->tx_aborted_errors +
		    stats->tx_window_errors +
		    stats->tx_heartbeat_errors,
		   stats->tx_compressed);
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized
 *	/proc/net interface to create /proc/net/dev
 */
static int dev_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Inter-|   Receive                            "
			      "                    |  Transmit\n"
			      " face |bytes    packets errs drop fifo frame "
			      "compressed multicast|bytes    packets errs "
			      "drop fifo colls carrier compressed\n");
	else
		dev_seq_printf_stats(seq, v);
	return 0;
}

static struct softnet_data *softnet_get_online(loff_t *pos)
{
	struct softnet_data *sd = NULL;

	while (*pos < nr_cpu_ids)
		if (cpu_online(*pos)) {
			sd = &per_cpu(softnet_data, *pos);
			break;
		} else
			++*pos;
	return sd;
}

static void *softnet_seq_start(struct seq_file *seq, loff_t *pos)
{
	return softnet_get_online(pos);
}

static void *softnet_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return softnet_get_online(pos);
}

static void softnet_seq_stop(struct seq_file *seq, void *v)
{
}

static int softnet_seq_show(struct seq_file *seq, void *v)
{
	struct softnet_data *sd = v;

	seq_printf(seq, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		   sd->processed, sd->dropped, sd->time_squeeze, 0,
		   0, 0, 0, 0, /* was fastroute */
		   sd->cpu_collision, sd->received_rps);
	return 0;
}

static const struct seq_operations dev_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = dev_seq_show,
};

static int dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &dev_seq_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations dev_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = dev_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};

static const struct seq_operations softnet_seq_ops = {
	.start = softnet_seq_start,
	.next  = softnet_seq_next,
	.stop  = softnet_seq_stop,
	.show  = softnet_seq_show,
};

static int softnet_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &softnet_seq_ops);
}

static const struct file_operations softnet_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = softnet_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static void *ptype_get_idx(loff_t pos)
{
	struct packet_type *pt = NULL;
	loff_t i = 0;
	int t;

	list_for_each_entry_rcu(pt, &ptype_all, list) {
		if (i == pos)
			return pt;
		++i;
	}

	for (t = 0; t < PTYPE_HASH_SIZE; t++) {
		list_for_each_entry_rcu(pt, &ptype_base[t], list) {
			if (i == pos)
				return pt;
			++i;
		}
	}
	return NULL;
}

static void *ptype_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return *pos ? ptype_get_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *ptype_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct packet_type *pt;
	struct list_head *nxt;
	int hash;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ptype_get_idx(0);

	pt = v;
	nxt = pt->list.next;
	if (pt->type == htons(ETH_P_ALL)) {
		if (nxt != &ptype_all)
			goto found;
		hash = 0;
		nxt = ptype_base[0].next;
	} else
		hash = ntohs(pt->type) & PTYPE_HASH_MASK;

	while (nxt == &ptype_base[hash]) {
		if (++hash >= PTYPE_HASH_SIZE)
			return NULL;
		nxt = ptype_base[hash].next;
	}
found:
	return list_entry(nxt, struct packet_type, list);
}

static void ptype_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int ptype_seq_show(struct seq_file *seq, void *v)
{
	struct packet_type *pt = v;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Type Device      Function\n");
	else if (pt->dev == NULL || dev_net(pt->dev) == seq_file_net(seq)) {
		if (pt->type == htons(ETH_P_ALL))
			seq_puts(seq, "ALL ");
		else
			seq_printf(seq, "%04x", ntohs(pt->type));

		seq_printf(seq, " %-8s %pF\n",
			   pt->dev ? pt->dev->name : "", pt->func);
	}

	return 0;
}

static const struct seq_operations ptype_seq_ops = {
	.start = ptype_seq_start,
	.next  = ptype_seq_next,
	.stop  = ptype_seq_stop,
	.show  = ptype_seq_show,
};

static int ptype_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &ptype_seq_ops,
			sizeof(struct seq_net_private));
}

static const struct file_operations ptype_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = ptype_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};


static int __net_init dev_proc_net_init(struct net *net)
{
	int rc = -ENOMEM;

	if (!proc_net_fops_create(net, "dev", S_IRUGO, &dev_seq_fops))
		goto out;
	if (!proc_net_fops_create(net, "softnet_stat", S_IRUGO, &softnet_seq_fops))
		goto out_dev;
	if (!proc_net_fops_create(net, "ptype", S_IRUGO, &ptype_seq_fops))
		goto out_softnet;

	if (wext_proc_init(net))
		goto out_ptype;
	rc = 0;
out:
	return rc;
out_ptype:
	proc_net_remove(net, "ptype");
out_softnet:
	proc_net_remove(net, "softnet_stat");
out_dev:
	proc_net_remove(net, "dev");
	goto out;
}

static void __net_exit dev_proc_net_exit(struct net *net)
{
	wext_proc_exit(net);

	proc_net_remove(net, "ptype");
	proc_net_remove(net, "softnet_stat");
	proc_net_remove(net, "dev");
}

static struct pernet_operations __net_initdata dev_proc_ops = {
	.init = dev_proc_net_init,
	.exit = dev_proc_net_exit,
};

static int __init dev_proc_init(void)
{
	return register_pernet_subsys(&dev_proc_ops);
}
#else
#define dev_proc_init() 0
#endif	/* CONFIG_PROC_FS */


/**
 *	netdev_set_master	-	set up master/slave pair
 *	@slave: slave device
 *	@master: new master device
 *
 *	Changes the master device of the slave. Pass %NULL to break the
 *	bonding. The caller must hold the RTNL semaphore. On a failure
 *	a negative errno code is returned. On success the reference counts
 *	are adjusted, %RTM_NEWLINK is sent to the routing socket and the
 *	function returns zero.
 */
int netdev_set_master(struct net_device *slave, struct net_device *master)
{
	struct net_device *old = slave->master;

	ASSERT_RTNL();

	if (master) {
		if (old)
			return -EBUSY;
		dev_hold(master);
	}

	slave->master = master;

	if (old) {
		synchronize_net();
		dev_put(old);
	}
	if (master)
		slave->flags |= IFF_SLAVE;
	else
		slave->flags &= ~IFF_SLAVE;

	rtmsg_ifinfo(RTM_NEWLINK, slave, IFF_SLAVE);
	return 0;
}
EXPORT_SYMBOL(netdev_set_master);

static void dev_change_rx_flags(struct net_device *dev, int flags)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if ((dev->flags & IFF_UP) && ops->ndo_change_rx_flags)
		ops->ndo_change_rx_flags(dev, flags);
}

static int __dev_set_promiscuity(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;
	uid_t uid;
	gid_t gid;

	ASSERT_RTNL();

	dev->flags |= IFF_PROMISC;
	dev->promiscuity += inc;
	if (dev->promiscuity == 0) {
		/*
		 * Avoid overflow.
		 * If inc causes overflow, untouch promisc and return error.
		 */
		if (inc < 0)
			dev->flags &= ~IFF_PROMISC;
		else {
			dev->promiscuity -= inc;
			printk(KERN_WARNING "%s: promiscuity touches roof, "
				"set promiscuity failed, promiscuity feature "
				"of device might be broken.\n", dev->name);
			return -EOVERFLOW;
		}
	}
	if (dev->flags != old_flags) {
		printk(KERN_INFO "device %s %s promiscuous mode\n",
		       dev->name, (dev->flags & IFF_PROMISC) ? "entered" :
							       "left");
		if (audit_enabled) {
			current_uid_gid(&uid, &gid);
			audit_log(current->audit_context, GFP_ATOMIC,
				AUDIT_ANOM_PROMISCUOUS,
				"dev=%s prom=%d old_prom=%d auid=%u uid=%u gid=%u ses=%u",
				dev->name, (dev->flags & IFF_PROMISC),
				(old_flags & IFF_PROMISC),
				audit_get_loginuid(current),
				uid, gid,
				audit_get_sessionid(current));
		}

		dev_change_rx_flags(dev, IFF_PROMISC);
	}
	return 0;
}

/**
 *	dev_set_promiscuity	- update promiscuity count on a device
 *	@dev: device
 *	@inc: modifier
 *
 *	Add or remove promiscuity from a device. While the count in the device
 *	remains above zero the interface remains promiscuous. Once it hits zero
 *	the device reverts back to normal filtering operation. A negative inc
 *	value is used to drop promiscuity on the device.
 *	Return 0 if successful or a negative errno code on error.
 */
int dev_set_promiscuity(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;
	int err;

	err = __dev_set_promiscuity(dev, inc);
	if (err < 0)
		return err;
	if (dev->flags != old_flags)
		dev_set_rx_mode(dev);
	return err;
}
EXPORT_SYMBOL(dev_set_promiscuity);

/**
 *	dev_set_allmulti	- update allmulti count on a device
 *	@dev: device
 *	@inc: modifier
 *
 *	Add or remove reception of all multicast frames to a device. While the
 *	count in the device remains above zero the interface remains listening
 *	to all interfaces. Once it hits zero the device reverts back to normal
 *	filtering operation. A negative @inc value is used to drop the counter
 *	when releasing a resource needing all multicasts.
 *	Return 0 if successful or a negative errno code on error.
 */

int dev_set_allmulti(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	ASSERT_RTNL();

	dev->flags |= IFF_ALLMULTI;
	dev->allmulti += inc;
	if (dev->allmulti == 0) {
		/*
		 * Avoid overflow.
		 * If inc causes overflow, untouch allmulti and return error.
		 */
		if (inc < 0)
			dev->flags &= ~IFF_ALLMULTI;
		else {
			dev->allmulti -= inc;
			printk(KERN_WARNING "%s: allmulti touches roof, "
				"set allmulti failed, allmulti feature of "
				"device might be broken.\n", dev->name);
			return -EOVERFLOW;
		}
	}
	if (dev->flags ^ old_flags) {
		dev_change_rx_flags(dev, IFF_ALLMULTI);
		dev_set_rx_mode(dev);
	}
	return 0;
}
EXPORT_SYMBOL(dev_set_allmulti);

/*
 *	Upload unicast and multicast address lists to device and
 *	configure RX filtering. When the device doesn't support unicast
 *	filtering it is put in promiscuous mode while unicast addresses
 *	are present.
 */
void __dev_set_rx_mode(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	/* dev_open will call this function so the list will stay sane. */
	if (!(dev->flags&IFF_UP))
		return;

	if (!netif_device_present(dev))
		return;

	if (ops->ndo_set_rx_mode)
		ops->ndo_set_rx_mode(dev);
	else {
		/* Unicast addresses changes may only happen under the rtnl,
		 * therefore calling __dev_set_promiscuity here is safe.
		 */
		if (!netdev_uc_empty(dev) && !dev->uc_promisc) {
			__dev_set_promiscuity(dev, 1);
			dev->uc_promisc = 1;
		} else if (netdev_uc_empty(dev) && dev->uc_promisc) {
			__dev_set_promiscuity(dev, -1);
			dev->uc_promisc = 0;
		}

		if (ops->ndo_set_multicast_list)
			ops->ndo_set_multicast_list(dev);
	}
}

void dev_set_rx_mode(struct net_device *dev)
{
	netif_addr_lock_bh(dev);
	__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
}

/**
 *	dev_get_flags - get flags reported to userspace
 *	@dev: device
 *
 *	Get the combination of flag bits exported through APIs to userspace.
 */
unsigned dev_get_flags(const struct net_device *dev)
{
	unsigned flags;

	flags = (dev->flags & ~(IFF_PROMISC |
				IFF_ALLMULTI |
				IFF_RUNNING |
				IFF_LOWER_UP |
				IFF_DORMANT)) |
		(dev->gflags & (IFF_PROMISC |
				IFF_ALLMULTI));

	if (netif_running(dev)) {
		if (netif_oper_up(dev))
			flags |= IFF_RUNNING;
		if (netif_carrier_ok(dev))
			flags |= IFF_LOWER_UP;
		if (netif_dormant(dev))
			flags |= IFF_DORMANT;
	}

	return flags;
}
EXPORT_SYMBOL(dev_get_flags);

int __dev_change_flags(struct net_device *dev, unsigned int flags)
{
	int old_flags = dev->flags;
	int ret;

	ASSERT_RTNL();

	/*
	 *	Set the flags on our device.
	 */

	dev->flags = (flags & (IFF_DEBUG | IFF_NOTRAILERS | IFF_NOARP |
			       IFF_DYNAMIC | IFF_MULTICAST | IFF_PORTSEL |
			       IFF_AUTOMEDIA)) |
		     (dev->flags & (IFF_UP | IFF_VOLATILE | IFF_PROMISC |
				    IFF_ALLMULTI));

	/*
	 *	Load in the correct multicast list now the flags have changed.
	 */

	if ((old_flags ^ flags) & IFF_MULTICAST)
		dev_change_rx_flags(dev, IFF_MULTICAST);

	dev_set_rx_mode(dev);

	/*
	 *	Have we downed the interface. We handle IFF_UP ourselves
	 *	according to user attempts to set it, rather than blindly
	 *	setting it.
	 */

	ret = 0;
	if ((old_flags ^ flags) & IFF_UP) {	/* Bit is different  ? */
		ret = ((old_flags & IFF_UP) ? __dev_close : __dev_open)(dev);

		if (!ret)
			dev_set_rx_mode(dev);
	}

	if ((flags ^ dev->gflags) & IFF_PROMISC) {
		int inc = (flags & IFF_PROMISC) ? 1 : -1;

		dev->gflags ^= IFF_PROMISC;
		dev_set_promiscuity(dev, inc);
	}

	/* NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
	   is important. Some (broken) drivers set IFF_PROMISC, when
	   IFF_ALLMULTI is requested not asking us and not reporting.
	 */
	if ((flags ^ dev->gflags) & IFF_ALLMULTI) {
		int inc = (flags & IFF_ALLMULTI) ? 1 : -1;

		dev->gflags ^= IFF_ALLMULTI;
		dev_set_allmulti(dev, inc);
	}

	return ret;
}

void __dev_notify_flags(struct net_device *dev, unsigned int old_flags)
{
	unsigned int changes = dev->flags ^ old_flags;

	if (changes & IFF_UP) {
		if (dev->flags & IFF_UP)
			call_netdevice_notifiers(NETDEV_UP, dev);
		else
			call_netdevice_notifiers(NETDEV_DOWN, dev);
	}

	if (dev->flags & IFF_UP &&
	    (changes & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI | IFF_VOLATILE)))
		call_netdevice_notifiers(NETDEV_CHANGE, dev);
}

/**
 *	dev_change_flags - change device settings
 *	@dev: device
 *	@flags: device state flags
 *
 *	Change settings on device based state flags. The flags are
 *	in the userspace exported format.
 */
int dev_change_flags(struct net_device *dev, unsigned flags)
{
	int ret, changes;
	int old_flags = dev->flags;

	ret = __dev_change_flags(dev, flags);
	if (ret < 0)
		return ret;

	changes = old_flags ^ dev->flags;
	if (changes)
		rtmsg_ifinfo(RTM_NEWLINK, dev, changes);

	__dev_notify_flags(dev, old_flags);
	return ret;
}
EXPORT_SYMBOL(dev_change_flags);

/**
 *	dev_set_mtu - Change maximum transfer unit
 *	@dev: device
 *	@new_mtu: new transfer unit
 *
 *	Change the maximum transfer size of the network device.
 */
int dev_set_mtu(struct net_device *dev, int new_mtu)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (new_mtu == dev->mtu)
		return 0;

	/*	MTU must be positive.	 */
	if (new_mtu < 0)
		return -EINVAL;

	if (!netif_device_present(dev))
		return -ENODEV;

	err = 0;
	if (ops->ndo_change_mtu)
		err = ops->ndo_change_mtu(dev, new_mtu);
	else
		dev->mtu = new_mtu;

	if (!err && dev->flags & IFF_UP)
		call_netdevice_notifiers(NETDEV_CHANGEMTU, dev);
	return err;
}
EXPORT_SYMBOL(dev_set_mtu);

/**
 *	dev_set_mac_address - Change Media Access Control Address
 *	@dev: device
 *	@sa: new address
 *
 *	Change the hardware (MAC) address of the device
 */
int dev_set_mac_address(struct net_device *dev, struct sockaddr *sa)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (!ops->ndo_set_mac_address)
		return -EOPNOTSUPP;
	if (sa->sa_family != dev->type)
		return -EINVAL;
	if (!netif_device_present(dev))
		return -ENODEV;
	err = ops->ndo_set_mac_address(dev, sa);
	if (!err)
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	return err;
}
EXPORT_SYMBOL(dev_set_mac_address);

/*
 *	Perform the SIOCxIFxxx calls, inside rcu_read_lock()
 */
static int dev_ifsioc_locked(struct net *net, struct ifreq *ifr, unsigned int cmd)
{
	int err;
	struct net_device *dev = dev_get_by_name_rcu(net, ifr->ifr_name);

	if (!dev)
		return -ENODEV;

	switch (cmd) {
	case SIOCGIFFLAGS:	/* Get interface flags */
		ifr->ifr_flags = (short) dev_get_flags(dev);
		return 0;

	case SIOCGIFMETRIC:	/* Get the metric on the interface
				   (currently unused) */
		ifr->ifr_metric = 0;
		return 0;

	case SIOCGIFMTU:	/* Get the MTU of a device */
		ifr->ifr_mtu = dev->mtu;
		return 0;

	case SIOCGIFHWADDR:
		if (!dev->addr_len)
			memset(ifr->ifr_hwaddr.sa_data, 0, sizeof ifr->ifr_hwaddr.sa_data);
		else
			memcpy(ifr->ifr_hwaddr.sa_data, dev->dev_addr,
			       min(sizeof ifr->ifr_hwaddr.sa_data, (size_t) dev->addr_len));
		ifr->ifr_hwaddr.sa_family = dev->type;
		return 0;

	case SIOCGIFSLAVE:
		err = -EINVAL;
		break;

	case SIOCGIFMAP:
		ifr->ifr_map.mem_start = dev->mem_start;
		ifr->ifr_map.mem_end   = dev->mem_end;
		ifr->ifr_map.base_addr = dev->base_addr;
		ifr->ifr_map.irq       = dev->irq;
		ifr->ifr_map.dma       = dev->dma;
		ifr->ifr_map.port      = dev->if_port;
		return 0;

	case SIOCGIFINDEX:
		ifr->ifr_ifindex = dev->ifindex;
		return 0;

	case SIOCGIFTXQLEN:
		ifr->ifr_qlen = dev->tx_queue_len;
		return 0;

	default:
		/* dev_ioctl() should ensure this case
		 * is never reached
		 */
		WARN_ON(1);
		err = -EINVAL;
		break;

	}
	return err;
}

/*
 *	Perform the SIOCxIFxxx calls, inside rtnl_lock()
 */
static int dev_ifsioc(struct net *net, struct ifreq *ifr, unsigned int cmd)
{
	int err;
	struct net_device *dev = __dev_get_by_name(net, ifr->ifr_name);
	const struct net_device_ops *ops;

	if (!dev)
		return -ENODEV;

	ops = dev->netdev_ops;

	switch (cmd) {
	case SIOCSIFFLAGS:	/* Set interface flags */
		return dev_change_flags(dev, ifr->ifr_flags);

	case SIOCSIFMETRIC:	/* Set the metric on the interface
				   (currently unused) */
		return -EOPNOTSUPP;

	case SIOCSIFMTU:	/* Set the MTU of a device */
		return dev_set_mtu(dev, ifr->ifr_mtu);

	case SIOCSIFHWADDR:
		return dev_set_mac_address(dev, &ifr->ifr_hwaddr);

	case SIOCSIFHWBROADCAST:
		if (ifr->ifr_hwaddr.sa_family != dev->type)
			return -EINVAL;
		memcpy(dev->broadcast, ifr->ifr_hwaddr.sa_data,
		       min(sizeof ifr->ifr_hwaddr.sa_data, (size_t) dev->addr_len));
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
		return 0;

	case SIOCSIFMAP:
		if (ops->ndo_set_config) {
			if (!netif_device_present(dev))
				return -ENODEV;
			return ops->ndo_set_config(dev, &ifr->ifr_map);
		}
		return -EOPNOTSUPP;

	case SIOCADDMULTI:
		if ((!ops->ndo_set_multicast_list && !ops->ndo_set_rx_mode) ||
		    ifr->ifr_hwaddr.sa_family != AF_UNSPEC)
			return -EINVAL;
		if (!netif_device_present(dev))
			return -ENODEV;
		return dev_mc_add_global(dev, ifr->ifr_hwaddr.sa_data);

	case SIOCDELMULTI:
		if ((!ops->ndo_set_multicast_list && !ops->ndo_set_rx_mode) ||
		    ifr->ifr_hwaddr.sa_family != AF_UNSPEC)
			return -EINVAL;
		if (!netif_device_present(dev))
			return -ENODEV;
		return dev_mc_del_global(dev, ifr->ifr_hwaddr.sa_data);

	case SIOCSIFTXQLEN:
		if (ifr->ifr_qlen < 0)
			return -EINVAL;
		dev->tx_queue_len = ifr->ifr_qlen;
		return 0;

	case SIOCSIFNAME:
		ifr->ifr_newname[IFNAMSIZ-1] = '\0';
		return dev_change_name(dev, ifr->ifr_newname);

	/*
	 *	Unknown or private ioctl
	 */
	default:
		if ((cmd >= SIOCDEVPRIVATE &&
		    cmd <= SIOCDEVPRIVATE + 15) ||
		    cmd == SIOCBONDENSLAVE ||
		    cmd == SIOCBONDRELEASE ||
		    cmd == SIOCBONDSETHWADDR ||
		    cmd == SIOCBONDSLAVEINFOQUERY ||
		    cmd == SIOCBONDINFOQUERY ||
		    cmd == SIOCBONDCHANGEACTIVE ||
		    cmd == SIOCGMIIPHY ||
		    cmd == SIOCGMIIREG ||
		    cmd == SIOCSMIIREG ||
		    cmd == SIOCBRADDIF ||
		    cmd == SIOCBRDELIF ||
		    cmd == SIOCSHWTSTAMP ||
		    cmd == SIOCWANDEV) {
			err = -EOPNOTSUPP;
			if (ops->ndo_do_ioctl) {
				if (netif_device_present(dev))
					err = ops->ndo_do_ioctl(dev, ifr, cmd);
				else
					err = -ENODEV;
			}
		} else
			err = -EINVAL;

	}
	return err;
}

/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

/**
 *	dev_ioctl	-	network device ioctl
 *	@net: the applicable net namespace
 *	@cmd: command to issue
 *	@arg: pointer to a struct ifreq in user space
 *
 *	Issue ioctl functions to devices. This is normally called by the
 *	user space syscall interfaces but can sometimes be useful for
 *	other purposes. The return value is the return from the syscall if
 *	positive or a negative errno code on error.
 */

int dev_ioctl(struct net *net, unsigned int cmd, void __user *arg)
{
	struct ifreq ifr;
	int ret;
	char *colon;

	/* One special case: SIOCGIFCONF takes ifconf argument
	   and requires shared lock, because it sleeps writing
	   to user space.
	 */

	if (cmd == SIOCGIFCONF) {
		rtnl_lock();
		ret = dev_ifconf(net, (char __user *) arg);
		rtnl_unlock();
		return ret;
	}
	if (cmd == SIOCGIFNAME)
		return dev_ifname(net, (struct ifreq __user *)arg);

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	ifr.ifr_name[IFNAMSIZ-1] = 0;

	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;

	/*
	 *	See which interface the caller is talking about.
	 */

	switch (cmd) {
	/*
	 *	These ioctl calls:
	 *	- can be done by all.
	 *	- atomic and do not require locking.
	 *	- return a value
	 */
	case SIOCGIFFLAGS:
	case SIOCGIFMETRIC:
	case SIOCGIFMTU:
	case SIOCGIFHWADDR:
	case SIOCGIFSLAVE:
	case SIOCGIFMAP:
	case SIOCGIFINDEX:
	case SIOCGIFTXQLEN:
		dev_load(net, ifr.ifr_name);
		rcu_read_lock();
		ret = dev_ifsioc_locked(net, &ifr, cmd);
		rcu_read_unlock();
		if (!ret) {
			if (colon)
				*colon = ':';
			if (copy_to_user(arg, &ifr,
					 sizeof(struct ifreq)))
				ret = -EFAULT;
		}
		return ret;

	case SIOCETHTOOL:
		dev_load(net, ifr.ifr_name);
		rtnl_lock();
		ret = dev_ethtool(net, &ifr);
		rtnl_unlock();
		if (!ret) {
			if (colon)
				*colon = ':';
			if (copy_to_user(arg, &ifr,
					 sizeof(struct ifreq)))
				ret = -EFAULT;
		}
		return ret;

	/*
	 *	These ioctl calls:
	 *	- require superuser power.
	 *	- require strict serialization.
	 *	- return a value
	 */
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSIFNAME:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		dev_load(net, ifr.ifr_name);
		rtnl_lock();
		ret = dev_ifsioc(net, &ifr, cmd);
		rtnl_unlock();
		if (!ret) {
			if (colon)
				*colon = ':';
			if (copy_to_user(arg, &ifr,
					 sizeof(struct ifreq)))
				ret = -EFAULT;
		}
		return ret;

	/*
	 *	These ioctl calls:
	 *	- require superuser power.
	 *	- require strict serialization.
	 *	- do not return a value
	 */
	case SIOCSIFFLAGS:
	case SIOCSIFMETRIC:
	case SIOCSIFMTU:
	case SIOCSIFMAP:
	case SIOCSIFHWADDR:
	case SIOCSIFSLAVE:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOCSIFHWBROADCAST:
	case SIOCSIFTXQLEN:
	case SIOCSMIIREG:
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDCHANGEACTIVE:
	case SIOCBRADDIF:
	case SIOCBRDELIF:
	case SIOCSHWTSTAMP:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		/* fall through */
	case SIOCBONDSLAVEINFOQUERY:
	case SIOCBONDINFOQUERY:
		dev_load(net, ifr.ifr_name);
		rtnl_lock();
		ret = dev_ifsioc(net, &ifr, cmd);
		rtnl_unlock();
		return ret;

	case SIOCGIFMEM:
		/* Get the per device memory space. We can add this but
		 * currently do not support it */
	case SIOCSIFMEM:
		/* Set the per device memory buffer space.
		 * Not applicable in our case */
	case SIOCSIFLINK:
		return -EINVAL;

	/*
	 *	Unknown or private ioctl.
	 */
	default:
		if (cmd == SIOCWANDEV ||
		    (cmd >= SIOCDEVPRIVATE &&
		     cmd <= SIOCDEVPRIVATE + 15)) {
			dev_load(net, ifr.ifr_name);
			rtnl_lock();
			ret = dev_ifsioc(net, &ifr, cmd);
			rtnl_unlock();
			if (!ret && copy_to_user(arg, &ifr,
						 sizeof(struct ifreq)))
				ret = -EFAULT;
			return ret;
		}
		/* Take care of Wireless Extensions */
		if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST)
			return wext_handle_ioctl(net, &ifr, cmd, arg);
		return -EINVAL;
	}
}


/**
 *	dev_new_index	-	allocate an ifindex
 *	@net: the applicable net namespace
 *
 *	Returns a suitable unique value for a new device interface
 *	number.  The caller must hold the rtnl semaphore or the
 *	dev_base_lock to be sure it remains unique.
 *///为设备分配一个唯一索引号和一个用于虚拟隧道设备的唯一标示号
static int dev_new_index(struct net *net)
{
	static int ifindex;
	for (;;) {
		if (++ifindex <= 0)
			ifindex = 1;
		if (!__dev_get_by_index(net, ifindex))
			return ifindex;
	}
}

/* Delayed registration/unregisteration */
//static LIST_HEAD(net_todo_list);
struct list_head net_todo_list = LIST_HEAD_INIT(net_todo_list);//最终由netdev_run_todo执行该链表中的dev。该链表是判断dev的引用计数refcnt是否为0

//如果为0则直接调用netdev_run_todo里面的函数来释放相关资源
static void net_set_todo(struct net_device *dev)
{
	list_add_tail(&dev->todo_list, &net_todo_list);
}

static void rollback_registered_many(struct list_head *head)
{
	struct net_device *dev, *tmp;

	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	list_for_each_entry_safe(dev, tmp, head, unreg_list) {
		/* Some devices call without registering
		 * for initialization unwind. Remove those
		 * devices and proceed with the remaining.
		 */
		if (dev->reg_state == NETREG_UNINITIALIZED) {
			pr_debug("unregister_netdevice: device %s/%p never "
				 "was registered\n", dev->name, dev);

			WARN_ON(1);
			list_del(&dev->unreg_list);
			continue;
		}

		BUG_ON(dev->reg_state != NETREG_REGISTERED);

		/* If device is running, close it first. */
		dev_close(dev);

		/* And unlink it from device chain. */
		unlist_netdevice(dev);

		dev->reg_state = NETREG_UNREGISTERING;
	}

	synchronize_net();

	list_for_each_entry(dev, head, unreg_list) {
		/* Shutdown queueing discipline. */
		dev_shutdown(dev);


		/* Notify protocols, that we are about to destroy
		   this device. They should clean all the things.
		*/
		call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

		if (!dev->rtnl_link_ops ||
		    dev->rtnl_link_state == RTNL_LINK_INITIALIZED)
			rtmsg_ifinfo(RTM_DELLINK, dev, ~0U);

		/*
		 *	Flush the unicast and multicast chains
		 */
		dev_uc_flush(dev);
		dev_mc_flush(dev);

		if (dev->netdev_ops->ndo_uninit)
			dev->netdev_ops->ndo_uninit(dev);

		/* Notifier chain MUST detach us from master device. */
		WARN_ON(dev->master);

		/* Remove entries from kobject tree */
		netdev_unregister_kobject(dev);
	}

	/* Process any work delayed until the end of the batch */
	dev = list_first_entry(head, struct net_device, unreg_list);
	call_netdevice_notifiers(NETDEV_UNREGISTER_BATCH, dev);

	synchronize_net();

	list_for_each_entry(dev, head, unreg_list)
		dev_put(dev);
}

static void rollback_reg111istered(struct net_device *dev)
{
	LIST_HEAD(single);

	list_add(&dev->unreg_list, &single);
	rollback_registered_many(&single);
}
//unregister_netdevice最终也是调用该函数
static void rollback_registered(struct net_device *dev)
{
	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	/* Some devices call without registering for initialization unwind. */
	/*
	  * 如果设备处于NETREG_UNINITIALIZED状态，即未
	  * 初始化状态，则输出信息后返回。
	  */
	if (dev->reg_state == NETREG_UNINITIALIZED) {
		printk(KERN_DEBUG "unregister_netdevice: device %s/%p never "
				  "was registered\n", dev->name, dev);

		WARN_ON(1);
		return;
	}

	BUG_ON(dev->reg_state != NETREG_REGISTERED);

	/* If device is running, close it first. */
	/*
	  * 如果设备没有关闭，则调用
	  * dev_close()进行关闭
	  */
	dev_close(dev);

	/* And unlink it from device chain. */
	/*
	  * 将待注销的网络设备实例从全局
	  * 链表dev_base及dev_name_head、dev_index_head
	  * 散列表中移除。移除后不能阻止
	  * 内核子系统使用该设备，他们仍然
	  * 拥有指向该net_device结构实例的指针，
	  * 只有当引用计数为0时才会真正释放
	  * 实例。
	  */
	unlist_netdevice(dev);

	/*
	  * 将网络设备实例设置为NETREG_UNREGISTERING
	  * 即未注册状态
	  */
	dev->reg_state = NETREG_UNREGISTERING;
	/*
	  * 同步数据包的接收处理
	  */
	synchronize_net();

	/* Shutdown queueing discipline. */
	/*
	  * 释放所有与设备相关的队列规则实例
	  */
	dev_shutdown(dev);

	/* Notify protocols, that we are about to destroy
	   this device. They should clean all the things.
	*/
	/*
	  * 发送NETDEV_UNREGISTER消息到netdev_chain通知链上，
	  * 以便通知对设备状态改变有兴趣的其他内核
	  * 组件
	  */
	call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

	/*
	 *	Flush the unicast and multicast chains
	 */
	dev_unicast_flush(dev);
	/*
	  * 释放设置到网络设备上的组播MAC地址等信息。
	  */
	dev_addr_discard(dev);

	/*
	  * 进行驱动程序相关的销毁操作，通常
	  * 是销毁那些在init中初始化的数据
	  */
	if (dev->netdev_ops->ndo_uninit)
		dev->netdev_ops->ndo_uninit(dev);

	/* Notifier chain MUST detach us from master device. */
	WARN_ON(dev->master);

	/* Remove entries from kobject tree */
	netdev_unregister_kobject(dev);

	synchronize_net();

	dev_put(dev);
}

static void __netdev_init_queue_locks_one(struct net_device *dev,
					  struct netdev_queue *dev_queue,
					  void *_unused)
{
	spin_lock_init(&dev_queue->_xmit_lock);
	netdev_set_xmit_lockdep_class(&dev_queue->_xmit_lock, dev->type);
	dev_queue->xmit_lock_owner = -1;
}

static void netdev_init_queue_locks(struct net_device *dev)
{
	netdev_for_each_tx_queue(dev, __netdev_init_queue_locks_one, NULL);
	__netdev_init_queue_locks_one(dev, &dev->rx_queue, NULL);
}

unsigned long netdev_fix_features(unsigned long features, const char *name)
{
	/* Fix illegal SG+CSUM combinations. */
	if ((features & NETIF_F_SG) &&
	    !(features & NETIF_F_ALL_CSUM)) {
		if (name)
			printk(KERN_NOTICE "%s: Dropping NETIF_F_SG since no "
			       "checksum feature.\n", name);
		features &= ~NETIF_F_SG;
	}

	/* TSO requires that SG is present as well. */
	if ((features & NETIF_F_TSO) && !(features & NETIF_F_SG)) {//只有NETIF_F_SG有效的时候，TSO才会有效
		if (name)
			printk(KERN_NOTICE "%s: Dropping NETIF_F_TSO since no "
			       "SG feature.\n", name);
		features &= ~NETIF_F_TSO;
	}

	if (features & NETIF_F_UFO) {
		if (!(features & NETIF_F_GEN_CSUM)) {
			if (name)
				printk(KERN_ERR "%s: Dropping NETIF_F_UFO "
				       "since no NETIF_F_HW_CSUM feature.\n",
				       name);
			features &= ~NETIF_F_UFO;
		}

		if (!(features & NETIF_F_SG)) {
			if (name)
				printk(KERN_ERR "%s: Dropping NETIF_F_UFO "
				       "since no NETIF_F_SG feature.\n", name);
			features &= ~NETIF_F_UFO;
		}
	}

	return features;
}
EXPORT_SYMBOL(netdev_fix_features);

/**
 *	netif_stacked_transfer_operstate -	transfer operstate
 *	@rootdev: the root or lower level device to transfer state from
 *	@dev: the device to transfer operstate to
 *
 *	Transfer operational state from root to device. This is normally
 *	called when a stacking relationship exists between the root
 *	device and the device(a leaf device).
 */
void netif_stacked_transfer_operstate(const struct net_device *rootdev,
					struct net_device *dev)
{
	if (rootdev->operstate == IF_OPER_DORMANT)
		netif_dormant_on(dev);
	else
		netif_dormant_off(dev);

	if (netif_carrier_ok(rootdev)) {
		if (!netif_carrier_ok(dev))
			netif_carrier_on(dev);
	} else {
		if (netif_carrier_ok(dev))
			netif_carrier_off(dev);
	}
}
EXPORT_SYMBOL(netif_stacked_transfer_operstate);

/**
 *	register_netdevice	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	Callers must hold the rtnl semaphore. You may want
 *	register_netdev() instead of this.
 *
 *	BUGS:
 *	The locking appears insufficient to guarantee two parallel registers
 *	will not get the same name.
 */
 /*
 * 当待注册的网络设备名确定之后，便调用register_netdevice()注册网络设备，并将
 * 网络设备描述符注册到系统中。完成注册后，会发送NETDEV_REGISTER消息到netdev_chain
 * 通知链中，使得所有对设备注册感兴趣的模块都能接收消息。
 */
//网络设备注册的时机:加载网络设备的驱动程序 、  擦入可热插拔的网络设备
//由驱动程序控制的网络设备都将会被注册  
////alloc_netdev分配好空间后，调用alloc_netdev完成注册
/*
  * 在调用该函数前必须调用rtnl_lock()来获取rtnl互斥锁
  */
int register_netdevice(struct net_device *dev)
{
	struct hlist_head *head;
	struct hlist_node *p;
	int ret;
	struct net *net = dev_net(dev);

	/*
	  * 如果为真，则表示设备层的初始化(net_dev_init())
	  * 尚未完成，此时注册网络设备即
	  * 为BUG。
	  */
	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	/*
	  * 2.6版本内核支持内核抢占，might_sleep()宏检查
	  * 是否需要重新调度，如果是，则重新调度，
	  * 无论此时进程执行在内核空间还是
	  * 用户空间。
	  */
	might_sleep();

	/* When net_device's are persistent, this will be fatal. */
	BUG_ON(dev->reg_state != NETREG_UNINITIALIZED);
	BUG_ON(!net);

	spin_lock_init(&dev->addr_list_lock);
	netdev_set_addr_lockdep_class(dev);
	netdev_init_queue_locks(dev);//对队列中的发送队列_tx[]锁和接收队列rx_queue进行初始化

	dev->iflink = -1;

	/* Init, if this function is available */
       /* 
        * 如果有初始化函数，则先初始化.
        * netdev_ops由不同的网络设备驱动程序
        * 来初始化，可以参考3c501.c中el1_probe函数
        * 来注册3c501网卡的过程
        */
	/*
	 * 如果设备驱动程序提供了初始化函数，则进行相关初始化。
	 */
	if (dev->netdev_ops->ndo_init) {
		ret = dev->netdev_ops->ndo_init(dev);//一般在alloc_netdev的setup或者xxx_probe(例如e100_probe)中初始化
		if (ret) {
			if (ret > 0)
				ret = -EIO;
			goto out;
		}
	}

	/*
	 * 调用dev_valid_name()检测待注册的网络设备名是否有效。
	 */
	if (!dev_valid_name(dev->name)) {
		ret = -EINVAL;
		goto err_uninit;
	}
	/*
	 * 调用dev_new_index()为设备分配一个唯一索引号和一个用于虚拟隧道设备
	 * 的唯一标识。索引号由一个32位计数器产生，每当一个新设备加到系统中
	 * 计数器就会递增。
	 */
	dev->ifindex = dev_new_index(net);
	if (dev->iflink == -1)
		dev->iflink = dev->ifindex; 

	/* Check for existence of name */
	/*
	 * 将网络设备添加到dev_name_head散列表中，并检测是否存在同名
	 * 的网络设备。
	 */
	head = dev_name_hash(net, dev->name);
	hlist_for_each(p, head) {
		struct net_device *d
			= hlist_entry(p, struct net_device, name_hlist);
		if (!strncmp(d->name, dev->name, IFNAMSIZ)) {
			ret = -EEXIST;
			goto err_uninit;
		}
	}

	/* Fix illegal checksum combinations */
	/* 
        * 检查特性,这些特性的宏定义在net_device定义时
        * 以宏的形式列出来，在include\linux\netdevice.h
        * NETIF_F_HW_CSUM: 可以对所有包进行校验
        * NETIF_F_IP_CSUM: 可以对使用ipv4协议的TCP/UDP进行校验
        * NETIF_F_IPV6_CSUM: 可以对使用ipv6协议的TCP/UDP进行校验
        */
	if ((dev->features & NETIF_F_HW_CSUM) &&
	    (dev->features & (NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM))) {
		printk(KERN_NOTICE "%s: mixed HW and IP checksum settings.\n",
		       dev->name);
             /* 
              * 如果上述三个特性都设置，则将NETIF_F_IP_CSUM、NETIF_F_IPV6_CSUM
              * 两个特性清除。或许是因为对所有包已进行校验了，这
              * 两个特性已经被涵盖了,或许跟后面的实现有关
              */
		dev->features &= ~(NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM);
	}

       /*
        * NETIF_F_NO_CSUM: 不要求计算校验和
        */
	if ((dev->features & NETIF_F_NO_CSUM) &&
	    (dev->features & (NETIF_F_HW_CSUM|NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM))) {
		printk(KERN_NOTICE "%s: mixed no checksumming and other settings.\n",
		       dev->name);
              /*将下列三个特性位清除 */
		dev->features &= ~(NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM|NETIF_F_HW_CSUM);
	}

	dev->features = netdev_fix_features(dev->features, dev->name);

	/* Enable software GSO if SG is supported. */
	if (dev->features & NETIF_F_SG)
		dev->features |= NETIF_F_GSO;

       /* 初始化dev的device类型成员dev*/
	netdev_initialize_kobject(dev);
       /* 在sysfs中创建跟设备关联的项*/
	ret = netdev_register_kobject(dev);
	if (ret)
		goto err_uninit;
	dev->reg_state = NETREG_REGISTERED;

	/*
	 *	Default initial state at registry is that the
	 *	device is present.
	 */

	set_bit(__LINK_STATE_PRESENT, &dev->state);

	dev_init_scheduler(dev);
	dev_hold(dev);
       /* 插入到特定命令空间的链表和散列表中*/
	list_netdevice(dev);

	/* Notify protocols, that a new device appeared. */
	ret = call_netdevice_notifiers(NETDEV_REGISTER, dev);
	ret = notifier_to_errno(ret);
	if (ret) {
		rollback_registered(dev);
		dev->reg_state = NETREG_UNREGISTERED;
	}

out:
	return ret;

err_uninit:
	if (dev->netdev_ops->ndo_uninit)
		dev->netdev_ops->ndo_uninit(dev);
	goto out;
}

EXPORT_SYMBOL(register_netdevice);

/**
 *	init_dummy_netdev	- init a dummy network device for NAPI
 *	@dev: device to init
 *
 *	This takes a network device structure and initialize the minimum
 *	amount of fields so it can be used to schedule NAPI polls without
 *	registering a full blown interface. This is to be used by drivers
 *	that need to tie several hardware interfaces to a single NAPI
 *	poll scheduler due to HW limitations.
 */
int init_dummy_netdev(struct net_device *dev)
{
	/* Clear everything. Note we don't initialize spinlocks
	 * are they aren't supposed to be taken by any of the
	 * NAPI code and this dummy netdev is supposed to be
	 * only ever used for NAPI polls
	 */
	memset(dev, 0, sizeof(struct net_device));

	/* make sure we BUG if trying to hit standard
	 * register/unregister code path
	 */
	dev->reg_state = NETREG_DUMMY;

	/* initialize the ref count */
	atomic_set(&dev->refcnt, 1);

	/* NAPI wants this */
	INIT_LIST_HEAD(&dev->napi_list);

	/* a dummy interface is started by default */
	set_bit(__LINK_STATE_PRESENT, &dev->state);
	set_bit(__LINK_STATE_START, &dev->state);

	return 0;
}
EXPORT_SYMBOL_GPL(init_dummy_netdev);


/**
 *	register_netdev	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	This is a wrapper around register_netdevice that takes the rtnl semaphore
 *	and expands the device name if you passed a format string to
 *	alloc_netdev.
 *///alloc_netdev分配好空间后，调用register_netdev完成注册，卸载的时候unregister_netdevice和free_netdev完成注销并释放内存
int register_netdev(struct net_device *dev)
{
	int err;

	rtnl_lock();

	/*
	 * If the name is a format string the caller wants us to do a
	 * name allocation.
	 */
	if (strchr(dev->name, '%')) {
		err = dev_alloc_name(dev, dev->name);
		if (err < 0)
			goto out;
	}

	err = register_netdevice(dev);
out:
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(register_netdev);

/*
 * netdev_wait_allrefs - wait until all references are gone.
 *
 * This is called when unregistering network devices.
 *
 * Any protocol or device that holds a reference should register
 * for netdevice notification, and cleanup and put back the
 * reference if they receive an UNREGISTER event.
 * We can get stuck here if buggy protocols don't correctly
 * call dev_put.
 */
/*
  * netdev_wait_allrefs()由一个循环组成，直到
  * 网络设备的引用计数值减到0结束。
  * 等待过程中每秒发送一次NETDEV_UNREGISTER通知，
  * 每10s在控制台打印一次警告。在发送通知时，
  * 如果发生了连接状态改变事件，则一定要处理。
  * 任何持有网络设备引用的协议或设备都要注册
  * 网络设备通知，当它们接收到NETDEV_UNREGISTER
  * 事件时，要开始进行清理并释放对网络设备的引用
  *///在unregister_netdev的时候，走到这里，每过1s想时间通知链上面通告一次，其他引用该dev的模块收到该通知后，需要使用dev_put来取消对该dev的引用
static void netdev_wait_allrefs(struct net_device *dev)
{
	unsigned long rebroadcast_time, warning_time;

	rebroadcast_time = warning_time = jiffies;
	/*
	  * 循环等待，直到引用计数为0
	  */
	while (atomic_read(&dev->refcnt) != 0) { //等于0的时候退出循环
		if (time_after(jiffies, rebroadcast_time + 1 * HZ)) {
			rtnl_lock();

			/* Rebroadcast unregister notification */
			/*
			  * 在等待过程中每秒广播一次NETDEV_UNREGISTER
			  * 消息。网络设备在注销期间，如果发生了 
			  * 连接状态改变事件，则一定要处理。
			  */
			call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

			if (test_bit(__LINK_STATE_LINKWATCH_PENDING,
				     &dev->state)) {
				/* We must not have linkwatch events
				 * pending on unregister. If this
				 * happens, we simply run the queue
				 * unscheduled, resulting in a noop
				 * for this device.
				 */
				linkwatch_run_queue();
			}

			__rtnl_unlock();

			rebroadcast_time = jiffies;
		}

		msleep(250);

		/*
		  * 在等待过程中，如果等待时间超过10s，
		  * 则会每10s打印一次警告信息。
		  */
		if (time_after(jiffies, warning_time + 10 * HZ)) {
			printk(KERN_EMERG "unregister_netdevice: "
			       "waiting for %s to become free. Usage "
			       "count = %d\n",
			       dev->name, atomic_read(&dev->refcnt));
			warning_time = jiffies;
		}
	}
}

/* The sequence is:
 *
 *	rtnl_lock();
 *	...
 *	register_netdevice(x1);
 *	register_netdevice(x2);
 *	...
 *	unregister_netdevice(y1);
 *	unregister_netdevice(y2);
 *      ...
 *	rtnl_unlock();
 *	free_netdev(y1);
 *	free_netdev(y2);
 *
 * We are invoked by rtnl_unlock().
 * This allows us to deal with problems:
 * 1) We can delete sysfs objects which invoke hotplug
 *    without deadlocking with linkwatch via keventd.
 * 2) Since we run with the RTNL semaphore not held, we can sleep
 *    safely in order to wait for the netdev refcnt to drop to zero.
 *
 * We must not return until all unregister events added during
 * the interval the lock was held have been completed.
 */
/*
  * netdev_run_todo()函数用来处理队列net_todo_list上
  * 的网络设备，继续处理相关的注销事务。
  * 主要是注销sysfs中该设备的结点。注销时，
  * 等待设备的引用计数为0，再调用设备
  * 自身的destruct()函数，完成注销过程。
  *///unregister_netdev的时候把dev添加到了net_todo_list链表中，见net_set_todo
void netdev_run_todo(void)
{
	struct list_head list;

	/* Snapshot list, allow later requests */
	/*
	  * 在持有锁的过程中，将net_todo_list
	  * 中的所有对象都存储到一个栈上的
	  * 临时变量中，这样就可以在没有锁
	  * 的情况下安全地处理所有待销毁
	  * 的设备。这里真是非常的巧妙
	  */
	list_replace_init(&net_todo_list, &list);

	__rtnl_unlock();

	while (!list_empty(&list)) {
		struct net_device *dev
			= list_entry(list.next, struct net_device, todo_list);
		list_del(&dev->todo_list);

		if (unlikely(dev->reg_state != NETREG_UNREGISTERING)) {
			printk(KERN_ERR "network todo '%s' but state %d\n",
			       dev->name, dev->reg_state);
			dump_stack();
			continue;
		}

		dev->reg_state = NETREG_UNREGISTERED;

		/*
		  * 清除每个CPU接收队列上pending的数据包
		  */
		on_each_cpu(flush_backlog, dev, 1);

		/*
		  * 等待直到待注销的网络设备没有引用为止，
		  * 也就是等待引用计数为0.
		  */
		netdev_wait_allrefs(dev);

		/* paranoia */
		BUG_ON(atomic_read(&dev->refcnt));
		WARN_ON(dev->ip_ptr);
		WARN_ON(dev->ip6_ptr);
		WARN_ON(dev->dn_ptr);

		/*
		  * destructor函数通常会调用free_netdev()函数，
		  * 在该函数中会检查设备的状态，
		  * 只有在设备处于NETREG_UNINITIALIZED
		  * 状态时才会将dev占用的内存释放。
		  */
		if (dev->destructor)
			dev->destructor(dev);

		/* Free network device */
		/*
		  * 在这个函数的后续调用过程中
		  * 会调用netdev_release()函数来释放
		  * dev所占用的内存。netdev_release()在
		  * net_class中的release成员中，在
		  * netdev_register_kobject()中设置到device结构(即dev->dev)
		  * 的class成员中。
		  */
		kobject_put(&dev->dev.kobj);
	}
}

/**
 *	dev_txq_stats_fold - fold tx_queues stats
 *	@dev: device to get statistics from
 *	@stats: struct net_device_stats to hold results
 */
void dev_txq_stats_fold(const struct net_device *dev,
			struct net_device_stats *stats)
{
	unsigned long tx_bytes = 0, tx_packets = 0, tx_dropped = 0;
	unsigned int i;
	struct netdev_queue *txq;

	for (i = 0; i < dev->num_tx_queues; i++) {
		txq = netdev_get_tx_queue(dev, i);
		tx_bytes   += txq->tx_bytes;
		tx_packets += txq->tx_packets;
		tx_dropped += txq->tx_dropped;
	}
	if (tx_bytes || tx_packets || tx_dropped) {
		stats->tx_bytes   = tx_bytes;
		stats->tx_packets = tx_packets;
		stats->tx_dropped = tx_dropped;
	}
}
EXPORT_SYMBOL(dev_txq_stats_fold);

/**
 *	dev_get_stats	- get network device statistics
 *	@dev: device to get statistics from
 *
 *	Get network statistics from device. The device driver may provide
 *	its own method by setting dev->netdev_ops->get_stats; otherwise
 *	the internal statistics structure is used.
 */
const struct net_device_stats *dev_get_stats(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (ops->ndo_get_stats)
		return ops->ndo_get_stats(dev);

	dev_txq_stats_fold(dev, &dev->stats);
	return &dev->stats;
}
EXPORT_SYMBOL(dev_get_stats);

static void netdev_init_one_queue(struct net_device *dev,
				  struct netdev_queue *queue,
				  void *_unused)
{
	queue->dev = dev;
}

static void netdev_init_queues(struct net_device *dev)
{
	netdev_init_one_queue(dev, &dev->rx_queue, NULL);
	netdev_for_each_tx_queue(dev, netdev_init_one_queue, NULL);
	spin_lock_init(&dev->tx_global_lock);
}

/**
 *	alloc_netdev_mq - allocate network device
 *	@sizeof_priv:	size of private data to allocate space for
 *	@name:		device name format string
 *	@setup:		callback to initialize device
 *	@queue_count:	the number of subqueues to allocate
 *
 *	Allocates a struct net_device with private data area for driver use
 *	and performs basic initialization.  Also allocates subquue structs
 *	for each queue on the device at the end of the netdevice.
 */
 //sizeof_priv用来存储驱动程序私有数据的大小，name设备名，setup配置函数用于初始化net_device结构实例的部分域,一般用ether_setup函数  queue_count为接收队列个数
 //由alloc_netdev_mq分配的空间组成：net_device数据结构的内存空间+私有数据内存空间+设备发送队列的内存空间。
 //不同设备分配参数会不一样
struct net_device *alloc_n33etdev_mq(int sizeof_priv, const char *name,
		void (*setup)(struct net_device *), unsigned int queue_count)
{
	struct netdev_queue *tx;
	struct net_device *dev;
	size_t alloc_size;
	struct net_device *p;
#ifdef CONFIG_RPS
	struct netdev_rx_queue *rx;
	int i;
#endif

	BUG_ON(strlen(name) >= sizeof(dev->name));

	alloc_size = sizeof(struct net_device);
	if (sizeof_priv) {
		/* ensure 32-byte alignment of private area */
		alloc_size = ALIGN(alloc_size, NETDEV_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* ensure 32-byte alignment of whole construct */
	alloc_size += NETDEV_ALIGN - 1;

	p = kzalloc(alloc_size, GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR "alloc_netdev: Unable to allocate device.\n");
		return NULL;
	}

	tx = kcalloc(queue_count, sizeof(struct netdev_queue), GFP_KERNEL);
	if (!tx) {
		printk(KERN_ERR "alloc_netdev: Unable to allocate "
		       "tx qdiscs.\n");
		goto free_p;
	}

#ifdef CONFIG_RPS
	rx = kcalloc(queue_count, sizeof(struct netdev_rx_queue), GFP_KERNEL);
	if (!rx) {
		printk(KERN_ERR "alloc_netdev: Unable to allocate "
		       "rx queues.\n");
		goto free_tx;
	}

	atomic_set(&rx->count, queue_count);

	/*
	 * Set a pointer to first element in the array which holds the
	 * reference count.
	 */
	for (i = 0; i < queue_count; i++)
		rx[i].first = rx;
#endif

	dev = PTR_ALIGN(p, NETDEV_ALIGN);
	dev->padded = (char *)dev - (char *)p;

	if (dev_addr_init(dev))
		goto free_rx;

	dev_mc_init(dev);
	dev_uc_init(dev);

	dev_net_set(dev, &init_net);

	dev->_tx = tx;
	dev->num_tx_queues = queue_count;
	dev->real_num_tx_queues = queue_count;

#ifdef CONFIG_RPS
	dev->_rx = rx;
	dev->num_rx_queues = queue_count;
#endif

	dev->gso_max_size = GSO_MAX_SIZE;

	netdev_init_queues(dev);

	INIT_LIST_HEAD(&dev->ethtool_ntuple_list.list);
	dev->ethtool_ntuple_list.count = 0;
	INIT_LIST_HEAD(&dev->napi_list);
	INIT_LIST_HEAD(&dev->unreg_list);
	INIT_LIST_HEAD(&dev->link_watch_list);
	dev->priv_flags = IFF_XMIT_DST_RELEASE;
	setup(dev);
	strcpy(dev->name, name);
	return dev;

free_rx:
#ifdef CONFIG_RPS
	kfree(rx);
free_tx:
#endif
	kfree(tx);
free_p:
	kfree(p);
	return NULL;
}


/**
 *	alloc_netdev_mq - allocate network device
 *	@sizeof_priv:	size of private data to allocate space for
 *	@name:		device name format string
 *	@setup:		callback to initialize device
 *	@queue_count:	the number of subqueues to allocate
 *
 *	Allocates a struct net_device with private data area for driver use
 *	and performs basic initialization.  Also allocates subquue structs
 *	for each queue on the device at the end of the netdevice.
 */
/*
 * 网络设备由net_device结构定义,每个net_device结构实例代表一个网络设备,该
 * 结构的实例由alloc_netdev()分配空间,参数说明如下:
 * @sizeof_priv:指定用于存储驱动程序参数的私有数据块大小,参见alloc_etherdrv()函数.
 * @name:设备名,通常是个前缀,相同前缀的设备会进行统一编号,以确保设备名唯一.
 * @setup:配置函数,用于初始化net_device结构实例的部分域,参见ether_setup()函数.
 */

/*
                    表8-1 alloc_netdev包裹函数
网络设备类型            封装函数名
以太网                   alloc_etherdev                     return alloc_netdev(sizeof_priv,"eth%d",ether_setup);   以太网设备全部使用该函数
光纤分布式数据接口       alloc_fddidev                      return alloc_netdev(sizeof_priv,"fddi%d",fddi_setup);
高性能并行接口          alloc_hippi_dev                     return alloc_netdev(sizeof_priv,"hip%d",hippi_setup);
令牌网                  alloc_trdev                         return alloc_netdev(sizeof_priv,"tr%d",tr_setup);
光纤通道                alloc_fcdev                         return alloc_netdev(sizeof_priv,"fc%d",fc_setup);
红外数据联盟            alloc_irdadev                       return alloc_netdev(sizeof_priv,"irda%d",irda_device_setup);
*/
struct net_device *alloc_netdev_mq(int sizeof_priv, const char *name,
		void (*setup)(struct net_device *), unsigned int queue_count) //以e100网卡为例，在e100_probe中进行调用
    //alloc_netdev分配好空间后，调用register_netdev完成注册，卸载的时候unregister_netdevice和free_netdev完成注销并释放内存,真正注销在netdev_run_todo
{//在unregister_netdev的时候，走到这里，每过1s想时间通知链上面通告一次，其他引用该dev的模块收到该通知后，需要使用dev_put来取消对该dev的引用。见netdev_wait_allrefs
	struct netdev_queue *tx;
	struct net_device *dev;
	size_t alloc_size;
	struct net_device *p;

	/*
	  * 检查name的长度是否超过16个字节
	  */
	BUG_ON(strlen(name) >= sizeof(dev->name));

	alloc_size = sizeof(struct net_device);
	if (sizeof_priv) {
		/* ensure 32-byte alignment of private area */
		alloc_size = ALIGN(alloc_size, NETDEV_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* ensure 32-byte alignment of whole construct */
      /* 
       * 分配的net_device实例 + 私有数据的指针会暂时存储在临时变量p中，
       * 但是这个地址有可能不是32位对齐的，所以在后面
       * 会调用PTR_ALIGN对这个地址进行修正。如果p不是32位
       * 对齐的，对齐后的地址dev会在p之后，这样在前边
       * 会留出一段空闲的地址。所以这里要加上31，多
       * 分配一些内存
       */
	alloc_size += NETDEV_ALIGN - 1;

	p = kzalloc(alloc_size, GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR "alloc_netdev: Unable to allocate device.\n");
		return NULL;
	}

	/*
	  * 分配网络设备的发送队列
	  */ //多个queue_count netdev_queue
	tx = kcalloc(queue_count, sizeof(struct netdev_queue), GFP_KERNEL);
	if (!tx) {
		printk(KERN_ERR "alloc_netdev: Unable to allocate "
		       "tx qdiscs.\n");
		goto free_p;
	}

	dev = PTR_ALIGN(p, NETDEV_ALIGN);
       /* 计算分配的地址和实际使用的地址之前的偏移*/
	dev->padded = (char *)dev - (char *)p;

       /* 在dev->dev_addrs中添加一项，并且初始化dev_add*/
	if (dev_addr_init(dev))
		goto free_tx;

       /* 初始化单播地址*/
	//dev_unicast_init(dev);
    dev_mc_init(dev);
	dev_uc_init(dev);
       /* 设置设备所属的命名空间*/
	dev_net_set(dev, &init_net);

       /* 初始化发送队列*/
	dev->_tx = tx; //_tx的第0个地址为dev->_tx[0], 低N个一次类推
	dev->num_tx_queues = queue_count;
	dev->real_num_tx_queues = queue_count;

	dev->gso_max_size = GSO_MAX_SIZE;

	/*
	  * 初始化设备的发送和接收队列
	  */
	netdev_init_queues(dev);

	INIT_LIST_HEAD(&dev->napi_list);
	dev->priv_flags = IFF_XMIT_DST_RELEASE;
     /* 
         * 调用setup函数来初始化设备，对于以太
         * 网设备，默认函数式ether_setup()
         */
	setup(dev); //例如ppp_setup
	strcpy(dev->name, name); //name%d中%d赋值的地方在register_netdevice中的dev_get_valid_name
	return dev;

free_tx:
	kfree(tx);

free_p:
	kfree(p);
	return NULL;
}

EXPORT_SYMBOL(alloc_netdev_mq);

/**
 *	free_netdev - free network device
 *	@dev: device
 *
 *	This function does the last stage of destroying an allocated device
 * 	interface. The reference to the device object is released.
 *	If this is the last reference then it will be freed.
 *///alloc_netdev分配好空间后，调用register_netdev完成注册，卸载的时候unregister_netdevice和free_netdev完成注销并释放内存
void free_netdev(struct net_device *dev)
{
	struct napi_struct *p, *n;

	release_net(dev_net(dev));

	kfree(dev->_tx);

	/* Flush device addresses */
	dev_addr_flush(dev);

	/* Clear ethtool n-tuple list */
	ethtool_ntuple_flush(dev);

	list_for_each_entry_safe(p, n, &dev->napi_list, dev_list)
		netif_napi_del(p);

	/*  Compatibility with error handling in drivers */
	if (dev->reg_state == NETREG_UNINITIALIZED) {
		kfree((char *)dev - dev->padded);
		return;
	}

	BUG_ON(dev->reg_state != NETREG_UNREGISTERED);
	dev->reg_state = NETREG_RELEASED;

	/* will free via device release */
	put_device(&dev->dev);
}
EXPORT_SYMBOL(free_netdev);

/**
 *	synchronize_net -  Synchronize with packet receive processing
 *
 *	Wait for packets currently being received to be done.
 *	Does not block later packets from starting.
 */
void synchronize_net(void)
{
	might_sleep();
	synchronize_rcu();
}
EXPORT_SYMBOL(synchronize_net);

/**
 *	unregister_netdevice_queue - remove device from the kernel
 *	@dev: device
 *	@head: list
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables.
 *	If head not NULL, device is queued to be unregistered later.
 *
 *	Callers must hold the rtnl semaphore.  You may want
 *	unregister_netdev() instead of this.
 */

void unregister_netdevice_queue(struct net_device *dev, struct list_head *head)
{
	ASSERT_RTNL();

	if (head) {
		list_move_tail(&dev->unreg_list, head);
	} else {
		rollback_registered(dev);
		/* Finish processing unregister after unlock */
		net_set_todo(dev);//最终由netdev_run_todo完成net_device是否,free_netdev
	}
}
EXPORT_SYMBOL(unregister_netdevice_queue);

/**
 *	unregister_netdevice_many - unregister many devices
 *	@head: list of devices
 */
void unregister_netdevice_many(struct list_head *head)
{
	struct net_device *dev;

	if (!list_empty(head)) {
		rollback_registered_many(head);
		list_for_each_entry(dev, head, unreg_list)
			net_set_todo(dev);
	}
}
EXPORT_SYMBOL(unregister_netdevice_many);

/**
 *	unregister_netdev - remove device from the kernel
 *	@dev: device
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables.
 *
 *	This is just a wrapper for unregister_netdevice that takes
 *	the rtnl semaphore.  In general you want to use this and not
 *	unregister_netdevice.
 */ //移除dev网卡设备内核模块的时候(如e100.ko)  或者拔掉热插拔网卡 触发
 //最终由netdev_run_todo完成net_device是否,free_netdev
 //在unregister_netdev的时候，走到这里，每过1s想时间通知链上面通告一次，其他引用该dev的模块收到该通知后，需要使用dev_put来取消对该dev的引用。见rtnl_unlock->netdev_wait_allrefs
void unregister_netdev(struct net_device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();//调用netdev_run_todo
}
EXPORT_SYMBOL(unregister_netdev);

/**
 *	dev_change_net_namespace - move device to different nethost namespace
 *	@dev: device
 *	@net: network namespace
 *	@pat: If not NULL name pattern to try if the current device name
 *	      is already taken in the destination network namespace.
 *
 *	This function shuts down a device interface and moves it
 *	to a new network namespace. On success 0 is returned, on
 *	a failure a netagive errno code is returned.
 *
 *	Callers must hold the rtnl semaphore.
 */

int dev_change_net_namespace(struct net_device *dev, struct net *net, const char *pat)
{
	int err;

	ASSERT_RTNL();

	/* Don't allow namespace local devices to be moved. */
	err = -EINVAL;
	if (dev->features & NETIF_F_NETNS_LOCAL)
		goto out;

	/* Ensure the device has been registrered */
	err = -EINVAL;
	if (dev->reg_state != NETREG_REGISTERED)
		goto out;

	/* Get out if there is nothing todo */
	err = 0;
	if (net_eq(dev_net(dev), net))
		goto out;

	/* Pick the destination device name, and ensure
	 * we can use it in the destination network namespace.
	 */
	err = -EEXIST;
	if (__dev_get_by_name(net, dev->name)) {
		/* We get here if we can't use the current device name */
		if (!pat)
			goto out;
		if (dev_get_valid_name(dev, pat, 1))
			goto out;
	}

	/*
	 * And now a mini version of register_netdevice unregister_netdevice.
	 */

	/* If device is running close it first. */
	dev_close(dev);

	/* And unlink it from device chain */
	err = -ENODEV;
	unlist_netdevice(dev);

	synchronize_net();

	/* Shutdown queueing discipline. */
	dev_shutdown(dev);

	/* Notify protocols, that we are about to destroy
	   this device. They should clean all the things.
	*/
	call_netdevice_notifiers(NETDEV_UNREGISTER, dev);
	call_netdevice_notifiers(NETDEV_UNREGISTER_BATCH, dev);

	/*
	 *	Flush the unicast and multicast chains
	 */
	dev_uc_flush(dev);
	dev_mc_flush(dev);

	/* Actually switch the network namespace */
	dev_net_set(dev, net);

	/* If there is an ifindex conflict assign a new one */
	if (__dev_get_by_index(net, dev->ifindex)) {
		int iflink = (dev->iflink == dev->ifindex);
		dev->ifindex = dev_new_index(net);
		if (iflink)
			dev->iflink = dev->ifindex;
	}

	/* Fixup kobjects */
	err = device_rename(&dev->dev, dev->name);
	WARN_ON(err);

	/* Add the device back in the hashes */
	list_netdevice(dev);

	/* Notify protocols, that a new device appeared. */
	call_netdevice_notifiers(NETDEV_REGISTER, dev);

	/*
	 *	Prevent userspace races by waiting until the network
	 *	device is fully setup before sending notifications.
	 */
	rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U);

	synchronize_net();
	err = 0;
out:
	return err;
}
EXPORT_SYMBOL_GPL(dev_change_net_namespace);

/*
  * 每个CPU都有各自的softnet_data，通常情况下
  * CPU都能处理softnet_data中的输出队列和输入
  * 队列等。当CPU状态变化时，有一个状态
  * 需要特殊处理，那就是CPU_DEAD，此时
  * CPU已无法工作，因此需要将该CPU的
  * softnet_data输入输出队列中的报文转交给
  * 其他CPU处理。为了能响应CPU状态的变化，
  * 在接口层初始化函数中通过hotcpu_notifier()注册
  * 了响应CPU状态变化的回调函数dev_cpu_callback()。
  * 参数说明如下:
  * @nfb:包括用来响应CPU状态变化回调函数的信息块。
  * @action:状态发生变化的CPU的当前状态。
  */
static int dev_cpu_callback(struct notifier_block *nfb,
			    unsigned long action,
			    void *ocpu)
{
	struct sk_buff **list_skb;
	struct Qdisc **list_net;
	struct sk_buff *skb;
	unsigned int cpu, oldcpu = (unsigned long)ocpu;
	struct softnet_data *sd, *oldsd;

	/*
	  * 只处理CPU_DEAD状态或CPU_DEAD_FROZEN行为，处于该状态
	  * 的CPU已不能再处理其softnet_data上的相关队列了，因此
	  * 需要作相应的处理。
	  */
	if (action != CPU_DEAD && action != CPU_DEAD_FROZEN)
		return NOTIFY_OK;

	local_irq_disable();
	/*
	  * 获取状态发生变化CPU的softnet_data以及当前CPU的softnet_data。
	  */
	cpu = smp_processor_id();
	sd = &per_cpu(softnet_data, cpu);
	oldsd = &per_cpu(softnet_data, oldcpu);

	/* Find end of our completion_queue. */
	/*
	  * 将状态发生变化CPU的completion_queue队列中
	  * 的报文转移到当前CPU的completion_queue队列。
	  */
	list_skb = &sd->completion_queue;
	while (*list_skb)
		list_skb = &(*list_skb)->next;
	/* Append completion queue from offline CPU. */
	*list_skb = oldsd->completion_queue;
	oldsd->completion_queue = NULL;

	/* Find end of our output_queue. */
	/*
	  * 将状态发生变化CPU的output_queue队列中
	  * 的报文转移到当前CPU的output_queue队列。
	  */
	list_net = &sd->output_queue;
	while (*list_net)
		list_net = &(*list_net)->next_sched;
	/* Append output queue from offline CPU. */
	*list_net = oldsd->output_queue;
	oldsd->output_queue = NULL;

	/*
	  * 经过以上操作，当前CPU的softnet_data中可能存在
	  * 完成输出和等待输出的报文，因此再次激活
	  * 数据包输出软中断，以便释放完成输出的报文，
	  * 输出等待发送的报文。
	  */
	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_enable();

	/* Process offline CPU's input_pkt_queue */
	/*
	  * 最后处理状态发生变化CPU的input_pkt_queue队列，
	  * 将队列上的报文输入到上层协议
	  */
	while ((skb = __skb_dequeue(&oldsd->input_pkt_queue)))
		netif_rx(skb);

	return NOTIFY_OK;
}


/**
 *	netdev_increment_features - increment feature set by one
 *	@all: current feature set
 *	@one: new feature set
 *	@mask: mask feature set
 *
 *	Computes a new feature set after adding a device with feature set
 *	@one to the master device with current feature set @all.  Will not
 *	enable anything that is off in @mask. Returns the new feature set.
 */
unsigned long netdev_increment_features(unsigned long all, unsigned long one,
					unsigned long mask)
{
	/* If device needs checksumming, downgrade to it. */
	if (all & NETIF_F_NO_CSUM && !(one & NETIF_F_NO_CSUM))
		all ^= NETIF_F_NO_CSUM | (one & NETIF_F_ALL_CSUM);
	else if (mask & NETIF_F_ALL_CSUM) {
		/* If one device supports v4/v6 checksumming, set for all. */
		if (one & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM) &&
		    !(all & NETIF_F_GEN_CSUM)) {
			all &= ~NETIF_F_ALL_CSUM;
			all |= one & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
		}

		/* If one device supports hw checksumming, set for all. */
		if (one & NETIF_F_GEN_CSUM && !(all & NETIF_F_GEN_CSUM)) {
			all &= ~NETIF_F_ALL_CSUM;
			all |= NETIF_F_HW_CSUM;
		}
	}

	one |= NETIF_F_ALL_CSUM;

	one |= all & NETIF_F_ONE_FOR_ALL;
	all &= one | NETIF_F_LLTX | NETIF_F_GSO | NETIF_F_UFO;
	all |= one & mask & NETIF_F_ONE_FOR_ALL;

	return all;
}
EXPORT_SYMBOL(netdev_increment_features);

static struct hlist_head *netdev_create_hash(void)
{
	int i;
	struct hlist_head *hash;

	hash = kmalloc(sizeof(*hash) * NETDEV_HASHENTRIES, GFP_KERNEL);
	if (hash != NULL)
		for (i = 0; i < NETDEV_HASHENTRIES; i++)
			INIT_HLIST_HEAD(&hash[i]);

	return hash;
}

/* Initialize per network namespace state */
static int __net_init netdev_init(struct net *net)
{
	INIT_LIST_HEAD(&net->dev_base_head);

	net->dev_name_head = netdev_create_hash();
	if (net->dev_name_head == NULL)
		goto err_name;

	net->dev_index_head = netdev_create_hash();
	if (net->dev_index_head == NULL)
		goto err_idx;

	return 0;

err_idx:
	kfree(net->dev_name_head);
err_name:
	return -ENOMEM;
}

/**
 *	netdev_drivername - network driver for the device
 *	@dev: network device
 *	@buffer: buffer for resulting name
 *	@len: size of buffer
 *
 *	Determine network driver for device.
 */
char *netdev_drivername(const struct net_device *dev, char *buffer, int len)
{
	const struct device_driver *driver;
	const struct device *parent;

	if (len <= 0 || !buffer)
		return buffer;
	buffer[0] = 0;

	parent = dev->dev.parent;

	if (!parent)
		return buffer;

	driver = parent->driver;
	if (driver && driver->name)
		strlcpy(buffer, driver->name, len);
	return buffer;
}

static void __net_exit netdev_exit(struct net *net)
{
	kfree(net->dev_name_head);
	kfree(net->dev_index_head);
}

static struct pernet_operations __net_initdata netdev_net_ops = {
	.init = netdev_init,
	.exit = netdev_exit,
};

static void __net_exit default_device_exit(struct net *net)
{
	struct net_device *dev, *aux;
	/*
	 * Push all migratable network devices back to the
	 * initial network namespace
	 */
	rtnl_lock();
	for_each_netdev_safe(net, dev, aux) {
		int err;
		char fb_name[IFNAMSIZ];

		/* Ignore unmoveable devices (i.e. loopback) */
		if (dev->features & NETIF_F_NETNS_LOCAL)
			continue;

		/* Leave virtual devices for the generic cleanup */
		if (dev->rtnl_link_ops)
			continue;

		/* Push remaing network devices to init_net */
		snprintf(fb_name, IFNAMSIZ, "dev%d", dev->ifindex);
		err = dev_change_net_namespace(dev, &init_net, fb_name);
		if (err) {
			printk(KERN_EMERG "%s: failed to move %s to init_net: %d\n",
				__func__, dev->name, err);
			BUG();
		}
	}
	rtnl_unlock();
}

static void __net_exit default_device_exit_batch(struct list_head *net_list)
{
	/* At exit all network devices most be removed from a network
	 * namespace.  Do this in the reverse order of registeration.
	 * Do this across as many network namespaces as possible to
	 * improve batching efficiency.
	 */
	struct net_device *dev;
	struct net *net;
	LIST_HEAD(dev_kill_list);

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list) {
		for_each_netdev_reverse(net, dev) {
			if (dev->rtnl_link_ops)
				dev->rtnl_link_ops->dellink(dev, &dev_kill_list);
			else
				unregister_netdevice_queue(dev, &dev_kill_list);
		}
	}
	unregister_netdevice_many(&dev_kill_list);
	rtnl_unlock();
}

static struct pernet_operations __net_initdata default_device_ops = {
	.exit = default_device_exit,
	.exit_batch = default_device_exit_batch,
};

/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not
 *	present) and leaves us with a valid list of present and active devices.
 *
 */
 
/*
设备处理层的初始化函数.
在系统启动时，net_dev_init()的初始化优先级
是subsys_initcall，用来初始化相关
接口层，如注册记录相关统计信息的proc
文件，初始化每个CPU的softnet_data，注册网络
报文输入/输出软中断以及处理例程，注册
响应CPU状态变化的回调函数等。
 */
 
/*
 *       This is called single threaded during boot, so no need
 *       to take the rtnl semaphore.
 */
 
/*
设备物理层的初始化net_dev_init
TCP/IP协议栈初始化inet_init  其实传输层的协议初始化也在这里面
传输层初始化proto_init
套接口层初始化sock_init   netfilter_init在套接口层初始化的时候也初始化了
*/
static int __init net_dev_init(void)
{
	int i, rc = -ENOMEM;

	BUG_ON(!dev_boot_phase);

	/*
	注册/proc/net/dev和/proc/net/softnet_stat文件，
	只读文件，存放一些网络设备状态和统计信息
	*/
	if (dev_proc_init())
		goto out;

	/*
	netdev_kobject_init会创建/sys/class/net目录，
	在此目录下，每个已注册的网络设备都会有一个子目录。
	例如ifconfig里面的eth0信息都可以在这里面查看
	*/
	if (netdev_kobject_init()) 
        
		goto out;

    /*
	 * 初始化网络处理函数散列表ptype_base。这些处理函数
	 * 用来处理接收到的不同协议族报文。
	 */
	INIT_LIST_HEAD(&ptype_all);
	for (i = 0; i < PTYPE_HASH_SIZE; i++)
		//初始化网络处理函数散列表，这些处理函数用来处理接收到的不同协议族的报文
		INIT_LIST_HEAD(&ptype_base[i]);

    /*
	  * 注册在net命名空间的初始化和退出操作。
	  * netdev_net_ops中会分别初始化以名称和索引
	  * 为查找的链表
	  */
	if (register_pernet_subsys(&netdev_net_ops))
		goto out;

	/*
	 *	Initialise the packet receive queues.
	 */

    /*
	 * 初始化与CPU相关的接收队列。
	 * update:初始化每个CPU的softnet_data，包括
	 * 完成发送数据包的等待释放队列，以及
	 * 非NAPI驱动的输入队列、轮询函数
	 */
	for_each_possible_cpu(i) {//初始化与CPU接收相关的队列
		struct softnet_data *sd = &per_cpu(softnet_data, i);

		memset(sd, 0, sizeof(*sd));
		skb_queue_head_init(&sd->input_pkt_queue);
		skb_queue_head_init(&sd->process_queue);
		sd->completion_queue = NULL;
		INIT_LIST_HEAD(&sd->poll_list);
		sd->output_queue = NULL;
		sd->output_queue_tailp = &sd->output_queue;
#ifdef CONFIG_RPS
		sd->csd.func = rps_trigger_softirq;
		sd->csd.info = sd;
		sd->csd.flags = 0;
		sd->cpu = i;
#endif

		sd->backlog.poll = process_backlog;
		sd->backlog.weight = weight_p;
		sd->backlog.gro_list = NULL;
		sd->backlog.gro_count = 0;
	}

	dev_boot_phase = 0;//标识网络设备初始化已完成

	/* The loopback device is special if any other network devices
	 * is present in a network namespace the loopback device must
	 * be present. Since we now dynamically allocate and free the
	 * loopback device ensure this invariant is maintained by
	 * keeping the loopback device as the first device on the
	 * list of network devices.  Ensuring the loopback devices
	 * is the first device that appears and the last network device
	 * that disappears.
	 */
	if (register_pernet_device(&loopback_net_ops)) //注册网络设备"lo"，ifconfig里面的lo          注册网络命令空间设备，确保loopback设备在所有网络设备中最先出现和最后消失 
        
		goto out;

	if (register_pernet_device(&default_device_ops))
		goto out;

    /*
	 * 在软中断系统中注册两个软中断NET_TX_SOFTIRQ和
	 * NET_RX_SOFTIRQ，用于网络数据的发送和接收。因为
	 * 软中断的性能比较好，而网络数据的接收和发送
	 * 对性能要求比较高，因此将软中断作为下半部来
	 * 使用。
	 * update:注册网络报文输入/输出软中断及其处理例程。
	 *///下半部和上半部最大的不同是下半部是可中断的，而上半部是不可中断的，下半部几乎做了中断处理程序所有的事情，而且可以被新的中断打断！下半部则相对来说并不是非常紧急的，通常还是比较耗时的，因此由系统自行安排运行时机，不在中断服务上下文中执行。
	open_softirq(NET_TX_SOFTIRQ, net_tx_action);//注册两个软中断，用于网络数据的发送和接收
	open_softirq(NET_RX_SOFTIRQ, net_rx_action);

    /*
	 * 在通知链表上注册一个回调函数，用来响应
	 * CPU热插拔事件。一旦接到通知，CPU输入队列
	 * 中的包逐一由netif_rx()处理。
	 * update:注册响应CPU状态变化的回调函数。当CPU
	 * 状态发生变化时，会调用dev_cpu_callback()，来处理
	 * 状态发生变化的CPU的softnet_data中相关队列
	 */
	hotcpu_notifier(dev_cpu_callback, 0);//在通知链表上注册一个回调函数，用来相应CPU热插拔事件，一旦接到通知，CPU输入队列中的包逐一交给netif_rx处理

    /*
	 * 初始化目的路由缓存
	 */
	dst_init();

    /*
	 * 初始化网络设备层的组播模块，并在proc文件系统中
	 * 增加文件/proc/net/dev_mcast,用来存放内核中网络设备与
	 * IP组播相关的参数。
	 */
	dev_mcast_init();
	rc = 0;
out:
	return rc;
}

subsys_initcall(net_dev_init);//设备物理层的初始化

static int __init initialize_hashrnd(void)
{
	get_random_bytes(&hashrnd, sizeof(hashrnd));
	return 0;
}

late_initcall_sync(initialize_hashrnd);

