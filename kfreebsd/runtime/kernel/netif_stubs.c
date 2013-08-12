/*-
 * Copyright (c) 2012, 2013 Gabor Pali
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>

#include "caml/mlvalues.h"
#include "caml/memory.h"
#include "caml/alloc.h"
#include "caml/fail.h"
#include "caml/bigarray.h"

const u_char lladdr_prefix[4]           = { 0x02, 0xAD, 0xBE, 0xEF };
const u_char lladdr_all[ETHER_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

struct mbuf_entry {
	LIST_ENTRY(mbuf_entry)	me_next;
	struct mbuf		*me_m;
};

struct plugged_if {
	TAILQ_ENTRY(plugged_if)	pi_next;
	struct ifnet		*pi_ifp;
	u_short	pi_index;
	u_short	pi_llindex;
	int	pi_flags;
	u_char	pi_lladdr[ETHER_ADDR_LEN];	/* Real MAC address */
	u_char	pi_lladdr_v[ETHER_ADDR_LEN];	/* Virtual MAC address */
#ifdef NETIF_DEBUG
	int	pi_rx_qlen;
#endif
	char	pi_xname[IFNAMSIZ];
	struct  mtx			pi_rx_lock;
	LIST_HEAD(, mbuf_entry)		pi_rx_head;
};

TAILQ_HEAD(plugged_ifhead, plugged_if) pihead =
    TAILQ_HEAD_INITIALIZER(pihead);

int plugged = 0;


/* Currently only Ethernet interfaces are returned. */
CAMLprim value caml_get_vifs(value v_unit);
CAMLprim value caml_plug_vif(value id);
CAMLprim value caml_unplug_vif(value id);
CAMLprim value caml_get_mbufs(value id);
CAMLprim value caml_put_mbufs(value id, value bufs);

void netif_ether_input(struct ifnet *ifp, struct mbuf **mp);
int  netif_ether_output(struct ifnet *ifp, struct mbuf **mp);
void netif_cleanup(void);


static struct plugged_if *
find_pi_by_index(u_short val)
{
	struct plugged_if *pip;

	TAILQ_FOREACH(pip, &pihead, pi_next)
		if (pip->pi_index == val)
			return pip;
	return NULL;
}

static struct plugged_if *
find_pi_by_name(const char *val)
{
	struct plugged_if *pip;

	TAILQ_FOREACH(pip, &pihead, pi_next)
		if (strncmp(pip->pi_xname, val, IFNAMSIZ) == 0)
			return pip;
	return NULL;
}

CAMLprim value
caml_get_vifs(value v_unit)
{
	CAMLparam1(v_unit);
	CAMLlocal2(result, r);
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;

	result = Val_emptylist;
	IFNET_RLOCK_NOSLEEP();
	TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		IF_ADDR_RLOCK(ifp);
		TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			sdl = (struct sockaddr_dl *) ifa->ifa_addr;
			if (sdl != NULL && sdl->sdl_family == AF_LINK &&
			    sdl->sdl_type == IFT_ETHER) {
				/* We have a MAC address, add the interface. */
				r = caml_alloc(2, 0);
				Store_field(r, 0,
				    caml_copy_string(ifp->if_xname));
				Store_field(r, 1, result);
				result = r;
				break;
			}
		}
		IF_ADDR_RUNLOCK(ifp);
	}
	IFNET_RUNLOCK_NOSLEEP();
	CAMLreturn(result);
}

