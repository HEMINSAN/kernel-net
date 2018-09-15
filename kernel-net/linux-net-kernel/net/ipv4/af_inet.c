/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PF_INET protocol family socket handler.
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Changes (see also sock.c)
 *
 *		piggy,
 *		Karl Knutson	:	Socket protocol table
 *		A.N.Kuznetsov	:	Socket death error in accept().
 *		John Richardson :	Fix non blocking error in connect()
 *					so sockets that fail to connect
 *					don't return -EINPROGRESS.
 *		Alan Cox	:	Asynchronous I/O support
 *		Alan Cox	:	Keep correct socket pointer on sock
 *					structures
 *					when accept() ed
 *		Alan Cox	:	Semantics of SO_LINGER aren't state
 *					moved to close when you look carefully.
 *					With this fixed and the accept bug fixed
 *					some RPC stuff seems happier.
 *		Niibe Yutaka	:	4.4BSD style write async I/O
 *		Alan Cox,
 *		Tony Gale 	:	Fixed reuse semantics.
 *		Alan Cox	:	bind() shouldn't abort existing but dead
 *					sockets. Stops FTP netin:.. I hope.
 *		Alan Cox	:	bind() works correctly for RAW sockets.
 *					Note that FreeBSD at least was broken
 *					in this respect so be careful with
 *					compatibility tests...
 *		Alan Cox	:	routing cache support
 *		Alan Cox	:	memzero the socket structure for
 *					compactness.
 *		Matt Day	:	nonblock connect error handler
 *		Alan Cox	:	Allow large numbers of pending sockets
 *					(eg for big web sites), but only if
 *					specifically application requested.
 *		Alan Cox	:	New buffering throughout IP. Used
 *					dumbly.
 *		Alan Cox	:	New buffering now used smartly.
 *		Alan Cox	:	BSD rather than common sense
 *					interpretation of listen.
 *		Germano Caronni	:	Assorted small races.
 *		Alan Cox	:	sendmsg/recvmsg basic support.
 *		Alan Cox	:	Only sendmsg/recvmsg now supported.
 *		Alan Cox	:	Locked down bind (see security list).
 *		Alan Cox	:	Loosened bind a little.
 *		Mike McLagan	:	ADD/DEL DLCI Ioctls
 *	Willy Konynenberg	:	Transparent proxying support.
 *		David S. Miller	:	New socket lookup architecture.
 *					Some other random speedups.
 *		Cyrus Durgin	:	Cleaned up file for kmod hacks.
 *		Andi Kleen	:	Fix inet_stream_connect TCP race.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
//PF_INET协议族相关参考http://jianshu.io/p/5d82a685b5b6可以知道应用层创建PF_INET socket过程中内核函数调用情况
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/netfilter_ipv4.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/ip_fib.h>
#include <net/inet_connection_sock.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/udplite.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <net/ipip.h>
#include <net/inet_common.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#ifdef CONFIG_IP_MROUTE
#include <linux/mroute.h>
#endif


/* The inetsw table contains everything that inet_create needs to
 * build a new socket.
 */
//当应用程序通过socket函数创建套接字的时候，通过协议类型protocol和套接字type在inet_create里面的inetsw中查找到对应的tcp_proto udp_proto raw_proto等
static struct list_head inetsw[SOCK_MAX];//见inet_init  inet_register_protosw(q); inetsw_array中的信息存放在该链表中的protocol，然后让struct socket的ops等指向对应的协议
static DEFINE_SPINLOCK(inetsw_lock);

struct ipv4_config ipv4_config;
EXPORT_SYMBOL(ipv4_config);

/* New destruction routine */

void inet_sock_destruct(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);

	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_error_queue);

	sk_mem_reclaim(sk);

	if (sk->sk_type == SOCK_STREAM && sk->sk_state != TCP_CLOSE) {
		pr_err("Attempt to release TCP socket in state %d %p\n",
		       sk->sk_state, sk);
		return;
	}
	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Attempt to release alive inet socket %p\n", sk);
		return;
	}

	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(atomic_read(&sk->sk_wmem_alloc));
	WARN_ON(sk->sk_wmem_queued);
	WARN_ON(sk->sk_forward_alloc);

	kfree(inet->opt);
	dst_release(rcu_dereference_check(sk->sk_dst_cache, 1));
	sk_refcnt_debug_dec(sk);
}
EXPORT_SYMBOL(inet_sock_destruct);

/*
 *	The routines beyond this point handle the behaviour of an AF_INET
 *	socket object. Mostly it punts to the subprotocols of IP to do
 *	the work.
 */

/*
 *	Automatically bind an unbound socket.
 */

static int inet_autobind(struct sock *sk)
{
	struct inet_sock *inet;
	/* We may need to bind the socket. */
	lock_sock(sk);
	inet = inet_sk(sk);
	if (!inet->inet_num) {
		if (sk->sk_prot->get_port(sk, 0)) {
			release_sock(sk);
			return -EAGAIN;
		}
		inet->inet_sport = htons(inet->inet_num);
	}
	release_sock(sk);
	return 0;
}

/*
 *	Move a socket into listening state.
 */
int inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	unsigned char old_state;
	int err;

	lock_sock(sk);

	err = -EINVAL;
	/*
	 * 检测调用listen的套接字的当前状态和类型。如果套接字状态
	 * 不是SS_UNCONNECTED，或套接字类型不是SOCK_STREAM，则不
	 * 允许进行监听操作，返回相应错误码
	 */
	if (sock->state != SS_UNCONNECTED || sock->type != SOCK_STREAM)
		goto out;

	old_state = sk->sk_state;

	if (!((1 << old_state) & (TCPF_CLOSE | TCPF_LISTEN)))
		goto out;

	/* Really, if the socket is already in listen state
	 * we can only allow the backlog to be adjusted.
	 */
	if (old_state != TCP_LISTEN) {
		err = inet_csk_listen_start(sk, backlog);
		if (err)
			goto out;
	}
	sk->sk_max_ack_backlog = backlog;
	err = 0;

out:
	release_sock(sk);
	return err;
}
EXPORT_SYMBOL(inet_listen);

u32 inet_ehash_secret __read_mostly;
EXPORT_SYMBOL(inet_ehash_secret);

/*
 * inet_ehash_secret must be set exactly once
 * Instead of using a dedicated spinlock, we (ab)use inetsw_lock
 */
void build_ehash_secret(void)
{
	u32 rnd;
	do {
		get_random_bytes(&rnd, sizeof(rnd));
	} while (rnd == 0);
	spin_lock_bh(&inetsw_lock);
	if (!inet_ehash_secret)
		inet_ehash_secret = rnd;
	spin_unlock_bh(&inetsw_lock);
}
EXPORT_SYMBOL(build_ehash_secret);

static inline int inet_netns_ok(struct net *net, int protocol)
{
	int hash;
	const struct net_protocol *ipprot;

	if (net_eq(net, &init_net))
		return 1;

	hash = protocol & (MAX_INET_PROTOS - 1);
	ipprot = rcu_dereference(inet_protos[hash]);

	if (ipprot == NULL)
		/* raw IP is OK */
		return 1;
	return ipprot->netns_ok;
}

/*
 *	Create an inet socket.
 */
