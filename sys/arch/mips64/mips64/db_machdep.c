/*	$OpenBSD: db_machdep.c,v 1.38 2014/04/09 21:10:35 miod Exp $ */

/*
 * Copyright (c) 1998-2003 Opsycon AB (www.opsycon.se)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <dev/cons.h>

#include <mips64/cache.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/db_machdep.h>
#include <machine/mips_opcode.h>
#include <machine/pte.h>
#include <machine/frame.h>
#include <machine/regnum.h>

#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>
#include <ddb/db_run.h>

#define MIPS_JR_RA        0x03e00008      /* instruction code for jr ra */

void  stacktrace_subr(db_regs_t *, int, int (*)(const char*, ...));

uint32_t kdbpeek(vaddr_t);
uint64_t kdbpeekd(vaddr_t);
uint16_t kdbpeekw(vaddr_t);
uint8_t  kdbpeekb(vaddr_t);
void  kdbpoke(vaddr_t, uint32_t);
void  kdbpoked(vaddr_t, uint64_t);
void  kdbpokew(vaddr_t, uint16_t);
void  kdbpokeb(vaddr_t, uint8_t);
int   kdb_trap(int, struct trap_frame *);

void db_print_tlb(uint, uint64_t);
void db_trap_trace_cmd(db_expr_t, int, db_expr_t, char *);
void db_dump_tlb_cmd(db_expr_t, int, db_expr_t, char *);

#ifdef MULTIPROCESSOR
struct mutex ddb_mp_mutex = MUTEX_INITIALIZER(IPL_HIGH);
volatile int ddb_state = DDB_STATE_NOT_RUNNING;
volatile cpuid_t ddb_active_cpu;
boolean_t        db_switch_cpu;
long             db_switch_to_cpu;
#endif

int   db_active = 0;
db_regs_t ddb_regs;

#ifdef MULTIPROCESSOR
void db_cpuinfo_cmd(db_expr_t, int, db_expr_t, char *);
void db_startproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_stopproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_ddbproc_cmd(db_expr_t, int, db_expr_t, char *);
#endif

struct db_variable db_regs[] = {
    { "at",  (long *)&ddb_regs.ast,     FCN_NULL },
    { "v0",  (long *)&ddb_regs.v0,      FCN_NULL },
    { "v1",  (long *)&ddb_regs.v1,      FCN_NULL },
    { "a0",  (long *)&ddb_regs.a0,      FCN_NULL },
    { "a1",  (long *)&ddb_regs.a1,      FCN_NULL },
    { "a2",  (long *)&ddb_regs.a2,      FCN_NULL },
    { "a3",  (long *)&ddb_regs.a3,      FCN_NULL },
    { "a4",  (long *)&ddb_regs.t0,      FCN_NULL },
    { "a5",  (long *)&ddb_regs.t1,      FCN_NULL },
    { "a6",  (long *)&ddb_regs.t2,      FCN_NULL },
    { "a7",  (long *)&ddb_regs.t3,      FCN_NULL },
    { "t0",  (long *)&ddb_regs.t4,      FCN_NULL },
    { "t1",  (long *)&ddb_regs.t5,      FCN_NULL },
    { "t2",  (long *)&ddb_regs.t6,      FCN_NULL },
    { "t3",  (long *)&ddb_regs.t7,      FCN_NULL },
    { "s0",  (long *)&ddb_regs.s0,      FCN_NULL },
    { "s1",  (long *)&ddb_regs.s1,      FCN_NULL },
    { "s2",  (long *)&ddb_regs.s2,      FCN_NULL },
    { "s3",  (long *)&ddb_regs.s3,      FCN_NULL },
    { "s4",  (long *)&ddb_regs.s4,      FCN_NULL },
    { "s5",  (long *)&ddb_regs.s5,      FCN_NULL },
    { "s6",  (long *)&ddb_regs.s6,      FCN_NULL },
    { "s7",  (long *)&ddb_regs.s7,      FCN_NULL },
    { "t8",  (long *)&ddb_regs.t8,      FCN_NULL },
    { "t9",  (long *)&ddb_regs.t9,      FCN_NULL },
    { "k0",  (long *)&ddb_regs.k0,      FCN_NULL },
    { "k1",  (long *)&ddb_regs.k1,      FCN_NULL },
    { "gp",  (long *)&ddb_regs.gp,      FCN_NULL },
    { "sp",  (long *)&ddb_regs.sp,      FCN_NULL },
    { "s8",  (long *)&ddb_regs.s8,      FCN_NULL },
    { "ra",  (long *)&ddb_regs.ra,      FCN_NULL },
    { "sr",  (long *)&ddb_regs.sr,      FCN_NULL },
    { "lo",  (long *)&ddb_regs.mullo,   FCN_NULL },
    { "hi",  (long *)&ddb_regs.mulhi,   FCN_NULL },
    { "bad", (long *)&ddb_regs.badvaddr,FCN_NULL },
    { "cs",  (long *)&ddb_regs.cause,   FCN_NULL },
    { "pc",  (long *)&ddb_regs.pc,      FCN_NULL },
};
struct db_variable *db_eregs = db_regs + sizeof(db_regs)/sizeof(db_regs[0]);