CAMLprim value
caml_plug_vif(value id)
{
	CAMLparam1(id);
	CAMLlocal1(result);
	struct ifnet *ifp;
	struct sockaddr_dl *sdl;
	struct plugged_if *pip;
	char	lladdr_str[32];
	u_char	lladdr[ETHER_ADDR_LEN];
	int found;
#ifdef NETIF_DEBUG
	u_char	*p1, *p2;
#endif

	pip = malloc(sizeof(struct plugged_if), M_MIRAGE, M_NOWAIT | M_ZERO);

	if (pip == NULL)
		caml_failwith("No memory for plugging a new interface");

	found = 0;
	IFNET_WLOCK();
	TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (strncmp(ifp->if_xname, String_val(id), IFNAMSIZ) != 0)
			continue;

		/* "Enable" the fake NetGraph node. */
		IFP2AC(ifp)->ac_netgraph = (void *) 1;
		pip->pi_ifp   = ifp;
		pip->pi_index = ifp->if_index;
		pip->pi_flags = ifp->if_flags;
		bcopy(ifp->if_xname, pip->pi_xname, IFNAMSIZ);

		IF_ADDR_RLOCK(ifp);
		sdl = (struct sockaddr_dl *) ifp->if_addr->ifa_addr;
		if (sdl != NULL && sdl->sdl_family == AF_LINK &&
		    sdl->sdl_type == IFT_ETHER)
			bcopy(LLADDR(sdl), pip->pi_lladdr, ETHER_ADDR_LEN);
		IF_ADDR_RUNLOCK(ifp);

		found = 1;
		break;
	}
	IFNET_WUNLOCK();

	if (!found) {
		free(pip, M_MIRAGE);
		caml_failwith("Invalid interface");
	}

	bcopy(lladdr_prefix, lladdr, sizeof(lladdr_prefix));
	pip->pi_llindex = plugged + 1;
	lladdr[4] = pip->pi_llindex >> 8;
	lladdr[5] = pip->pi_llindex & 0xFF;

	sprintf(lladdr_str, "%02x:%02x:%02x:%02x:%02x:%02x", lladdr[0],
	    lladdr[1], lladdr[2], lladdr[3], lladdr[4], lladdr[5]);

	bcopy(lladdr, pip->pi_lladdr_v, sizeof(lladdr));

	mtx_init(&pip->pi_rx_lock, "plugged_if_rx", NULL, MTX_DEF);
	LIST_INIT(&pip->pi_rx_head);

	if (plugged == 0)
		TAILQ_INIT(&pihead);

	TAILQ_INSERT_TAIL(&pihead, pip, pi_next);
	plugged++;

#ifdef NETIF_DEBUG
	p1 = pip->pi_lladdr;
	p2 = pip->pi_lladdr_v;
	printf("caml_plug_vif: ifname=[%s] MAC=("
	    "real=%02x:%02x:%02x:%02x:%02x:%02x, "
	    "virtual=%02x:%02x:%02x:%02x:%02x:%02x)\n",
	    pip->pi_xname, p1[0], p1[1], p1[2], p1[3], p1[4], p1[5],
	    p2[0], p2[1], p2[2], p2[3], p2[4], p2[5]);
#endif

	result = caml_alloc(3, 0);
	Store_field(result, 0, Val_bool(pip->pi_flags & IFF_UP));
	Store_field(result, 1, Val_int(pip->pi_llindex));
	Store_field(result, 2, caml_copy_string(lladdr_str));
	CAMLreturn(result);
}

CAMLprim value
caml_unplug_vif(value id)
{
	CAMLparam1(id);
	struct plugged_if *pip;
	struct ifnet *ifp;
	struct mbuf_entry *e1;
	struct mbuf_entry *e2;

	pip = find_pi_by_name(String_val(id));
	if (pip == NULL)
		CAMLreturn(Val_unit);

	IFNET_WLOCK();
	TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (strncmp(ifp->if_xname, String_val(id), IFNAMSIZ) == 0) {
			/* "Disable" the fake NetGraph node. */
			IFP2AC(ifp)->ac_netgraph = (void *) 0;
			break;
		}
	}
	IFNET_WUNLOCK();

#ifdef NETIF_DEBUG
	printf("caml_unplug_vif: ifname=[%s]\n", pip->pi_xname);
#endif

	TAILQ_REMOVE(&pihead, pip, pi_next);

	e1 = LIST_FIRST(&pip->pi_rx_head);
	while (e1 != NULL) {
		e2 = LIST_NEXT(e1, me_next);
		m_freem(e1->me_m);
		free(e1, M_MIRAGE);
		e1 = e2;
	}
	LIST_INIT(&pip->pi_rx_head);
	mtx_destroy(&pip->pi_rx_lock);

	free(pip, M_MIRAGE);
	plugged--;

	CAMLreturn(Val_unit);
}