//pf_inet的net_families[]为inet_family_ops，对应的套接口层ops参考inetsw_array中的inet_stream_ops inet_dgram_ops inet_sockraw_ops，传输层操作集分别为tcp_prot udp_prot raw_prot
//netlink的net_families[]netlink_family_ops，对应的套接口层ops为netlink_ops
static int inet_create(struct net *net, struct socket *sock, int protocol,
		       int kern)
{
	struct sock *sk;
	struct inet_protosw *answer;
	struct inet_sock *inet;
	struct proto *answer_prot;
	unsigned char answer_flags;
	char answer_no_check;
	int try_loading_module = 0;
	int err;

	if (unlikely(!inet_ehash_secret))
		if (sock->type != SOCK_RAW && sock->type != SOCK_DGRAM)
			build_ehash_secret();

	sock->state = SS_UNCONNECTED;//设置socket的状态为SS_UNCONNECTED；

	/* Look for the requested type/protocol pair. */
lookup_protocol:
	err = -ESOCKTNOSUPPORT;
	rcu_read_lock();
	list_for_each_entry_rcu(answer, &inetsw[sock->type], list) {

		err = 0;
		/* Check the non-wild match. */
		if (protocol == answer->protocol) {
			if (protocol != IPPROTO_IP)
				break;
		} else {
			/* Check for the two wild cases. */
			if (IPPROTO_IP == protocol) {
				protocol = answer->protocol;
				break;
			}
			if (IPPROTO_IP == answer->protocol)
				break;
		}
		err = -EPROTONOSUPPORT;
	}

	if (unlikely(err)) {
		if (try_loading_module < 2) {
			rcu_read_unlock();
			/*
			 * Be more specific, e.g. net-pf-2-proto-132-type-1
			 * (net-pf-PF_INET-proto-IPPROTO_SCTP-type-SOCK_STREAM)
			 */
			if (++try_loading_module == 1)
				request_module("net-pf-%d-proto-%d-type-%d",
					       PF_INET, protocol, sock->type);
			/*
			 * Fall back to generic, e.g. net-pf-2-proto-132
			 * (net-pf-PF_INET-proto-IPPROTO_SCTP)
			 */
			else
				request_module("net-pf-%d-proto-%d",
					       PF_INET, protocol);
			goto lookup_protocol;
		} else
			goto out_rcu_unlock;
	}

	err = -EPERM;
	if (sock->type == SOCK_RAW && !kern && !capable(CAP_NET_RAW))
		goto out_rcu_unlock;

	err = -EAFNOSUPPORT;
	/*
	 * 检查网络命名空间，检查指定的协议类型
	 * 是否已经添加，参见init_inet()，tcp协议对应
	 * 的net_protocol实例是tcp_protocol。
	 */
	if (!inet_netns_ok(net, protocol))
		goto out_rcu_unlock;

	sock->ops = answer->ops;
	answer_prot = answer->prot;
	answer_no_check = answer->no_check;
	answer_flags = answer->flags;
	rcu_read_unlock();

	WARN_ON(answer_prot->slab == NULL);

	err = -ENOBUFS;
	/*
	 * 分配sock实例并初始化，如果是TCP协议，
	 * 则实际分配的大小为sizeof(tcp_sock)。
	 */
	sk = sk_alloc(net, PF_INET, GFP_KERNEL, answer_prot);
	if (sk == NULL)
		goto out;

	err = 0;
	sk->sk_no_check = answer_no_check;
	if (INET_PROTOSW_REUSE & answer_flags)
		sk->sk_reuse = 1;

	inet = inet_sk(sk);
	inet->is_icsk = (INET_PROTOSW_ICSK & answer_flags) != 0;

	if (SOCK_RAW == sock->type) {
		inet->inet_num = protocol;
		if (IPPROTO_RAW == protocol)
			inet->hdrincl = 1;
	}

	if (ipv4_config.no_pmtu_disc)
		inet->pmtudisc = IP_PMTUDISC_DONT;
	else
		inet->pmtudisc = IP_PMTUDISC_WANT;

	inet->inet_id = 0;

	sock_init_data(sock, sk);

	sk->sk_destruct	   = inet_sock_destruct;
	sk->sk_protocol	   = protocol;
	sk->sk_backlog_rcv = sk->sk_prot->backlog_rcv;

	inet->uc_ttl	= -1;
	inet->mc_loop	= 1;
	inet->mc_ttl	= 1;
	inet->mc_all	= 1;
	inet->mc_index	= 0;
	inet->mc_list	= NULL;

	sk_refcnt_debug_inc(sk);

	if (inet->inet_num) {
		/* It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically
		 * shares.
		 */
		inet->inet_sport = htons(inet->inet_num);
		/* Add to protocol hash chains. */
		sk->sk_prot->hash(sk);
	}

	if (sk->sk_prot->init) {
		err = sk->sk_prot->init(sk);
		if (err)
			sk_common_release(sk);
	}
out:
	return err;
out_rcu_unlock:
	rcu_read_unlock();
	goto out;
}


/*
 *	The peer socket should always be NULL (or else). When we call this
 *	function we are destroying the object and from then on nobody
 *	should refer to it.
 *///从sock_release走到这里，见sock_release
int inet_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		long timeout;

		sock_rps_reset_flow(sk);

		/* Applications forget to leave groups before exiting */
		ip_mc_drop_socket(sk);

		/* If linger is set, we don't return until the close
		 * is complete.  Otherwise we return immediately. The
		 * actually closing is done the same either way.
		 *
		 * If the close is due to the process exiting, we never
		 * linger..
		 */
		timeout = 0;
		/*
		 * 如果设置了SO_LINGER选项，则关闭连接的时候会等待
		 * 关闭完成，否则会立即返回。但是如果是进程退出，
		 * 则会忽略SO_LINGER选项
		 */
		if (sock_flag(sk, SOCK_LINGER) &&
		    !(current->flags & PF_EXITING))
			timeout = sk->sk_lingertime;
		sock->sk = NULL;
		sk->sk_prot->close(sk, timeout);
	}
	return 0;
}
EXPORT_SYMBOL(inet_release);

/* It is off by default, see below. */
int sysctl_ip_nonlocal_bind __read_mostly;
EXPORT_SYMBOL(sysctl_ip_nonlocal_bind);

int inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *addr = (struct sockaddr_in *)uaddr;
	struct sock *sk = sock->sk;
	struct inet_sock *inet = inet_sk(sk);
	unsigned short snum;
	int chk_addr_ret;
	int err;

	/* If the socket has its own bind function then use it. (RAW) */
	/*
	 * 如果是TCP套接字，sk->sk_prot指向的是tcp_prot，在
	 * inet_create()中调用的sk_alloc()函数中初始化。由于
	 * tcp_prot中没有设置bind接口，因此判断条件不成立。 现在只有RAW才有
	 */
	if (sk->sk_prot->bind) {
		err = sk->sk_prot->bind(sk, uaddr, addr_len);
		goto out;
	}
	err = -EINVAL;
	if (addr_len < sizeof(struct sockaddr_in))
		goto out;

	chk_addr_ret = inet_addr_type(sock_net(sk), addr->sin_addr.s_addr);//检查原地址类型，组播 广播 单薄等

	/* Not specified by any standard per-se, however it breaks too
	 * many applications when removed.  It is unfortunate since
	 * allowing applications to make a non-local bind solves
	 * several problems with systems using dynamic addressing.
	 * (ie. your servers still start up even if your ISDN link
	 *  is temporarily down)
	 */
	err = -EADDRNOTAVAIL;
	if (!sysctl_ip_nonlocal_bind &&
	    !(inet->freebind || inet->transparent) &&
	    addr->sin_addr.s_addr != htonl(INADDR_ANY) &&
	    chk_addr_ret != RTN_LOCAL &&
	    chk_addr_ret != RTN_MULTICAST &&
	    chk_addr_ret != RTN_BROADCAST)
		goto out;

	snum = ntohs(addr->sin_port);
	err = -EACCES;
	/*
	 * 如果绑定的端口号小于1024(保留端口号)，但是
	 * 当前用户没有CAP_NET_BIND_SERVICE权限，则返回EACCESS错误。
	 */
	if (snum && snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE)) //检查是否可以使用1024以下的端口
		goto out;

	/*      We keep a pair of addresses. rcv_saddr is the one
	 *      used by hash lookups, and saddr is used for transmit.
	 *
	 *      In the BSD API these are the same except where it
	 *      would be illegal to use them (multicast/broadcast) in
	 *      which case the sending device address is used.
	 */
	lock_sock(sk);

	/* Check these errors (active socket, double bind). */
	/*
	 * 如果套接字状态不是TCP_CLOSE(套接字的初始状态，参见
	 * sock_init_data()函数)，或者已经绑定过，则返回EINVAL错误。
	 */
	err = -EINVAL;
	if (sk->sk_state != TCP_CLOSE || inet->inet_num)
		goto out_release_sock;

	inet->inet_rcv_saddr = inet->inet_saddr = addr->sin_addr.s_addr;
	if (chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		inet->inet_saddr = 0;  /* Use device */

	/* Make sure we are allowed to bind here. */
	if (sk->sk_prot->get_port(sk, snum)) {
		inet->inet_saddr = inet->inet_rcv_saddr = 0;
		err = -EADDRINUSE;
		goto out_release_sock;
	}

	if (inet->inet_rcv_saddr)
		sk->sk_userlocks |= SOCK_BINDADDR_LOCK;
	if (snum)
		sk->sk_userlocks |= SOCK_BINDPORT_LOCK;
	inet->inet_sport = htons(inet->inet_num);
	inet->inet_daddr = 0;
	inet->inet_dport = 0;
	sk_dst_reset(sk);
	err = 0;
out_release_sock:
	release_sock(sk);
out:
	return err;
}
EXPORT_SYMBOL(inet_bind);

int inet_dgram_connect(struct socket *sock, struct sockaddr * uaddr,
		       int addr_len, int flags)
{
	struct sock *sk = sock->sk;

	if (addr_len < sizeof(uaddr->sa_family))
		return -EINVAL;
	if (uaddr->sa_family == AF_UNSPEC)
		return sk->sk_prot->disconnect(sk, flags);

	if (!inet_sk(sk)->inet_num && inet_autobind(sk))
		return -EAGAIN;
	return sk->sk_prot->connect(sk, (struct sockaddr *)uaddr, addr_len);
}
EXPORT_SYMBOL(inet_dgram_connect);

static long inet_wait_for_connect(struct sock *sk, long timeo)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

	/* Basic assumption: if someone sets sk->sk_err, he _must_
	 * change state of the socket from TCP_SYN_*.
	 * Connect() does not allow to get error notifications
	 * without closing the socket.
	 */
	while ((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV)) {
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		if (signal_pending(current) || !timeo)
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	}
	finish_wait(sk_sleep(sk), &wait);
	return timeo;
}

/*
 *	Connect to a remote host. There is regrettably still a little
 *	TCP 'magic' in here.
 */
int inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	int err;
	long timeo;

	if (addr_len < sizeof(uaddr->sa_family))
		return -EINVAL;

	lock_sock(sk);

	if (uaddr->sa_family == AF_UNSPEC) {
		err = sk->sk_prot->disconnect(sk, flags);
		sock->state = err ? SS_DISCONNECTING : SS_UNCONNECTED;
		goto out;
	}

	switch (sock->state) {
	default:
		err = -EINVAL;
		goto out;
	case SS_CONNECTED:
		err = -EISCONN;
		goto out;
	case SS_CONNECTING:
		err = -EALREADY;
		/* Fall out of switch with err, set for this state */
		break;
	case SS_UNCONNECTED://只有在这个状态下才能进行连接
		err = -EISCONN;
		if (sk->sk_state != TCP_CLOSE)
			goto out;

		err = sk->sk_prot->connect(sk, uaddr, addr_len);
		if (err < 0)
			goto out;

		sock->state = SS_CONNECTING;

		/* Just entered SS_CONNECTING state; the only
		 * difference is that return value in non-blocking
		 * case is EINPROGRESS, rather than EALREADY.
		 */
		err = -EINPROGRESS;
		break;
	}

	timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);

	if ((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV)) {
		/* Error code is set above */
		if (!timeo || !inet_wait_for_connect(sk, timeo))
			goto out;

		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			goto out;
	}

	/* Connection was closed by RST, timeout, ICMP error
	 * or another process disconnected us.
	 */
	if (sk->sk_state == TCP_CLOSE) //在前面的sk->sk_prot->connect过程中，这个状态会根据实际情况变化
		goto sock_error;

	/* sk->sk_err may be not zero now, if RECVERR was ordered by user
	 * and error was received after socket entered established state.
	 * Hence, it is handled normally after connect() return successfully.
	 */

	sock->state = SS_CONNECTED;
	err = 0;
out:
	release_sock(sk);
	return err;

sock_error:
	err = sock_error(sk) ? : -ECONNABORTED;
	sock->state = SS_UNCONNECTED;
	if (sk->sk_prot->disconnect(sk, flags))
		sock->state = SS_DISCONNECTING;
	goto out;
}
EXPORT_SYMBOL(inet_stream_connect);

/*
 *	Accept a pending connection. The TCP layer now gives BSD semantics.
 */

int inet_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk1 = sock->sk;
	int err = -EINVAL;

	/*
	 * 调用accept的传输层接口实现inet_csk_accept()获取已完成
	 * 连接(被接受)的传输控制块,这是在三次握手的时候，会创建一个传输控制块
	 */
	struct sock *sk2 = sk1->sk_prot->accept(sk1, flags, &err);//这个是inet_csk_accept的时候从reqsk_queue_get_child队列中取出来的，是三次握手第三步的时候开辟的sk空间

	if (!sk2)
		goto do_err;

	lock_sock(sk2);

	WARN_ON(!((1 << sk2->sk_state) &
		  (TCPF_ESTABLISHED | TCPF_CLOSE_WAIT | TCPF_CLOSE)));

    /*
	 * 如果accept成功，则需调用sock_graft()把子套接字和传输控制块关联
	 * 起来以便这两者之间相互索引
	 */
	sock_graft(sk2, newsock);//把newsock和sk2关联起来

	newsock->state = SS_CONNECTED;
	err = 0;
	release_sock(sk2);
do_err:
	return err;
}
EXPORT_SYMBOL(inet_accept);


/*
 *	This does both peername and sockname.
 */ //获取本端或者对端的地址和端口   peer=1获取对端的，peer=0获取本端的
int inet_getname(struct socket *sock, struct sockaddr *uaddr,
			int *uaddr_len, int peer)
{
	struct sock *sk		= sock->sk;
	struct inet_sock *inet	= inet_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, uaddr);

	sin->sin_family = AF_INET;  inet->
	if (peer) {
		if (!inet->inet_dport ||
		    (((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_SYN_SENT)) &&
		     peer == 1))
			return -ENOTCONN;
		sin->sin_port = inet->inet_dport;
		sin->sin_addr.s_addr = inet->inet_daddr;
	} else {
		__be32 addr = inet->inet_rcv_saddr;
		if (!addr)
			addr = inet->inet_saddr;
		sin->sin_port = inet->inet_sport;
		sin->sin_addr.s_addr = addr;
	}
	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
	*uaddr_len = sizeof(*sin);
	return 0;
}
EXPORT_SYMBOL(inet_getname);

