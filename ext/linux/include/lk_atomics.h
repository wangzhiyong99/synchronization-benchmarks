/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu_relax.h"

typedef struct {
	int counter;
} atomic_t;

typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
#ifndef _ASM_GENERIC_INT_LL64_H
typedef int64_t __s64;
typedef uint64_t __u64;
#endif

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;

static inline void prefetchw(const void *ptr) {
#if defined(__x64_64__)
	asm volatile("prefetchw	%P1\n" : : "m" (*(const char *) ptr));
#elif defined(__aarch64__)
	asm volatile("prfm pstl1keep, %a0\n" : : "p" (ptr));
#elif defined(__riscv) && defined(__riscv_zicbop)
    asm volatile("prefetch.w 0(%0)" : : "m" (ptr) : "memory");
#else
#endif
}

static __always_inline void __read_once_size(const volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(__u8 *)res = *(volatile __u8 *)p; break;
	case 2: *(__u16 *)res = *(volatile __u16 *)p; break;
	case 4: *(__u32 *)res = *(volatile __u32 *)p; break;
	case 8: *(__u64 *)res = *(volatile __u64 *)p; break;
	}
}

static __always_inline void __write_once_size(volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(volatile __u8 *)p = *(__u8 *)res; break;
	case 2: *(volatile __u16 *)p = *(__u16 *)res; break;
	case 4: *(volatile __u32 *)p = *(__u32 *)res; break;
	case 8: *(volatile __u64 *)p = *(__u64 *)res; break;
	}
}

#define READ_ONCE(x) \
	({ union { typeof(x) __val; char __c[1]; } __u; __read_once_size(&(x), __u.__c, sizeof(x)); __u.__val; })

#define WRITE_ONCE(x, val) \
	({ typeof(x) __val = (val); __write_once_size(&(x), &__val, sizeof(__val)); __val; })

static inline uint32_t atomic_read(const atomic_t *v) {
	return READ_ONCE((v)->counter);
}