/* Listening to incoming Ethernet frames. */
void
netif_ether_input(struct ifnet *ifp, struct mbuf **mp)
{
	struct plugged_if *pip;
	struct mbuf_entry *e;
	struct ether_header *eh;
	char mine, bcast;
#ifdef NETIF_DEBUG
	int i;
#endif

#ifdef NETIF_DEBUG
	printf("New incoming frame on if=[%s]!\n", ifp->if_xname);
#endif

	if (plugged == 0)
		return;

	pip = find_pi_by_index(ifp->if_index);

	if (pip == NULL)
		return;

	eh = mtod(*mp, struct ether_header *);
	mine  = bcmp(eh->ether_dhost, pip->pi_lladdr_v, ETHER_ADDR_LEN) == 0;
	bcast = bcmp(eh->ether_dhost, lladdr_all, ETHER_ADDR_LEN) == 0;

#ifdef NETIF_DEBUG
	printf("Destination: %02x:%02x:%02x:%02x:%02x:%02x (%04x), %s.\n",
	    eh->ether_dhost[0], eh->ether_dhost[1], eh->ether_dhost[2],
	    eh->ether_dhost[3], eh->ether_dhost[4], eh->ether_dhost[5],
	    ntohs(eh->ether_type), (mine || bcast) ? "intercepting" : "skipping");
#endif

	/* Let the frame escape if it is neither ours nor broadcast. */
	if (!mine && !bcast)
		return;

	e = (struct mbuf_entry *) malloc(sizeof(struct mbuf_entry), M_MIRAGE,
	    M_NOWAIT);
	e->me_m = bcast ? m_copypacket(*mp, M_DONTWAIT) : *mp;
	mtx_lock(&pip->pi_rx_lock);
	LIST_INSERT_HEAD(&pip->pi_rx_head, e, me_next);
#ifdef NETIF_DEBUG
	i = (++pip->pi_rx_qlen);
#endif
	mtx_unlock(&pip->pi_rx_lock);
#ifdef NETIF_DEBUG
	printf("[%s]: %d frames are queued.\n", pip->pi_xname, i);
#endif

	if (!bcast)
		*mp = NULL;
}

CAMLprim value
caml_get_mbufs(value id)
{
	CAMLparam1(id);
	CAMLlocal3(result, t, r);
	struct plugged_if *pip;
	struct mbuf_entry *e1;
	struct mbuf_entry *e2;
	struct mbuf *m;
	struct mbuf *n;
#ifdef NETIF_DEBUG
	int num_pages;
#endif

#ifdef NETIF_DEBUG
	printf("caml_get_mbufs(): invoked\n");
#endif

	result = Val_emptylist;

	if (plugged == 0)
		CAMLreturn(result);

	pip = find_pi_by_index(Int_val(id));

#ifdef NETIF_DEBUG
	printf("caml_get_mbufs(): pip=%p\n", pip);
	num_pages = 0;
#endif

	if (pip == NULL)
		CAMLreturn(result);

	mtx_lock(&pip->pi_rx_lock);
	e1 = LIST_FIRST(&pip->pi_rx_head);
	while (e1 != NULL) {
		for (m = e1->me_m; m != NULL; m = m->m_nextpkt) {
			for (n = m; n != NULL; n = n->m_next) {
				t = caml_alloc(3, 0);
				Store_field(t, 0,
				    caml_ba_alloc_dims(CAML_BA_UINT8
				    | CAML_BA_C_LAYOUT | CAML_BA_MBUF, 1,
				    (void *) n, (long) n->m_len));
				Store_field(t, 1, Val_int(0));
				Store_field(t, 2, Val_int(n->m_len));
				r = caml_alloc(2, 0);
				Store_field(r, 0, t);
				Store_field(r, 1, result);
				result = r;
#ifdef NETIF_DEBUG
				num_pages++;
				pip->pi_rx_qlen--;
#endif
			}
		}
		e2 = LIST_NEXT(e1, me_next);
		free(e1, M_MIRAGE);
		e1 = e2;
	}
	LIST_INIT(&pip->pi_rx_head);
	mtx_unlock(&pip->pi_rx_lock);

#ifdef NETIF_DEBUG
	printf("caml_get_mbufs(): shipped %d pages.\n", num_pages);
#endif

	CAMLreturn(result);
}

int
netif_ether_output(struct ifnet *ifp, struct mbuf **mp)
{
#ifdef NETIF_DEBUG
	printf("New outgoing frame on if=[%s], feeding back.\n", ifp->if_xname);
#endif

	if (plugged == 0)
		return 0;

	netif_ether_input(ifp, mp);
	return 0;
}

static void
netif_mbuf_free(void *p1, void *p2)
{
	struct caml_ba_meta *meta = (struct caml_ba_meta *) p1;

#ifdef NETIF_DEBUG
	printf("netif_mbuf_free: %p, %p\n", p1, p2);
#endif

	switch (meta->bm_type) {
		case BM_IOPAGE:
			if (--(meta->bm_refcnt) > 0)
				return;
			contigfree(p2, meta->bm_size, M_MIRAGE);
			break;

		case BM_MBUF:
			m_free(meta->bm_mbuf);
			break;

		default:
			printf("Unknown Bigarray metadata type: %02x\n",
			    meta->bm_type);
			break;
	}

	free(meta, M_MIRAGE);
}