int inet_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
		 size_t size)
{
	struct sock *sk = sock->sk;

	sock_rps_record_flow(sk);

	/* We may need to bind the socket. */
	if (!inet_sk(sk)->inet_num && inet_autobind(sk))
		return -EAGAIN;

	return sk->sk_prot->sendmsg(iocb, sk, msg, size);
}
EXPORT_SYMBOL(inet_sendmsg);

static ssize_t inet_sendpage(struct socket *sock, struct page *page, int offset,
			     size_t size, int flags)
{
	struct sock *sk = sock->sk;

	sock_rps_record_flow(sk);

	/* We may need to bind the socket. */
	if (!inet_sk(sk)->inet_num && inet_autobind(sk))
		return -EAGAIN;

	if (sk->sk_prot->sendpage)
		return sk->sk_prot->sendpage(sk, page, offset, size, flags);
	return sock_no_sendpage(sock, page, offset, size, flags);
}

int inet_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
		 size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	sock_rps_record_flow(sk);

	err = sk->sk_prot->recvmsg(iocb, sk, msg, size, flags & MSG_DONTWAIT,
				   flags & ~MSG_DONTWAIT, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}
EXPORT_SYMBOL(inet_recvmsg);

int inet_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = 0;

	/* This should really check to make sure
	 * the socket is a TCP socket. (WHY AC...)
	 */
	how++; /* maps 0->1 has the advantage of making bit 1 rcvs and
		       1->2 bit 2 snds.
		       2->3 */
	if ((how & ~SHUTDOWN_MASK) || !how)	/* MAXINT->0 */
		return -EINVAL;

	lock_sock(sk);
	if (sock->state == SS_CONNECTING) {
		if ((1 << sk->sk_state) &
		    (TCPF_SYN_SENT | TCPF_SYN_RECV | TCPF_CLOSE))
			sock->state = SS_DISCONNECTING;
		else
			sock->state = SS_CONNECTED;
	}

	switch (sk->sk_state) {
	case TCP_CLOSE:
		err = -ENOTCONN;
		/* Hack to wake up other listeners, who can poll for
		   POLLHUP, even on eg. unconnected UDP sockets -- RR */
	default:
		sk->sk_shutdown |= how;
		if (sk->sk_prot->shutdown)
			sk->sk_prot->shutdown(sk, how);
		break;

	/* Remaining two branches are temporary solution for missing
	 * close() in multithreaded environment. It is _not_ a good idea,
	 * but we have no choice until close() is repaired at VFS level.
	 */
	case TCP_LISTEN:
		if (!(how & RCV_SHUTDOWN))
			break;
		/* Fall through */
	case TCP_SYN_SENT:
		err = sk->sk_prot->disconnect(sk, O_NONBLOCK);
		sock->state = err ? SS_DISCONNECTING : SS_UNCONNECTED;
		break;
	}

	/* Wake up anyone sleeping in poll. */
	sk->sk_state_change(sk);
	release_sock(sk);
	return err;
}
EXPORT_SYMBOL(inet_shutdown);

/*
 *	ioctl() calls you can issue on an INET socket. Most of these are
 *	device configuration and stuff and very rarely used. Some ioctls
 *	pass on to the socket itself.
 *
 *	NOTE: I like the idea of a module for the config stuff. ie ifconfig
 *	loads the devconfigure module does its configuring and unloads it.
 *	There's a good 20K of config code hanging around the kernel.
 */

int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err = 0;
	struct net *net = sock_net(sk);

	switch (cmd) {
	case SIOCGSTAMP:
		err = sock_get_timestamp(sk, (struct timeval __user *)arg);
		break;
	case SIOCGSTAMPNS:
		err = sock_get_timestampns(sk, (struct timespec __user *)arg);
		break;
		/* 添加 删除 路由操作 */
	case SIOCADDRT:
	case SIOCDELRT:
	case SIOCRTMSG:
		err = ip_rt_ioctl(net, cmd, (void __user *)arg);
		break;

		//ARP添加 删除 设置
	case SIOCDARP:
	case SIOCGARP:
	case SIOCSARP:
		err = arp_ioctl(net, cmd, (void __user *)arg);
		break;

		/* DEV设备接口操作 */
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFPFLAGS:
	case SIOCGIFPFLAGS:
	case SIOCSIFFLAGS:
		err = devinet_ioctl(net, cmd, (void __user *)arg);
		break;
		
	default://对具体的某个协议的套接口ioctl操作
		if (sk->sk_prot->ioctl)
			err = sk->sk_prot->ioctl(sk, cmd, arg);
		else
			err = -ENOIOCTLCMD;
		break;
	}
	return err;
}
EXPORT_SYMBOL(inet_ioctl);

//TCP协议z在INET层操作集inet_stream_ops,
const struct proto_ops inet_stream_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = inet_getname,
	.poll		   = tcp_poll,
	.ioctl		   = inet_ioctl, //通过sock_do_ioctl走到这里
	.listen		   = inet_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt, //解析应用程序的setsockopt，并内核生效
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = tcp_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = tcp_sendpage,
	.splice_read	   = tcp_splice_read,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};
EXPORT_SYMBOL(inet_stream_ops);

//UDP协议在INET层操作集inet_dgram_ops
const struct proto_ops inet_dgram_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = inet_getname,
	.poll		   = udp_poll,
	.ioctl		   = inet_ioctl,
	.listen		   = sock_no_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = inet_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};
EXPORT_SYMBOL(inet_dgram_ops);

/*
 * For SOCK_RAW sockets; should be the same as inet_dgram_ops but without
 * udp_poll
 */
static const struct proto_ops inet_sockraw_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = inet_getname,
	.poll		   = datagram_poll,
	.ioctl		   = inet_ioctl,
	.listen		   = sock_no_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = inet_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

/*
ops->create在应用程序创建套接字的时候，引起系统调用，
应用层创建套接字的时候，内核系统调用 sock_create 然后执行该函数
从而在函数 __sock_create 中执行 ops->create 
netlink 为 netlink_family_ops
pf_inet的net_families[]为inet_family_ops，
对应的套接口层ops参考inetsw_array中的inet_stream_ops inet_dgram_ops inet_sockraw_ops，
传输层操作集分别为 tcp_prot udp_prot raw_prot
netlink的net_families[]netlink_family_ops，对应的套接口层ops为netlink_ops
family协议族通过sock_register注册  传输层接口tcp_prot udp_prot netlink_prot等
通过proto_register注册   IP层接口通过inet_add_protocol(&icmp_protocol等注册
这些组成过程参考inet_init函数
*/
static const struct net_proto_family inet_family_ops = {//操作集参考inetsw_array  
	.family = PF_INET,
	.create = inet_create,
	.owner	= THIS_MODULE,
};

/* Upon startup we insert all the elements in inetsw_array[] into
 * the linked list inetsw.
 在初始化的时候我们会将上面数组中的的元素按套接字类型插入static struct list_head inetsw[SOCK_MAX];链表数组中
 */  //参见樊东东P211 
 /* 
  * inetsw_array数组包含三个inet_protosw结构的实例，分别对应
  * TCP、UDP和原始套接字。在Internet协议族初始化函数inet_init()中
  * 调用inet_register_protosw()将inetsw_array数组中
  * 的inet_protosw结构实例，以其type值为key组织到散列表inetsw中，
  * 也就是说各协议族中type值相同而protocol值不同的inet_protosw结构
  * 实例，在inetsw散列表中以type为关键字连接成链表，通过inetsw
  * 散列表可以找到所有协议族的inet_protosw结构实例。
  */ ////ipv4_specific是TCP传输层到网络层数据发送以及TCP建立过程的真正OPS，在tcp_prot->init中被赋值给inet_connection_sock->icsk_af_ops
