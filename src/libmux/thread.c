/* Copyright (C) 2003 Russ Cox, Massachusetts Institute of Technology */
/* See COPYRIGHT */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <mux.h>

enum
{
	STACK = 32768
};

void
muxthreads(Mux *mux)
{
	proccreate(_muxrecvproc, mux, STACK);
	qlock(&mux->lk);
	while(!mux->writeq)
		rsleep(&mux->rpcfork);
	qunlock(&mux->lk);
	proccreate(_muxsendproc, mux, STACK);
	qlock(&mux->lk);
	while(!mux->writeq)
		rsleep(&mux->rpcfork);
	qunlock(&mux->lk);
}