static inline uint32_t atomic_cmpxchg_acquire32(uint32_t *ptr, uint32_t exp, uint32_t val) {
	uint32_t old;

#if defined(__x86_64__)
	asm volatile ("lock cmpxchgl %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__)
#if defined(USE_LSE)
	unsigned long tmp;

	asm volatile(
	"	mov	%w[tmp], %w[exp]\n"
	"	casa	%w[tmp], %w[val], %[ptr]\n"
	"	mov	%w[old], %w[tmp]\n"
	"	nop\n"
	"	nop\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldaxr	%w[old], %[ptr]\n"
	"	eor	%w[tmp], %w[old], %w[exp]\n"
	"	cbnz	%w[tmp], 2f\n"
	"	stxr	%w[tmp], %w[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#elif defined(__riscv) && !defined(__riscv_zacas)
	unsigned long tmp;
    asm volatile (
        "1: lr.w.aq %[old], %[ptr]\n"       
        "   bne %[old], %[exp], 2f\n"       
        "   sc.w %[tmp], %[val], %[ptr]\n"    
        "   bnez %[tmp], 1b\n"          
        "2:"
        : [old] "=&r" (old), [tmp] "=&r" (tmp) , [ptr] "+A" (*(uint32_t *)(ptr))
        : [exp] "r" (exp), [val] "r" (val)
        : "memory");
#elif defined(__riscv) && defined(__riscv_zacas)
    asm volatile("amocas.w.aq  %[exp], %[val], %[ptr]"
                : [exp] "=&r" (old), [ptr] "+A" (*(ptr))
                : "r[exp]" (exp), [val] "r" (val)
                : "memory");	
#else
	/* TODO: builtin atomic call */
#endif

	return old;
}

static inline uint32_t atomic_cmpxchg_release32(uint32_t *ptr, uint32_t exp, uint32_t val) {
	uint32_t old;

#if defined(__x86_64__)
	asm volatile ("lock cmpxchgl %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__)
#if defined(USE_LSE)
	unsigned long tmp;

	asm volatile(
	"	mov	%w[tmp], %w[exp]\n"
	"	casl	%w[tmp], %w[val], %[ptr]\n"
	"	mov	%w[old], %w[tmp]\n"
	"	nop\n"
	"	nop\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldxr	%w[old], %[ptr]\n"
	"	eor	%w[tmp], %w[old], %w[exp]\n"
	"	cbnz	%w[tmp], 2f\n"
	"	stlxr	%w[tmp], %w[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#elif defined(__riscv) && !defined(__riscv_zacas)
    unsigned long tmp;
    asm volatile (
        "1: lr.w %[old], %[ptr]\n"
        "   bne %[old], %[exp], 2f\n"
        "   sc.w.rl %[tmp], %[val], %[ptr]\n"
        "   bnez %[tmp], 1b\n"
        "2:"
        : [old] "=&r" (old), [tmp] "=&r" (tmp) , [ptr] "+A" (*(uint32_t *)(ptr))
        : [exp] "r" (exp), [val] "r" (val)
        : "memory");
#elif defined(__riscv) && defined(__riscv_zacas)
    asm volatile("amocas.w.rl  %[exp], %[val], %[ptr]"
                : [exp] "=&r" (old), [ptr] "+A" (*(uint32_t *)(ptr))
                : "r[exp]" (exp), [val] "r" (val)
                : "memory");
#else
	/* TODO: builtin atomic call */
#endif

	return old;
}

static inline uint32_t atomic_cmpxchg_relaxed32(uint32_t *ptr, uint32_t exp, uint32_t val) {
	uint32_t old;

#if defined(__x86_64__)
	asm volatile ("lock cmpxchgl %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__)
#if defined(USE_LSE)
	unsigned long tmp;

	asm volatile(
	"	mov	%w[tmp], %w[exp]\n"
	"	cas	%w[tmp], %w[val], %[ptr]\n"
	"	mov	%w[old], %w[tmp]\n"
	"	nop\n"
	"	nop\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldxr	%w[old], %[ptr]\n"
	"	eor	%w[tmp], %w[old], %w[exp]\n"
	"	cbnz	%w[tmp], 2f\n"
	"	stxr	%w[tmp], %w[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#elif defined(__riscv) && !defined(__riscv_zacas)
    unsigned long tmp;
    asm volatile (
        "1: lr.w.aq %[old], %[ptr]\n"
        "   bne %[old], %[exp], 2f\n"
        "   sc.w.rl %[tmp], %[val], %[ptr]\n"
        "   bnez %[tmp], 1b\n"
        "2:"
        : [old] "=&r" (old), [tmp] "=&r" (tmp) , [ptr] "+A" (*(uint32_t *)(ptr))
        : [exp] "r" (exp), [val] "r" (val)
        : "memory");
#elif defined(__riscv) && defined(__riscv_zacas)
    asm volatile("amocas.w.aqrl  %[exp], %[val], %[ptr]"
                : [exp] "=&r" (old), [ptr] "+A" (*(uint32_t *)(ptr))
                : "r[exp]" (exp), [val] "r" (val)
                : "memory");
#else
	/* TODO: builtin atomic call */
#endif

	return old;
}

static inline int atomic_try_cmpxchg_acquire32(uint32_t *ptr, uint32_t *exp, uint32_t val) {
	uint32_t tmp = *exp;
	uint32_t old = atomic_cmpxchg_acquire32(ptr, tmp, val);
	if (old != tmp) {
		*exp = old;
		return 0;
	} else  {
		return 1;
	}
}

static inline int atomic_try_cmpxchg_relaxed32(uint32_t *ptr, uint32_t *exp, uint32_t val) {
	uint32_t tmp = *exp;
	uint32_t old = atomic_cmpxchg_relaxed32(ptr, tmp, val);
	if (old != tmp) {
		*exp = old;
		return 0;
	} else  {
		return 1;
	}
}

/**
 * atomic_fetch_or_acquire() - atomic bitwise OR with acquire ordering
 * @i: int value
 * @v: pointer to atomic_t
 *
 * Atomically updates @v to (@v | @i) with acquire ordering.
 *
 * Safe to use in noinstr code; prefer atomic_fetch_or_acquire() elsewhere.
 *
 * Return: The original value of @v.
 */
static __always_inline uint32_t
atomic_fetch_or_acquire32(uint32_t i, atomic_t *v)
{
#if defined(__x86_64__)
	uint32_t old_val = v->counter;
	while(!atomic_try_cmpxchg_acquire32((uint32_t *)&v->counter, &old_val, old_val | i));
	return old_val;
#elif defined(__aarch64__)
	uint32_t old_val;
#if defined(USE_LSE)
	asm volatile ( \
	"ldseta %w[_val], %w[_old], %[_loc]\n"
		: [_old] "=&r" (old_val)
		: [_loc] "Q" (*(uint32_t *)(&v->counter)), [_val] "r" (i)
		: "memory");
#else
	uint32_t tmp, new_val;
	asm volatile(
	"	prfm  pstl1strm, [%[_loc]]\n"
	"1:	ldaxr %w[_old], [%[_loc]]\n"
	"	orr   %w[_new_val], %w[_old], %w[_val]\n"
	"	stlxr %w[_tmp], %w[_new_val], [%[_loc]]\n"
	"	cbnz  %w[_tmp], 1b\n"
	: [_old]"=&r" (old_val), [_new_val] "=&r" (new_val), [_tmp] "=&r" (tmp)
	: [_loc] "r" (&v->counter), [_val] "r" (i)
	: "memory");
#endif
	return old_val;
#elif defined(__riscv)
	uint32_t old_val, new_val, tmp;
	asm volatile(
		"1: lr.w.aq %[old], %[ptr]\n"
		"   or %[val], %[old], %[_val]\n"
		"   sc.w.rl %[tmp], %[val], %[ptr]\n"
		"   bnez %[tmp], 1b\n"
		: [old] "=&r" (old_val), [val] "=&r" (new_val), [tmp] "=&r" (tmp)
		: [ptr] "A" (*((uint32_t *)(&v->counter))), [_val] "r" (i)
		: "memory"
    );
	return old_val;
#else
	#error "Unable to define atomic_fetch_or_acquire"
#endif
}


static inline uint16_t xchg_release16(uint16_t *ptr, uint16_t val) {
#if defined(__x86_64__)
	asm volatile ("xchgw %w0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__)
#if defined(USE_LSE)
	uint16_t old;

	asm volatile(
	"	swplh	%w[val], %w[old], %[ptr]\n"
	"	nop\n"
	"	nop\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(uint32_t *)ptr)
	: [val] "r" (val)
	: );
#else
	uint16_t tmp, old;

	asm volatile(
	"1:	ldxrh	%w[old], %[ptr]\n"
	"	stlxrh	%w[tmp], %w[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(uint32_t *)ptr)
	: [val] "r" (val)
	: );
#endif

	val = old;
#else
	/* TODO: builtin atomic call */
#endif

	return val;
}

/* -----------------  */

#if defined(__aarch64__)
#define __stringify_1(x...)     #x
#define __stringify(x...)       __stringify_1(x)

#define wfe()              asm volatile("wfe" : : : "memory")

#define isb()              asm volatile("isb" : : : "memory")

/*
 * Unlike read_cpuid, calls to read_sysreg are never expected to be
 * optimized away or replaced with synthetic values.
 */
#define read_sysreg(r) ({                                       \
        u64 __val;                                              \
        asm volatile("mrs %0, " __stringify(r) : "=r" (__val)); \
        __val;                                                  \
})



/*
 * Ensure that reads of the counter are treated the same as memory reads
 * for the purposes of ordering by subsequent memory barriers.
 *
 * This insanity brought to you by speculative system register reads,
 * out-of-order memory accesses, sequence locks and Thomas Gleixner.
 *
 * http://lists.infradead.org/pipermail/linux-arm-kernel/2019-February/631195.html
 */
#define arch_counter_enforce_ordering(val) do {                         \
        u64 tmp, _val = (val);                                          \
                                                                        \
        asm volatile(                                                   \
        "       eor     %0, %1, %1\n"                                   \
        "       add     %0, sp, %0\n"                                   \
        "       ldr     xzr, [%0]"                                      \
        : "=r" (tmp) : "r" (_val));                                     \
} while (0)


static __always_inline u64 __arch_counter_get_cntvct(void)
{
        u64 cnt;

        isb();
        cnt = read_sysreg(cntvct_el0);
        arch_counter_enforce_ordering(cnt);
        return cnt;
}


static inline u64 get_cycles(void) {
	return __arch_counter_get_cntvct();
}

#endif


/* ------------------- */
static inline void cpu_relax (void) {
	__cpu_relax();
}


// ------------------------------

#ifndef CPU_RELAX
#define CPU_RELAX(x) cpu_relax()
#endif

#ifndef INCREMENT_COUNTER
#define INCREMENT_COUNTER(x)
#endif


// -------------------------------

#define barrier() __asm__ __volatile__("" : : :"memory")

#define smp_read_barrier_depends() do { } while (0)

#ifndef smp_store_release
#define smp_store_release(p, v) __smp_store_release(p, v)
#endif

#ifndef smp_load_acquire
#define smp_load_acquire(p) __smp_load_acquire(p)
#endif

#if defined(__aarch64__)
static inline void __cmpwait_relaxed(volatile void *ptr,
				     unsigned long val)
{
	unsigned long tmp;

	asm volatile(
	"	ldxr	%w[tmp], %[v]\n"
	"	eor	%w[tmp], %w[tmp], %w[val]\n"
	"	cbnz	%w[tmp], 1f\n"
	"	wfe\n"
	"1:"
	: [tmp] "=&r" (tmp), [v] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val));
}

#define __smp_store_release(p, v)					\
do {									\
	union { typeof(*p) __val; char __c[1]; } __u =			\
		{ .__val = (typeof(*p)) (v) };				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("stlrb %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u8 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile ("stlrh %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u16 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile ("stlr %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u32 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 8:								\
		asm volatile ("stlr %1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u64 *)__u.__c)		\
				: "memory");				\
		break;							\
	}								\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	union { typeof(*p) __val; char __c[1]; } __u;			\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("ldarb %w0, %1"				\
			: "=r" (*(__u8 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	case 2:								\
		asm volatile ("ldarh %w0, %1"				\
			: "=r" (*(__u16 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	case 4:								\
		asm volatile ("ldar %w0, %1"				\
			: "=r" (*(__u32 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	case 8:								\
		asm volatile ("ldar %0, %1"				\
			: "=r" (*(__u64 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	}								\
	__u.__val;							\
})

#define smp_cond_load_acquire(ptr, cond_expr)				\
({									\
	typeof(ptr) __PTR = (ptr);					\
	typeof(*ptr) VAL;						\
	for (;;) {							\
		VAL = smp_load_acquire(__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, (unsigned long) (VAL));	\
	}								\
	VAL;								\
})

#define smp_cond_load_relaxed(ptr, cond_expr)				\
({									\
	typeof(ptr) __PTR = (ptr);					\
	typeof(*ptr) VAL;						\
	for (;;) {							\
		VAL = READ_ONCE(*__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, (unsigned long) (VAL));	\
		INCREMENT_COUNTER(osq_lock_locked_spins);               \
	}								\
	VAL;								\
})

#else
#define __smp_store_release(p, v)					\
do {									\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	barrier();							\
	___p1;								\
})

#define smp_cond_load_acquire(ptr, cond_expr) ({		\
	typeof(ptr) __PTR = (ptr);				\
	typeof(*ptr) VAL;					\
	for (;;) {						\
		VAL = READ_ONCE(*__PTR);			\
		if (cond_expr)					\
			break;					\
		cpu_relax();					\
	}							\
	barrier();						\
	VAL;							\
})

#define smp_cond_load_relaxed(ptr, cond_expr) ({		\
	typeof(ptr) __PTR = (ptr);				\
	typeof(*ptr) VAL;					\
	for (;;) {						\
		VAL = READ_ONCE(*__PTR);			\
		if (cond_expr)					\
			break;					\
		CPU_RELAX(thread_to_relax[thread_number][0]);   \
		INCREMENT_COUNTER(osq_lock_locked_spins);       \
	}							\
	VAL;							\
})

#endif

#define arch_mcs_spin_lock_contended(l)					\
do {									\
	while (!(smp_load_acquire(l)))					\
		cpu_relax();						\
} while (0)

#define arch_mcs_spin_unlock_contended(l)				\
	smp_store_release((l), 1)

#define ATOMIC_INIT(i)  { (i) }
#define atomic_read(v)                  READ_ONCE((v)->counter)
#define atomic_set(v, i)                WRITE_ONCE(((v)->counter), (i))

# define likely(x)      __builtin_expect(!!(x), 1)
# define unlikely(x)    __builtin_expect(!!(x), 0)

#ifndef bool
#define bool int
#define true 1
#define false 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define atomic_cond_read_acquire(v, c) smp_cond_load_acquire(&(v)->counter, (c))
#define atomic_cond_read_relaxed(v, c) smp_cond_load_relaxed(&(v)->counter, (c))

/* vim: set tabstop=8 shiftwidth=8 softtabstop=8 noexpandtab : */