static struct inet_protosw inetsw_array[] =   //这个和应用层创建套接字相关，个人我理解是属于套接口层,为了把套接口层和传输层衔接起来(tcp_protocol udp_protol icmp_protocol)
{
	{  
		.type =       SOCK_STREAM, //在inet_create的时候，用它做为关键字，把下面这几个成员联系在一起
		.protocol =   IPPROTO_TCP,

    //tcp_prot udp proto raw_proto头添加到的proto_list中，通过遍历该链表就可以知道有哪些传输层协议添加到该链表中
//协议最终都是通过inet_init中的proto_register添加到proto_list链表中的。family协议族通过sock_register注册  传输层接口tcp_prot udp_prot netlink_prot等通过proto_register注册   IP层接口通过inet_add_protocol(&icmp_protocol等注册 ，这些组成过程参考inet_init函数
		.prot =       &tcp_prot,//传输层操作集  在inet_create中的sk_alloc中赋值  先执行ops中的函数，然后执行prot中对应的函数 proto结构为网络接口层，结构中的操作实现传输层的操作和从传输层到网络层调用的跳转，在proto结构中的某些成员跟proto_ops结构中的成员对应，比如connect()等
		.ops =        &inet_stream_ops,//套接口层操作集,也就是协议族操作集 用来区分协议族(netlink family(ops为netlink_ops)或者 inet family)  ops在创建套接字的时候被赋值，例如netlink赋值的地方在__netlink_create  pf_net赋值的地方在inet_create中
		.no_check =   0, //为0表示始终进行校验和操作
		.flags =      INET_PROTOSW_PERMANENT |
			      INET_PROTOSW_ICSK,
	},

	{
		.type =       SOCK_DGRAM,
		.protocol =   IPPROTO_UDP,
		.prot =       &udp_prot,//传输层操作集  在inet_create中的sk_alloc中赋值  先执行ops中的函数，然后执行prot中对应的函数
		.ops =        &inet_dgram_ops,//套接口层操作集 用来区分协议族(netlink family(ops为netlink_ops)或者 inet family)  ops在创建套接字的时候被赋值，例如netlink赋值的地方在__netlink_create  pf_net赋值的地方在inet_create中
		.no_check =   UDP_CSUM_DEFAULT,
		.flags =      INET_PROTOSW_PERMANENT,
       },


       {
	       .type =       SOCK_RAW,  //原始套接口
	       .protocol =   IPPROTO_IP,	/* wild card */
	       .prot =       &raw_prot,//传输层操作集  在inet_create中的sk_alloc中赋值  先执行ops中的函数，然后执行prot中对应的函数
	       .ops =        &inet_sockraw_ops,//套接口层操作集  用来区分协议族(netlink family(ops为netlink_ops)或者 inet family)  ops在创建套接字的时候被赋值，例如netlink赋值的地方在__netlink_create  pf_net赋值的地方在inet_create中
	       .no_check =   UDP_CSUM_DEFAULT,
	       .flags =      INET_PROTOSW_REUSE,
       }
};

#define INETSW_ARRAY_LEN ARRAY_SIZE(inetsw_array)
/*
inet_register_protosw把数组inetsw_array中定义的套接字类型全部注册到inetsw数组中；其中相同套接字类型，不同协议类型的套接字
通过链表存放在到inetsw数组中，以套接字类型为索引，在系统实际使用的时候，只使用inetsw，而不使用 inetsw_array；
*/
void inet_register_protosw(struct inet_protosw *p)
{
	struct list_head *lh;
	struct inet_protosw *answer;
	int protocol = p->protocol;
	struct list_head *last_perm;

	spin_lock_bh(&inetsw_lock);

	if (p->type >= SOCK_MAX)
		goto out_illegal;

	/* If we are trying to override a permanent protocol, bail. */
	answer = NULL;
	last_perm = &inetsw[p->type];
	list_for_each(lh, &inetsw[p->type]) {
		answer = list_entry(lh, struct inet_protosw, list);

		/* Check only the non-wild match. */
		if (INET_PROTOSW_PERMANENT & answer->flags) {
			if (protocol == answer->protocol)
				break;
			last_perm = lh;
		}

		answer = NULL;
	}
	if (answer)
		goto out_permanent;

	/* Add the new entry after the last permanent entry if any, so that
	 * the new entry does not override a permanent entry when matched with
	 * a wild-card protocol. But it is allowed to override any existing
	 * non-permanent entry.  This means that when we remove this entry, the
	 * system automatically returns to the old behavior.
	 */
	list_add_rcu(&p->list, last_perm);
out:
	spin_unlock_bh(&inetsw_lock);

	return;

out_permanent:
	printk(KERN_ERR "Attempt to override permanent protocol %d.\n",
	       protocol);
	goto out;

out_illegal:
	printk(KERN_ERR
	       "Ignoring attempt to register invalid socket type %d.\n",
	       p->type);
	goto out;
}
EXPORT_SYMBOL(inet_register_protosw);

void inet_unregister_protosw(struct inet_protosw *p)
{
	if (INET_PROTOSW_PERMANENT & p->flags) {
		printk(KERN_ERR
		       "Attempt to unregister permanent protocol %d.\n",
		       p->protocol);
	} else {
		spin_lock_bh(&inetsw_lock);
		list_del_rcu(&p->list);
		spin_unlock_bh(&inetsw_lock);

		synchronize_net();
	}
}
EXPORT_SYMBOL(inet_unregister_protosw);

/*
 *      Shall we try to damage output packets if routing dev changes?
 */

int sysctl_ip_dynaddr __read_mostly;

static int inet_sk_reselect_saddr(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	int err;
	struct rtable *rt;
	__be32 old_saddr = inet->inet_saddr;
	__be32 new_saddr;
	__be32 daddr = inet->inet_daddr;

	if (inet->opt && inet->opt->srr)
		daddr = inet->opt->faddr;

	/* Query new route. */
	err = ip_route_connect(&rt, daddr, 0,
			       RT_CONN_FLAGS(sk),
			       sk->sk_bound_dev_if,
			       sk->sk_protocol,
			       inet->inet_sport, inet->inet_dport, sk, 0);
	if (err)
		return err;

	sk_setup_caps(sk, &rt->u.dst);

	new_saddr = rt->rt_src;

	if (new_saddr == old_saddr)
		return 0;

	if (sysctl_ip_dynaddr > 1) {
		printk(KERN_INFO "%s(): shifting inet->saddr from %pI4 to %pI4\n",
		       __func__, &old_saddr, &new_saddr);
	}

	inet->inet_saddr = inet->inet_rcv_saddr = new_saddr;

	/*
	 * XXX The only one ugly spot where we need to
	 * XXX really change the sockets identity after
	 * XXX it has entered the hashes. -DaveM
	 *
	 * Besides that, it does not check for connection
	 * uniqueness. Wait for troubles.
	 */
	__sk_prot_rehash(sk);
	return 0;
}
/*
 * 为该套接字建立路由
 */