extern label_t  *db_recover;

int
kdb_trap(type, fp)
	int type;
	struct trap_frame *fp;
{
	switch(type) {
	case T_BREAK:		/* breakpoint */
		if (db_get_value((fp)->pc, sizeof(int), FALSE) == BREAK_SOVER) {
			(fp)->pc += BKPT_SIZE;
		}
		break;
	case -1:
		break;
	default:
#if 0
		if (!db_panic)
			return (0);
#endif
		if (db_recover != 0) {
			db_error("Caught exception in ddb.\n");
			/*NOTREACHED*/
		}
		printf("stopped on non ddb fault\n");
	}
	
#ifdef MULTIPROCESSOR
	mtx_enter(&ddb_mp_mutex);
	if (ddb_state == DDB_STATE_EXITING)
		ddb_state = DDB_STATE_NOT_RUNNING;
	mtx_leave(&ddb_mp_mutex);
	
	while (db_enter_ddb()) {
#endif
		bcopy((void *)fp, (void *)&ddb_regs, NUMSAVEREGS * sizeof(register_t));

		db_active++;
		cnpollc(TRUE);
		db_trap(type, 0);
		cnpollc(FALSE);
		db_active--;
		
		bcopy((void *)&ddb_regs, (void *)fp, NUMSAVEREGS * sizeof(register_t));
#ifdef MULTIPROCESSOR
		if (!db_switch_cpu)
			ddb_state = DDB_STATE_EXITING;
	}
#endif
	return(TRUE);
}

#ifdef MULTIPROCESSOR
int
db_enter_ddb(void)
{
	int i;
	struct cpu_info *ci = curcpu();
	mtx_enter(&ddb_mp_mutex);

#ifdef DEBUG
	printf("db_enter_ddb %d: state %x pause %x\n", ci->ci_cpuid,
	    ddb_state, ci->ci_ddb);
#endif
	/* If we are first in, grab ddb and stop all other CPUs */
	if (ddb_state == DDB_STATE_NOT_RUNNING) {
		ddb_active_cpu = cpu_number();
		ddb_state = DDB_STATE_RUNNING;
		ci->ci_ddb = CI_DDB_INDDB;
		for (i = 0; i < ncpus; i++) {
			if (i != cpu_number() &&
			    get_cpu_info(i)->ci_ddb != CI_DDB_STOPPED) {
				get_cpu_info(i)->ci_ddb = CI_DDB_SHOULDSTOP;
				mips64_send_ipi(get_cpu_info(i)->ci_cpuid, MIPS64_IPI_DDB);
			}
		}
		mtx_leave(&ddb_mp_mutex);
		return (1);
	}

	/* Leaving ddb completely.  Start all other CPUs and return 0 */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_EXITING) {
		for (i = 0; i < ncpus; i++) {
			get_cpu_info(i)->ci_ddb = CI_DDB_RUNNING;
		}
		mtx_leave(&ddb_mp_mutex);
		return (0);
	}

	/* We are switching to another CPU. ddb_ddbproc_cmd() has made sure
	 * it is waiting for ddb, we just have to set ddb_active_cpu. */
	if (ddb_active_cpu == cpu_number() && db_switch_cpu) {
		ci->ci_ddb = CI_DDB_SHOULDSTOP;
		db_switch_cpu = 0;
		ddb_active_cpu = db_switch_to_cpu;
		get_cpu_info(db_switch_to_cpu)->ci_ddb = CI_DDB_ENTERDDB;
	}

	/* Wait until we should enter ddb or resume */
	while (ddb_active_cpu != cpu_number() &&
	    ci->ci_ddb != CI_DDB_RUNNING) {
		if (ci->ci_ddb == CI_DDB_SHOULDSTOP)
			ci->ci_ddb = CI_DDB_STOPPED;
		mtx_leave(&ddb_mp_mutex);
		/* Busy wait without locking, we will confirm with lock later */
		while (ddb_active_cpu != cpu_number() &&
		    ci->ci_ddb != CI_DDB_RUNNING)
			;	/* Do nothing */
		mtx_enter(&ddb_mp_mutex);
	}

	/* Either enter ddb or exit */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_RUNNING) {
		ci->ci_ddb = CI_DDB_INDDB;
		mtx_leave(&ddb_mp_mutex);
		return (1);
	} else {
		mtx_leave(&ddb_mp_mutex);
		return (0);
	}
}


