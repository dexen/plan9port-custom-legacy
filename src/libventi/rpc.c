/*
 * Multiplexed Venti client.  It would be nice if we 
 * could turn this into a generic library routine rather
 * than keep it Venti specific.  A user-level 9P client
 * could use something like this too.
 *
 * This is a little more complicated than it might be
 * because we want it to work well within and without libthread.
 *
 * The mux code is inspired by tra's, which is inspired by the Plan 9 kernel.
 */

#include <u.h>
#include <libc.h>
#include <venti.h>

typedef struct Rwait Rwait;
struct Rwait
{
	Rendez r;
	Packet *p;
	int done;
	int sleeping;
};

static int gettag(VtConn*, Rwait*);
static void puttag(VtConn*, Rwait*, int);
static void muxrpc(VtConn*, Packet*);
Packet *vtrpc(VtConn*, Packet*);

Packet*
vtrpc(VtConn *z, Packet *p)
{
	int i;
	uchar tag, buf[2], *top;
	Rwait *r;

	/* must malloc because stack could be private */
	r = vtmallocz(sizeof(Rwait));

	qlock(&z->lk);
	r->r.l = &z->lk;
	tag = gettag(z, r);

	/* slam tag into packet */
	top = packetpeek(p, buf, 0, 2);
	if(top == nil){
		packetfree(p);
		return nil;
	}
	if(top == buf){
		werrstr("first two bytes must be in same packet fragment");
		packetfree(p);
		return nil;
	}
	top[1] = tag;
	qunlock(&z->lk);
	if(vtsend(z, p) < 0)
		return nil;

	qlock(&z->lk);
	/* wait for the muxer to give us our packet */
	r->sleeping = 1;
	z->nsleep++;
	while(z->muxer && !r->done)
		rsleep(&r->r);
	z->nsleep--;
	r->sleeping = 0;

	/* if not done, there's no muxer: start muxing */
	if(!r->done){
		if(z->muxer)
			abort();
		z->muxer = 1;
		while(!r->done){
			qunlock(&z->lk);
			if((p = vtrecv(z)) == nil){
				werrstr("unexpected eof on venti connection");
				z->muxer = 0;
				return nil;
			}
			qlock(&z->lk);
			muxrpc(z, p);
		}
		z->muxer = 0;
		/* if there is anyone else sleeping, wake them to mux */
		if(z->nsleep){
			for(i=0; i<256; i++)
				if(z->wait[i] != nil && ((Rwait*)z->wait[i])->sleeping)
					break;
			if(i==256)
				fprint(2, "libventi: nsleep botch\n");
			else
				rwakeup(&((Rwait*)z->wait[i])->r);
		}	
	}

	p = r->p;
	puttag(z, r, tag);
	vtfree(r);
	qunlock(&z->lk);
	return p;
}

static int 
gettag(VtConn *z, Rwait *r)
{
	int i;

Again:
	while(z->ntag == 256)
		rsleep(&z->tagrend);
	for(i=0; i<256; i++)
		if(z->wait[i] == 0){
			z->ntag++;
			z->wait[i] = r;
			return i;
		}
	fprint(2, "libventi: ntag botch\n");
	goto Again;
}

static void
puttag(VtConn *z, Rwait *r, int tag)
{
	assert(z->wait[tag] == r);
	z->wait[tag] = nil;
	z->ntag--;
	rwakeup(&z->tagrend);
}

static void
muxrpc(VtConn *z, Packet *p)
{
	uchar tag, buf[2], *top;
	Rwait *r;

	if((top = packetpeek(p, buf, 0, 2)) == nil){
		fprint(2, "libventi: short packet in vtrpc\n");
		packetfree(p);
		return;
	}

	tag = top[1];
	if((r = z->wait[tag]) == nil){
		fprint(2, "libventi: unexpected packet tag %d in vtrpc\n", tag);
abort();
		packetfree(p);
		return;
	}

	r->p = p;
	r->done = 1;
	rwakeup(&r->r);
}