int inet_sk_rebuild_header(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct rtable *rt = (struct rtable *)__sk_dst_check(sk, 0);
	__be32 daddr;
	int err;

	/* Route is OK, nothing to do. */
	if (rt)
		return 0;

	/* Reroute. */
	daddr = inet->inet_daddr;
	if (inet->opt && inet->opt->srr)
		daddr = inet->opt->faddr;
{
	struct flowi fl = {
		.oif = sk->sk_bound_dev_if,
		.mark = sk->sk_mark,
		.nl_u = {
			.ip4_u = {
				.daddr	= daddr,
				.saddr	= inet->inet_saddr,
				.tos	= RT_CONN_FLAGS(sk),
			},
		},
		.proto = sk->sk_protocol,
		.flags = inet_sk_flowi_flags(sk),
		.uli_u = {
			.ports = {
				.sport = inet->inet_sport,
				.dport = inet->inet_dport,
			},
		},
	};

	security_sk_classify_flow(sk, &fl);
	err = ip_route_output_flow(sock_net(sk), &rt, &fl, sk, 0);
}
	if (!err)
		sk_setup_caps(sk, &rt->u.dst);
	else {
		/* Routing failed... */
		sk->sk_route_caps = 0;
		/*
		 * Other protocols have to map its equivalent state to TCP_SYN_SENT.
		 * DCCP maps its DCCP_REQUESTING state to TCP_SYN_SENT. -acme
		 */
		if (!sysctl_ip_dynaddr ||
		    sk->sk_state != TCP_SYN_SENT ||
		    (sk->sk_userlocks & SOCK_BINDADDR_LOCK) ||
		    (err = inet_sk_reselect_saddr(sk)) != 0)
			sk->sk_err_soft = -err;
	}

	return err;
}
EXPORT_SYMBOL(inet_sk_rebuild_header);

/*
 * inet_gso_send_check()是IP层gso_send_check接口的实现
 * 函数，在分段之前对伪首部进行校验和
 * 计算。
 *///见ip_packet_type
static int inet_gso_send_check(struct sk_buff *skb)
{
	struct iphdr *iph;
	const struct net_protocol *ops;
	int proto;
	int ihl;
	int err = -EINVAL;

	/*
	 * 待分段数据包长度至少大于IP首部
	 * 长度，否则必定是无效的IP数据包。
	 */
	if (unlikely(!pskb_may_pull(skb, sizeof(*iph))))
		goto out;

	/*
	 * 检测IP首部中长度字段是否有效。
	 */
	iph = ip_hdr(skb);
	ihl = iph->ihl * 4;
	if (ihl < sizeof(*iph))
		goto out;

	/*
	 * 再次通过IP首部中长度字段来检测
	 * IP数据包长度是否有效。
	 */
	if (unlikely(!pskb_may_pull(skb, ihl)))
		goto out;

	/*
	 * 取出IP首部中协议字段值，用于定位
	 * 与之对应的传输层协议接口。
	 */
	__skb_pull(skb, ihl);
	skb_reset_transport_header(skb);
	iph = ip_hdr(skb);
	proto = iph->protocol & (MAX_INET_PROTOS - 1);
	err = -EPROTONOSUPPORT;

	/*
	 * 根据IP首部中协议字段值，获取对应的传输层
	 * 协议接口，然后通过gso_send_check接口(参见
	 * tcp_v4_gso_send_check())，重新对TCP段伪首部进行校验
	 * 和计算。
	 */
	rcu_read_lock();
	ops = rcu_dereference(inet_protos[proto]);
	if (likely(ops && ops->gso_send_check)) //参考这里struct net_protocol 
		err = ops->gso_send_check(skb);?/ip_packet_type
	rcu_read_unlock();

out:
	/*
	 * 返回操作结果。
	 */
	return err;
}

/*
 * inet_gso_segment()是IP层gso_segment接口的实现函数，根据
 * 分段数据包获取对应的传输层接口，并完成GSO
 * 分段后，对分段后的IP数据包重新计算校验和。
 */
static struct sk_buff *inet_gso_segment(struct sk_buff *skb, int features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	struct iphdr *iph;
	const struct net_protocol *ops;
	int proto;
	int ihl;
	int id;
	unsigned int offset = 0;

	if (!(features & NETIF_F_V4_CSUM))
		features &= ~NETIF_F_SG;

	/*
	 * 检验待软GSO分段的SKB，其gsotype是否
	 * 存在其他非法的值。
	 */
	if (unlikely(skb_shinfo(skb)->gso_type &
		     ~(SKB_GSO_TCPV4 |
		       SKB_GSO_UDP |
		       SKB_GSO_DODGY |
		       SKB_GSO_TCP_ECN |
		       0)))
		goto out;

	/*
	 * 待分段数据包长度至少大于IP首部长度，
	 * 否则必定是无效IP数据包。
	 */
	if (unlikely(!pskb_may_pull(skb, sizeof(*iph))))
		goto out;

	/*
	 * 检测IP首部中长度字段是否有效。
	 */
	iph = ip_hdr(skb);
	ihl = iph->ihl * 4;
	if (ihl < sizeof(*iph))
		goto out;

	/*
	 * 再次通过IP首部中长度字段检测
	 * IP数据包长度是否有效。
	 */
	if (unlikely(!pskb_may_pull(skb, ihl)))
		goto out;

	/*
	 * 取出IP首部中ID字段值，用于分片数据包
	 * 的IP首部中ID的基础值。取出IP首部中的
	 * 协议字段值，用于定位与之对应的传输
	 * 层协议接口。
	 */
	__skb_pull(skb, ihl);
	skb_reset_transport_header(skb);
	iph = ip_hdr(skb);
	id = ntohs(iph->id);
	proto = iph->protocol & (MAX_INET_PROTOS - 1);
	segs = ERR_PTR(-EPROTONOSUPPORT);

	/*
	 * 根据IP首部中协议字段值，获取对应的
	 * 传输层协议接口，然后通过gso_segment接口
	 * (参见tcp_tso_segment())，对TCP段进行软GSO分段，
	 * 分段得到的新段通过next链表在原先的段
	 * 之后。
	 */
	rcu_read_lock();
	ops = rcu_dereference(inet_protos[proto]);
	if (likely(ops && ops->gso_segment))//icmp_protocol、udp_protocol、tcp_protocol和igmp_protocol
		segs = ops->gso_segment(skb, features);?/分段得到的新段通过next链表在原先的段之后
	rcu_read_unlock();

	if (!segs || IS_ERR(segs))
		goto out;

	/*
	 * 循环对分段得到所有的段进行IP校验和计算。
	 */
	skb = segs;
	do {
		iph = ip_hdr(skb);
		if (proto == IPPROTO_UDP) {
			iph->id = htons(id);
			iph->frag_off = htons(offset >> 3);
			if (skb->next != NULL)
				iph->frag_off |= htons(IP_MF);
			offset += (skb->len - skb->mac_len - iph->ihl * 4);
		} else
			iph->id = htons(id++);
		iph->tot_len = htons(skb->len - skb->mac_len);
		iph->check = 0;
		iph->check = ip_fast_csum(skb_network_header(skb), iph->ihl);
	} while ((skb = skb->next));

out:
	/*
	 * 最后，如果分段成功，则返回分段后SKB链表
	 * 中的第一个SKB，否则返回相应的错误码。
	 */
	return segs;
}