void
db_cpuinfo_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;

	for (i = 0; i < ncpus; i++) {
		db_printf("%c%4d: ", (i == cpu_number()) ? '*' : ' ',
		    get_cpu_info(i)->ci_cpuid);
		switch(get_cpu_info(i)->ci_ddb) {
		case CI_DDB_RUNNING:
			db_printf("running\n");
			break;
		case CI_DDB_SHOULDSTOP:
			db_printf("stopping\n");
			break;
		case CI_DDB_STOPPED:
			db_printf("stopped\n");
			break;
		case CI_DDB_ENTERDDB:
			db_printf("entering ddb\n");
			break;
		case CI_DDB_INDDB:
			db_printf("ddb\n");
			break;
		default:
			db_printf("? (%d)\n",
			    get_cpu_info(i)->ci_ddb);
			break;
		}
	}
}
#endif

void
db_read_bytes(addr, size, data)
	vaddr_t addr;
	size_t      size;
	char       *data;
{
	while (size >= sizeof(uint32_t)) {
		*(uint32_t *)data = kdbpeek(addr);
		data += sizeof(uint32_t);
		addr += sizeof(uint32_t);
		size -= sizeof(uint32_t);
	}

	if (size >= sizeof(uint16_t)) {
		*(uint16_t *)data = kdbpeekw(addr);
		data += sizeof(uint16_t);
		addr += sizeof(uint16_t);
		size -= sizeof(uint16_t);
	}

	if (size)
		*(uint8_t *)data = kdbpeekb(addr);
}

void
db_write_bytes(addr, size, data)
	vaddr_t addr;
	size_t      size;
	char       *data;
{
	vaddr_t ptr = addr;
	size_t len = size;

	while (len >= sizeof(uint32_t)) {
		kdbpoke(ptr, *(uint32_t *)data);
		data += sizeof(uint32_t);
		ptr += sizeof(uint32_t);
		len -= sizeof(uint32_t);
	}

	if (len >= sizeof(uint16_t)) {
		kdbpokew(ptr, *(uint16_t *)data);
		data += sizeof(uint16_t);
		ptr += sizeof(uint16_t);
		len -= sizeof(uint16_t);
	}

	if (len)
		kdbpokeb(ptr, *(uint8_t *)data);

	if (addr < VM_MAXUSER_ADDRESS) {
		struct cpu_info *ci = curcpu();

		Mips_HitSyncDCache(ci, addr, size);
		Mips_InvalidateICache(ci, addr, size);
	}
}

void
db_stack_trace_print(addr, have_addr, count, modif, pr)
	db_expr_t	addr;
	boolean_t	have_addr;
	db_expr_t	count;
	char		*modif;
	int		(*pr)(const char *, ...);
{
	struct trap_frame *regs = &ddb_regs;

	if (have_addr) {
		(*pr)("mips trace requires a trap frame... giving up\n");
		return;
	}

	stacktrace_subr(regs, count, pr);
}