static struct mbuf *
netif_map_to_mbuf(struct caml_ba_array *b, int v_off, int *v_len)
{
	struct mbuf **mp;
	struct mbuf *m;
	struct mbuf *frag;
	struct caml_ba_meta *meta;
	void *data;
	size_t frag_len;
	char *p;

	meta = (struct caml_ba_meta *) b->data2;
	data = (char *) b->data + v_off;
	frag_len = min(meta->bm_size - v_off, *v_len);
	*v_len = frag_len;

	mp = &frag;
	p = data;

	while (frag_len > 0) {
		MGET(m, M_DONTWAIT, MT_DATA);

		if (m == NULL) {
			m_freem(frag);
			return NULL;
		}

		m->m_flags       |= M_EXT;
		m->m_ext.ext_type = EXT_EXTREF;
		m->m_ext.ext_buf  = (void *) p;
		m->m_ext.ext_free = netif_mbuf_free;
		m->m_ext.ext_arg1 = meta;
		m->m_ext.ext_arg2 = data;
		m->m_ext.ref_cnt  = &meta->bm_refcnt;
		m->m_len          = min(MCLBYTES, frag_len);
		m->m_data         = m->m_ext.ext_buf;
		*(m->m_ext.ref_cnt) += 1;

		frag_len -= m->m_len;
		p += m->m_len;
		*mp = m;
		mp = &(m->m_next);
	}

	return frag;
}

CAMLprim value
caml_put_mbufs(value id, value bufs)
{
	CAMLparam2(id, bufs);
	CAMLlocal2(v, t);
	struct plugged_if *pip;
	struct mbuf **mp;
	struct mbuf *frag;
	struct mbuf *pkt;
	struct caml_ba_array *b;
	struct ifnet *ifp;
	size_t pkt_len;
	int v_off, v_len;
	char bcast, real;
	struct ether_header *eh;

	if ((bufs == Val_emptylist) || (plugged == 0))
		CAMLreturn(Val_unit);

	pip = find_pi_by_index(Int_val(id));
	if (pip == NULL)
		CAMLreturn(Val_unit);

	ifp = pip->pi_ifp;
	pkt_len = 0;
	mp = &pkt;

	while (bufs != Val_emptylist) {
		t = Field(bufs, 0);
		v = Field(t, 0);
		v_off = Int_val(Field(t, 1));
		v_len = Int_val(Field(t, 2));
		b = Caml_ba_array_val(v);
		frag = netif_map_to_mbuf(b, v_off, &v_len);
		if (frag == NULL)
			caml_failwith("No memory for mapping to mbuf");
		*mp = frag;
		mp = &(frag->m_next);
		pkt_len += v_len;
		bufs = Field(bufs, 1);
	}

	pkt->m_flags       |= M_PKTHDR;
	pkt->m_pkthdr.len   = pkt_len;
	pkt->m_pkthdr.rcvif = ifp;
	SLIST_INIT(&pkt->m_pkthdr.tags);

	if (pkt->m_pkthdr.len > pip->pi_ifp->if_mtu)
		printf("%s: Packet is greater (%d) than the MTU (%ld)\n",
		    pip->pi_xname, pkt->m_pkthdr.len, pip->pi_ifp->if_mtu);

	eh = mtod(pkt, struct ether_header *);
	real  = bcmp(eh->ether_dhost, pip->pi_lladdr, ETHER_ADDR_LEN) == 0;
	bcast = bcmp(eh->ether_dhost, lladdr_all, ETHER_ADDR_LEN) == 0;

#ifdef NETIF_DEBUG
	printf("Sending to: %02x:%02x:%02x:%02x:%02x:%02x (%04x), %s%s.\n",
	    eh->ether_dhost[0], eh->ether_dhost[1], eh->ether_dhost[2],
	    eh->ether_dhost[3], eh->ether_dhost[4], eh->ether_dhost[5],
	    ntohs(eh->ether_type),
	    (real || bcast)  ? "[if_input]"  : "",
	    (!real && bcast) ? "[if_output]" : "");
#endif

	/* Sending to the real Ethernet address. */
	if (real || bcast)
		(ifp->if_input)(ifp,
		    bcast ? m_copypacket(pkt, M_DONTWAIT) : pkt);

	if (!real && bcast)
		(ifp->if_transmit)(ifp, pkt);

	CAMLreturn(Val_unit);
}

void
netif_cleanup(void)
{
	struct plugged_if *p1, *p2;
	struct ifnet *ifp;

	p1 = TAILQ_FIRST(&pihead);
	while (p1 != NULL) {
		p2  = TAILQ_NEXT(p1, pi_next);
		ifp = p1->pi_ifp;
		IFNET_WLOCK();
		IFP2AC(ifp)->ac_netgraph = (void *) 0;
		IFNET_WUNLOCK();
		free(p1, M_MIRAGE);
		plugged--;
		p1 = p2;
	}
	TAILQ_INIT(&pihead);
}