static struct sk_buff **inet_gro_receive(struct sk_buff **head,
					 struct sk_buff *skb)
{
	const struct net_protocol *ops;
	struct sk_buff **pp = NULL;
	struct sk_buff *p;
	struct iphdr *iph;
	unsigned int hlen;
	unsigned int off;
	unsigned int id;
	int flush = 1;
	int proto;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*iph);
	iph = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, hlen)) {
		iph = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!iph))
			goto out;
	}

	proto = iph->protocol & (MAX_INET_PROTOS - 1);

	rcu_read_lock();
	ops = rcu_dereference(inet_protos[proto]);
	if (!ops || !ops->gro_receive)
		goto out_unlock;

	if (*(u8 *)iph != 0x45)
		goto out_unlock;

	if (unlikely(ip_fast_csum((u8 *)iph, iph->ihl)))
		goto out_unlock;

	id = ntohl(*(__be32 *)&iph->id);
	flush = (u16)((ntohl(*(__be32 *)iph) ^ skb_gro_len(skb)) | (id ^ IP_DF));
	id >>= 16;

	for (p = *head; p; p = p->next) {
		struct iphdr *iph2;

		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		iph2 = ip_hdr(p);

		if ((iph->protocol ^ iph2->protocol) |
		    (iph->tos ^ iph2->tos) |
		    ((__force u32)iph->saddr ^ (__force u32)iph2->saddr) |
		    ((__force u32)iph->daddr ^ (__force u32)iph2->daddr)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		/* All fields must match except length and checksum. */
		NAPI_GRO_CB(p)->flush |=
			(iph->ttl ^ iph2->ttl) |
			((u16)(ntohs(iph2->id) + NAPI_GRO_CB(p)->count) ^ id);

		NAPI_GRO_CB(p)->flush |= flush;
	}

	NAPI_GRO_CB(skb)->flush |= flush;
	skb_gro_pull(skb, sizeof(*iph));
	skb_set_transport_header(skb, skb_gro_offset(skb));

	pp = ops->gro_receive(head, skb);

out_unlock:
	rcu_read_unlock();

out:
	NAPI_GRO_CB(skb)->flush |= flush;

	return pp;
}

static int inet_gro_complete(struct sk_buff *skb)
{
	const struct net_protocol *ops;
	struct iphdr *iph = ip_hdr(skb);
	int proto = iph->protocol & (MAX_INET_PROTOS - 1);
	int err = -ENOSYS;
	__be16 newlen = htons(skb->len - skb_network_offset(skb));

	csum_replace2(&iph->check, iph->tot_len, newlen);
	iph->tot_len = newlen;

	rcu_read_lock();
	ops = rcu_dereference(inet_protos[proto]);
	if (WARN_ON(!ops || !ops->gro_complete))
		goto out_unlock;

	err = ops->gro_complete(skb);

out_unlock:
	rcu_read_unlock();

	return err;
}

int inet_ctl_sock_create(struct sock **sk, unsigned short family,
			 unsigned short type, unsigned char protocol,
			 struct net *net)
{
	struct socket *sock;
	int rc = sock_create_kern(family, type, protocol, &sock);