/*
 *	To do a single step ddb needs to know the next address
 *	that we will get to. It means that we need to find out
 *	both the address for a branch taken and for not taken, NOT! :-)
 *	MipsEmulateBranch will do the job to find out _exactly_ which
 *	address we will end up at so the 'dual bp' method is not
 *	required.
 */
db_addr_t
next_instr_address(db_addr_t pc, boolean_t bd)
{
	db_addr_t next;

	next = MipsEmulateBranch(&ddb_regs, (vaddr_t)pc, 0, 0);
	return(next);
}


/*
 *  MIPS machine dependent DDB commands.
 */

/*
 *  Do a trap traceback.
 */
void
db_trap_trace_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *m)
{
	trapDump("ddb trap trace", db_printf);
}

void
db_print_tlb(uint tlbno, uint64_t tlblo)
{
	paddr_t pa;
#ifndef CPU_R8000
	/* short description of coherency attributes */
	static const char *attr[] = {
	    "CCA 0",
	    "CCA 1",
	    "NC   ",
	    "C    ",
	    "CEX  ",
	    "CEXW ",
	    "CCA 6",
	    "NCACC"
	};
#endif

	pa = pfn_to_pad(tlblo);
#ifdef CPU_R8000
	pa |= ptoa(tlbno % 128);
#endif
	if (tlblo & PG_V) {
		db_printf("%016lx ", pa);
		db_printf("%c", tlblo & PG_M ? 'M' : ' ');
#ifndef CPU_R8000
		db_printf("%c", tlblo & PG_G ? 'G' : ' ');
		db_printf("%s ", attr[(tlblo >> 3) & 7]);
#endif
	} else {
		db_printf("invalid                 ");
	}
}

/*
 *	Dump TLB contents.
 * Syntax: machine tlb [/p asid] [/c] [tlb#]
 *	/p: only display tlb entries matching the given asid
 *	/c: check for duplicate entries
 *	tlb#: display <count> entries starting from this index
 */
void
db_dump_tlb_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *m)
{
	int tlbno, last, check, pid;
	struct tlb_entry tlb, tlbp;
	struct cpu_info *ci = curcpu();

	pid = -1;

	if (m[0] == 'p') {
		if (have_addr && addr < PG_ASID_COUNT) {
			pid = addr;
		}
		tlbno = 0;
		count = ci->ci_hw.tlbsize;
	} else if (m[0] == 'c') {
		last = ci->ci_hw.tlbsize;
		for (tlbno = 0; tlbno < last; tlbno++) {
			tlb_read(tlbno, &tlb);
			for (check = tlbno + 1; check < last; check++) {
				tlb_read(check, &tlbp);
				if ((tlbp.tlb_hi == tlb.tlb_hi &&
				    (tlb.tlb_lo0 & PG_V || tlb.tlb_lo1 & PG_V)) ||
				    (pfn_to_pad(tlb.tlb_lo0) ==
				     pfn_to_pad(tlbp.tlb_lo0) &&
				     tlb.tlb_lo0 & PG_V) ||
				    (pfn_to_pad(tlb.tlb_lo1) ==
				     pfn_to_pad(tlbp.tlb_lo1) &&
				     tlb.tlb_lo1 & PG_V)) {
					db_printf("MATCH:\n");
					db_dump_tlb_cmd(tlbno, 1, 1, "");
					db_dump_tlb_cmd(check, 1, 1, "");
				}
			}
		}
		return;
	} else {
		if (have_addr && addr < ci->ci_hw.tlbsize) {
			tlbno = addr;
		} else {
			tlbno = 0;
			count = ci->ci_hw.tlbsize;
		}
	}
	last = tlbno + count;
	if (last > ci->ci_hw.tlbsize)
		last = ci->ci_hw.tlbsize;

	if (pid == -1)
		db_printf("current asid: %d\n", tlb_get_pid());
	for (; tlbno < last; tlbno++) {
		tlb_read(tlbno, &tlb);

		if (pid >= 0 &&
		    (tlb.tlb_hi & PG_ASID_MASK) != (pid << PG_ASID_SHIFT))
			continue;

		if (tlb.tlb_lo0 & PG_V || tlb.tlb_lo1 & PG_V) {
			vaddr_t va;
			uint asid;

			asid = (tlb.tlb_hi & PG_ASID_MASK) >> PG_ASID_SHIFT;
			va = tlb.tlb_hi & ~((vaddr_t)PG_ASID_MASK);
#ifdef CPU_R8000
			if ((int64_t)va < 0)
				asid = 0;	/* KV1 addresses ignore ASID */
			va |= ptoa((tlbno ^ asid) % 128);
#endif
			db_printf("%3d v=%016llx", tlbno, va);
			db_printf("/%02x ", asid);

			db_print_tlb(tlbno, tlb.tlb_lo0);
#ifndef CPU_R8000
			db_print_tlb(tlbno, tlb.tlb_lo1);
			db_printf(" sz=%x", tlb.tlb_mask);
#endif
		} else if (pid < 0) {
			db_printf("%3d v=invalid    ", tlbno);
		}
		db_printf("\n");
	}
}