	if (rc == 0) {
		*sk = sock->sk;
		(*sk)->sk_allocation = GFP_ATOMIC;
		/*
		 * Unhash it so that IP input processing does not even see it,
		 * we do not wish this socket to see incoming packets.
		 */
		(*sk)->sk_prot->unhash(*sk);

		sk_change_net(*sk, net);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(inet_ctl_sock_create);

unsigned long snmp_fold_field(void __percpu *mib[], int offt)
{
	unsigned long res = 0;
	int i;

	for_each_possible_cpu(i) {
		res += *(((unsigned long *) per_cpu_ptr(mib[0], i)) + offt);
		res += *(((unsigned long *) per_cpu_ptr(mib[1], i)) + offt);
	}
	return res;
}
EXPORT_SYMBOL_GPL(snmp_fold_field);

int snmp_mib_init(void __percpu *ptr[2], size_t mibsize)
{
	BUG_ON(ptr == NULL);
	ptr[0] = __alloc_percpu(mibsize, __alignof__(unsigned long));
	if (!ptr[0])
		goto err0;
	ptr[1] = __alloc_percpu(mibsize, __alignof__(unsigned long));
	if (!ptr[1])
		goto err1;
	return 0;
err1:
	free_percpu(ptr[0]);
	ptr[0] = NULL;
err0:
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(snmp_mib_init);

void snmp_mib_free(void __percpu *ptr[2])
{
	BUG_ON(ptr == NULL);
	free_percpu(ptr[0]);
	free_percpu(ptr[1]);
	ptr[0] = ptr[1] = NULL;
}
EXPORT_SYMBOL_GPL(snmp_mib_free);

#ifdef CONFIG_IP_MULTICAST
static const struct net_protocol igmp_protocol = {
	.handler =	igmp_rcv,
	.netns_ok =	1,
};
#endif

////ipv4_specific是TCP传输层到网络层数据发送以及TCP建立过程的真正OPS，在tcp_prot->init中被赋值给inet_connection_sock->icsk_af_ops
////这里面有每种协议传输层的接收函数，后面的inetsw_array那几行是套接口层的相关函数  在函数中执行handler,见函数ip_local_deliver_finish
//family协议族通过sock_register注册  传输层接口tcp_prot udp_prot netlink_prot等通过proto_register注册   IP层接口通过inet_add_protocol(&icmp_protocol等注册 ，这些组成过程参考inet_init函数

//IP层处理完后(包括ip_local_deliver_finish和icmp_unreach)，走到这里，这是IP层和传输层的邻借口，然后在由这里走到tcp_prot udp_prot raw_prot
static const struct net_protocol tcp_protocol = { //这些是传输层的接收处理过程，传输层和套接口层的处理过程需要使用udp_prot tcp_prot raw_prot过渡到socket层，处理过程参考inetsw_array
	.handler =	tcp_v4_rcv,  //当接收到报文后，ip层处理完后在ip_local_deliver_finish 函数中ret = ipprot->handler(skb);走到这里          //从这里面跳转到tcp_prot
	.err_handler =	tcp_v4_err, //icmp_unreach当收到ICMP差错报文后，如果引起差错的是TCP包就走到该函数
	.gso_send_check = tcp_v4_gso_send_check,
	.gso_segment =	tcp_tso_segment,
	.gro_receive =	tcp4_gro_receive,
	.gro_complete =	tcp4_gro_complete,
	.no_policy =	1,
	.netns_ok =	1,
};
//这几个是接收的过程处理
static const struct net_protocol udp_protocol = { //在函数中执行handler,见函数ip_local_deliver_finish
	.handler =	udp_rcv,
	.err_handler =	udp_err,
	.gso_send_check = udp4_ufo_send_check,
	.gso_segment = udp4_ufo_fragment,
	.no_policy =	1,
	.netns_ok =	1,
};

static const struct net_protocol icmp_protocol = {//在函数中执行handler,见函数ip_local_deliver_finish
	.handler =	icmp_rcv,
	.no_policy =	1,
	.netns_ok =	1,
};

/*
上面几个结构体中的err_handler的解释如下:
 * 目的不可达、源端被关闭、超时、参数错误这四种类型
 * 的差错ICMP报文，都是由同一个函数icmp_unreach()来处理的，
 * 对其中目的不可达、源端被关闭这两种类型ICMP报文
 * 因要提取某些信息而需作一些特殊的处理，而另外
 * 一些则不需要，根据差错报文中的信息直接调用
 * 传输层的错误处理例程。参见<Linux内核源码剖析348页>
 */

static __net_init int ipv4_mib_init_net(struct net *net)
{
	if (snmp_mib_init((void __percpu **)net->mib.tcp_statistics,
			  sizeof(struct tcp_mib)) < 0)
		goto err_tcp_mib;
	if (snmp_mib_init((void __percpu **)net->mib.ip_statistics,
			  sizeof(struct ipstats_mib)) < 0)
		goto err_ip_mib;
	if (snmp_mib_init((void __percpu **)net->mib.net_statistics,
			  sizeof(struct linux_mib)) < 0)
		goto err_net_mib;
	if (snmp_mib_init((void __percpu **)net->mib.udp_statistics,
			  sizeof(struct udp_mib)) < 0)
		goto err_udp_mib;
	if (snmp_mib_init((void __percpu **)net->mib.udplite_statistics,
			  sizeof(struct udp_mib)) < 0)
		goto err_udplite_mib;
	if (snmp_mib_init((void __percpu **)net->mib.icmp_statistics,
			  sizeof(struct icmp_mib)) < 0)
		goto err_icmp_mib;
	if (snmp_mib_init((void __percpu **)net->mib.icmpmsg_statistics,
			  sizeof(struct icmpmsg_mib)) < 0)
		goto err_icmpmsg_mib;

	tcp_mib_init(net);
	return 0;

err_icmpmsg_mib:
	snmp_mib_free((void __percpu **)net->mib.icmp_statistics);
err_icmp_mib:
	snmp_mib_free((void __percpu **)net->mib.udplite_statistics);
err_udplite_mib:
	snmp_mib_free((void __percpu **)net->mib.udp_statistics);
err_udp_mib:
	snmp_mib_free((void __percpu **)net->mib.net_statistics);
err_net_mib:
	snmp_mib_free((void __percpu **)net->mib.ip_statistics);
err_ip_mib:
	snmp_mib_free((void __percpu **)net->mib.tcp_statistics);
err_tcp_mib:
	return -ENOMEM;
}

static __net_exit void ipv4_mib_exit_net(struct net *net)
{
	snmp_mib_free((void __percpu **)net->mib.icmpmsg_statistics);
	snmp_mib_free((void __percpu **)net->mib.icmp_statistics);
	snmp_mib_free((void __percpu **)net->mib.udplite_statistics);
	snmp_mib_free((void __percpu **)net->mib.udp_statistics);
	snmp_mib_free((void __percpu **)net->mib.net_statistics);
	snmp_mib_free((void __percpu **)net->mib.ip_statistics);
	snmp_mib_free((void __percpu **)net->mib.tcp_statistics);
}

static __net_initdata struct pernet_operations ipv4_mib_ops = {
	.init = ipv4_mib_init_net,
	.exit = ipv4_mib_exit_net,
};

static int __init init_ipv4_mibs(void)
{
	return register_pernet_subsys(&ipv4_mib_ops);
}

static int ipv4_proc_init(void);

/*
 *	IP protocol layer initialiser
 */

static struct packet_type ip_packet_type = {// __read_mostly = {
    /* 
        * 由于类型不是 ETH_P_ALL 所以 ip_packet_type
        * 会被添加到 ptype_base 链表中，参见dev_add_pack
        */
	.type = cpu_to_be16(ETH_P_IP),
	.func = ip_rcv,
	.gso_send_check = inet_gso_send_check,//使用的地方在skb_gso_segment
	.gso_segment = inet_gso_segment,//使用的地方在skb_gso_segment
	.gro_receive = inet_gro_receive, 
	.gro_complete = inet_gro_complete, 
};

//设备物理层的初始化net_dev_init
 //TCP/IP协议栈初始化inet_init 其实传输层的协议初始化也在这里面
 //传输层初始化proto_init  只是为了显示各种协议用的
 //套接口层初始化sock_init  netfilter_init在套接口层初始化的时候也初始化了
static int __init inet_init(void)
{
	struct sk_buff *dummy_skb;
	struct inet_protosw *q;
	struct list_head *r;
	int rc = -EINVAL;

	BUILD_BUG_ON(sizeof(struct inet_skb_parm) > sizeof(dummy_skb->cb));

	sysctl_local_reserved_ports = kzalloc(65536 / 8, GFP_KERNEL);
	if (!sysctl_local_reserved_ports)
		goto out;

	rc = proto_register(&tcp_prot, 1);
	if (rc)
		goto out_free_reserved_ports;

	rc = proto_register(&udp_prot, 1);
	if (rc)
		goto out_unregister_tcp_proto;

	rc = proto_register(&raw_prot, 1);
	if (rc)
		goto out_unregister_udp_proto;

	/*
	 *	Tell SOCKET that we are alive...
	 */

	(void)sock_register(&inet_family_ops);

#ifdef CONFIG_SYSCTL
	ip_static_sysctl_init();
#endif

	/*
	 *	Add all the base protocols.
	 */

	if (inet_add_protocol(&icmp_protocol, IPPROTO_ICMP) < 0) //这里面有每种协议传输层的接收函数，后面的inetsw_array那几行是套接口层的相关函数
		printk(KERN_CRIT "inet_init: Cannot add ICMP protocol\n");
	if (inet_add_protocol(&udp_protocol, IPPROTO_UDP) < 0)
		printk(KERN_CRIT "inet_init: Cannot add UDP protocol\n");
	if (inet_add_protocol(&tcp_protocol, IPPROTO_TCP) < 0)
		printk(KERN_CRIT "inet_init: Cannot add TCP protocol\n");
#ifdef CONFIG_IP_MULTICAST
	if (inet_add_protocol(&igmp_protocol, IPPROTO_IGMP) < 0)
		printk(KERN_CRIT "inet_init: Cannot add IGMP protocol\n");
#endif

	/* Register the socket-side information for inet_create. */
	for (r = &inetsw[0]; r < &inetsw[SOCK_MAX]; ++r)
		INIT_LIST_HEAD(r);

	for (q = inetsw_array; q < &inetsw_array[INETSW_ARRAY_LEN]; ++q)
		inet_register_protosw(q); //把inetsw_array结构中的节点添加到inetsw表中，以type为索引

	/*
	 *	Set the ARP module up
	 */

	arp_init();

	/*
	 *	Set the IP module up
	 */

	ip_init();

	tcp_v4_init();

	/* Setup TCP slab cache for open requests. */
	tcp_init();

	/* Setup UDP memory threshold */
	udp_init();

	/* Add UDP-Lite (RFC 3828) */
	udplite4_register();

	/*
	 *	Set the ICMP layer up
	 */
        /*由于协议栈本身有发送ICMP数据报的需求，所以，需要在协议栈中创建内核态的原始套接字，用于发送ICMP数据报，这个事情在协议栈初始化时，
        由 icmp_init函数完成。它为每个CPU都创建一个icmp_socket，创建工作由sock_create_kern函数完成，创建流程跟应用层 创建socket完全一致。*/
    if (icmp_init() < 0)
		panic("Failed to create the ICMP control socket.\n");

	/*
	 *	Initialise the multicast router
	 */
#if defined(CONFIG_IP_MROUTE)
	if (ip_mr_init())
		printk(KERN_CRIT "inet_init: Cannot init ipv4 mroute\n");
#endif
	/*
	 *	Initialise per-cpu ipv4 mibs
	 */

	if (init_ipv4_mibs())
		printk(KERN_CRIT "inet_init: Cannot init ipv4 mibs\n");

	ipv4_proc_init();

	ipfrag_init();

	dev_add_pack(&ip_packet_type);

	rc = 0;
out:
	return rc;
out_unregister_udp_proto:
	proto_unregister(&udp_prot);
out_unregister_tcp_proto:
	proto_unregister(&tcp_prot);
out_free_reserved_ports:
	kfree(sysctl_local_reserved_ports);
	goto out;
}

fs_initcall(inet_init);//internet协议族的初始化

/* ------------------------------------------------------------------------ */

#ifdef CONFIG_PROC_FS
static int __init ipv4_proc_init(void)
{
	int rc = 0;

	if (raw_proc_init())
		goto out_raw;
	if (tcp4_proc_init())
		goto out_tcp;
	if (udp4_proc_init())
		goto out_udp;
	if (ip_misc_proc_init())
		goto out_misc;
out:
	return rc;
out_misc:
	udp4_proc_exit();
out_udp:
	tcp4_proc_exit();
out_tcp:
	raw_proc_exit();
out_raw:
	rc = -ENOMEM;
	goto out;
}

#else /* CONFIG_PROC_FS */
static int __init ipv4_proc_init(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */

MODULE_ALIAS_NETPROTO(PF_INET);