struct db_command mips_db_command_table[] = {
	{ "tlb",	db_dump_tlb_cmd,	0,	NULL },
	{ "trap",	db_trap_trace_cmd,	0,	NULL },
#ifdef MULTIPROCESSOR
	{ "cpuinfo",    db_cpuinfo_cmd,         0,      NULL },
	{ "startcpu",   db_startproc_cmd,       0,      NULL },
	{ "stopcpu",    db_stopproc_cmd,        0,      NULL },
	{ "ddbcpu",     db_ddbproc_cmd,         0,      NULL },
#endif   
	{ NULL,		NULL,			0,	NULL }
};

void
db_machine_init()
{
#ifdef MULTIPROCESSOR
	int i;
#endif

extern char *ssym;
	db_machine_commands_install(mips_db_command_table);
#ifdef MULTIPROCESSOR
	for (i = 0; i < ncpus; i++) {
		get_cpu_info(i)->ci_ddb = CI_DDB_RUNNING;
	}
#endif
	if (ssym != NULL) {
		ddb_init();	/* Init symbols */
	}
}


#ifdef MULTIPROCESSOR
void
db_ddbproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int cpu_n;

	if (have_addr) {
		cpu_n = addr;
		if (cpu_n >= 0 && cpu_n < ncpus &&
		    cpu_n != cpu_number()) {
			db_stopcpu(cpu_n);
			db_switch_to_cpu = cpu_n;
			db_switch_cpu = 1;
			db_cmd_loop_done = 1;
		} else {
			db_printf("Invalid cpu %d\n", (int)addr);
		}
	} else {
		db_printf("CPU not specified\n");
	}
}

void
db_startproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int cpu_n;

	if (have_addr) {
		cpu_n = addr;
		if (cpu_n >= 0 && cpu_n < ncpus &&
		    cpu_n != cpu_number())
			db_startcpu(cpu_n);
		else
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (cpu_n = 0; cpu_n < ncpus; cpu_n++) {
			if (cpu_n != cpu_number()) {
				db_startcpu(cpu_n);
			}
		}
	}
}

void
db_stopproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int cpu_n;
	
	if (have_addr) {
		cpu_n = addr;
		if (cpu_n >= 0 && cpu_n < ncpus &&
		    cpu_n != cpu_number())
			db_stopcpu(cpu_n);
		else
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (cpu_n = 0; cpu_n < ncpus; cpu_n++) {
			if (cpu_n != cpu_number()) {
				db_stopcpu(cpu_n);
			}
		}
	}
}

void
db_startcpu(int cpu)
{
	if (cpu != cpu_number() && cpu < ncpus) {
		mtx_enter(&ddb_mp_mutex);
		get_cpu_info(cpu)->ci_ddb = CI_DDB_RUNNING;
		mtx_leave(&ddb_mp_mutex);
	}
}   

void
db_stopcpu(int cpu)
{
	mtx_enter(&ddb_mp_mutex);
	if (cpu != cpu_number() && cpu < ncpus &&
	    get_cpu_info(cpu)->ci_ddb != CI_DDB_STOPPED) {
		get_cpu_info(cpu)->ci_ddb = CI_DDB_SHOULDSTOP;  
		mtx_leave(&ddb_mp_mutex);
		mips64_send_ipi(cpu, MIPS64_IPI_DDB);
	} else {
		mtx_leave(&ddb_mp_mutex);
	}
}
#endif
