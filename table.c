/*
 * table.c - ktap table data structure manipulation function
 *
 * Copyright (C) 2012-2013 Jovi Zhang
 *
 * Author: Jovi Zhang <bookjovi@gmail.com>
 *         zhangwei(Jovi) <jovi.zhangwei@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef __KERNEL__
#include "ktap.h"
#else
#include "ktap_types.h"
#endif


#define MAXBITS         30
#define MAXASIZE        (1 << MAXBITS)


#define NILCONSTANT     {NULL}, KTAP_TNIL
const struct Tvalue ktap_nilobjectv = {NILCONSTANT};
#define ktap_nilobject	(&ktap_nilobjectv)

static const Node dummynode_ = {
	{NILCONSTANT}, /* value */
	{{NILCONSTANT, NULL}}, /* key */
};

#define gnode(t,i)      (&(t)->node[i])
#define gkey(n)         (&(n)->i_key.tvk)
#define gval(n)         (&(n)->i_val)
#define gnext(n)        ((n)->i_key.nk.next)

#define twoto(x)        (1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))

#define hashpow2(t,n)           (gnode(t, lmod((n), sizenode(t))))

#define hashmod(t,n)		(gnode(t, ((n) % ((sizenode(t)-1)|1))))

#define hashstr(t,str)          hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)        hashpow2(t, p)
#define hashnum(t, n)		hashmod(t, (unsigned int)n)
#define hashpointer(t,p)	hashmod(t, (unsigned long)(p))

#define dummynode	(&dummynode_)
#define isdummy(n)	((n) == dummynode)



Tvalue *table_set(ktap_State *ks, Table *t, const Tvalue *key);
void table_setint(ktap_State *ks, Table *t, int key, Tvalue *v);
static void setnodevector(ktap_State *ks, Table *t, int size);


static int ceillog2(unsigned int x)
{
	static const u8 log_2[256] = {
	0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	};

	int l = 0;

	x--;
	while (x >= 256) { l += 8; x >>= 8; }
	return l + log_2[x];
}


Table *table_new(ktap_State *ks)
{
	Table *t = &newobject(ks, KTAP_TTABLE, sizeof(Table), NULL)->h;

	t->flags = (u8)(~0);
	t->array = NULL;
	t->sizearray = 0;
	t->node = (Node *)dummynode;
	setnodevector(ks, t, 0);

	return t;
}

const Tvalue *table_getint(Table *t, int key)
{
	Node *n;

	if ((unsigned int)(key - 1) < (unsigned int)t->sizearray)
		return &t->array[key - 1];

	n = hashnum(t, key);
	do {
		if (ttisnumber(gkey(n)) && nvalue(gkey(n)) == key)
			return gval(n);
		else
			n = gnext(n);
	} while (n);

	return ktap_nilobject;
}


static Node *mainposition (const Table *t, const Tvalue *key)
{
	switch (ttype(key)) {
	case KTAP_TNUMBER:
		return hashnum(t, nvalue(key));
	case KTAP_TLNGSTR: {
		Tstring *s = rawtsvalue(key);
		if (s->tsv.extra == 0) {  /* no hash? */
			s->tsv.hash = string_hash(getstr(s), s->tsv.len, s->tsv.hash);
			s->tsv.extra = 1;  /* now it has its hash */
		}
		return hashstr(t, rawtsvalue(key));
		}
	case KTAP_TSHRSTR:
		return hashstr(t, rawtsvalue(key));
	case KTAP_TBOOLEAN:
		return hashboolean(t, bvalue(key));
	case KTAP_TLIGHTUSERDATA:
		return hashpointer(t, pvalue(key));
	case KTAP_TLCF:
		return hashpointer(t, fvalue(key));
	default:
		return hashpointer(t, gcvalue(key));
	}
}

static int arrayindex(const Tvalue *key)
{
	if (ttisnumber(key)) {
		ktap_Number n = nvalue(key);
		int k = (int)n;
		if ((ktap_Number)k == n)
			return k;
	}

	return -1;  /* `key' did not match some condition */
}



static int computesizes (int nums[], int *narray)
{
	int i;
	int twotoi;  /* 2^i */
	int a = 0;  /* number of elements smaller than 2^i */
	int na = 0;  /* number of elements to go to array part */
	int n = 0;  /* optimal size for array part */

	for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
		if (nums[i] > 0) {
			a += nums[i];
			if (a > twotoi/2) {  /* more than half elements present? */
				n = twotoi;  /* optimal size (till now) */
				na = a;  /* all elements smaller than n will go to array part */
			}
		}
		if (a == *narray)
			break;  /* all elements already counted */
	}
	*narray = n;
	return na;
}


static int countint(const Tvalue *key, int *nums)
{
	int k = arrayindex(key);

	if (0 < k && k <= MAXASIZE) {  /* is `key' an appropriate array index? */
		nums[ceillog2(k)]++;  /* count as such */
		return 1;
	} else
		return 0;
}


static int numusearray(const Table *t, int *nums)
{
	int lg;
	int ttlg;  /* 2^lg */
	int ause = 0;  /* summation of `nums' */
	int i = 1;  /* count to traverse all array keys */

	for (lg=0, ttlg=1; lg <= MAXBITS; lg++, ttlg *= 2) {  /* for each slice */
		int lc = 0;  /* counter */
		int lim = ttlg;

		if (lim > t->sizearray) {
			lim = t->sizearray;  /* adjust upper limit */
			if (i > lim)
				break;  /* no more elements to count */
		}

		/* count elements in range (2^(lg-1), 2^lg] */
		for (; i <= lim; i++) {
			if (!ttisnil(&t->array[i-1]))
				lc++;
		}
		nums[lg] += lc;
		ause += lc;
	}
	return ause;
}



static int numusehash(const Table *t, int *nums, int *pnasize)
{
	int totaluse = 0;  /* total number of elements */
	int ause = 0;  /* summation of `nums' */
	int i = sizenode(t);

	while (i--) {
		Node *n = &t->node[i];
		if (!isnil(gval(n))) {
			ause += countint(gkey(n), nums);
			totaluse++;
		}
	}

	*pnasize += ause;
	return totaluse;
}


static void setarrayvector(ktap_State *ks, Table *t, int size)
{
	int i;

	ktap_realloc(ks, t->array, t->sizearray, size, Tvalue);
	for (i = t->sizearray; i < size; i++)
		setnilvalue(&t->array[i]);

	t->sizearray = size;
}

static void setnodevector(ktap_State *ks, Table *t, int size)
{
	int lsize;

	if (size == 0) {  /* no elements to hash part? */
		t->node = (Node *)dummynode;  /* use common `dummynode' */
		lsize = 0;
	} else {
		int i;
		lsize = ceillog2(size);
		if (lsize > MAXBITS)
			ktap_runerror(ks, "table overflow");
		size = twoto(lsize);
		t->node = ktap_malloc(ks, size * sizeof(Node));
		for (i = 0; i < size; i++) {
			Node *n = gnode(t, i);
			gnext(n) = NULL;
			setnilvalue(gkey(n));
			setnilvalue(gval(n));
		}
	}

	t->lsizenode = (u8)lsize;
	t->lastfree = gnode(t, size);  /* all positions are free */
}

void table_resize(ktap_State *ks, Table *t, int nasize, int nhsize)
{
	int i;
	int oldasize = t->sizearray;
	int oldhsize = t->lsizenode;
	Node *nold = t->node;  /* save old hash ... */

	if (nasize > oldasize)  /* array part must grow? */
		setarrayvector(ks, t, nasize);

	/* create new hash part with appropriate size */
	setnodevector(ks, t, nhsize);

	if (nasize < oldasize) {  /* array part must shrink? */
		t->sizearray = nasize;
		/* re-insert elements from vanishing slice */
		for (i=nasize; i<oldasize; i++) {
			if (!ttisnil(&t->array[i]))
				table_setint(ks, t, i + 1, &t->array[i]);
		}

		/* shrink array */
		ktap_realloc(ks, t->array, oldasize, nasize, Tvalue);
	}

	/* re-insert elements from hash part */
	for (i = twoto(oldhsize) - 1; i >= 0; i--) {
		Node *old = nold+i;
		if (!ttisnil(gval(old))) {
			/* doesn't need barrier/invalidate cache, as entry was
			 * already present in the table
			 */
			setobj(ks, table_set(ks, t, gkey(old)), gval(old));
		}
	}

	if (!isdummy(nold))
		ktap_free(ks, nold); /* free old array */
}

void table_resizearray(ktap_State *ks, Table *t, int nasize)
{
	int nsize = isdummy(t->node) ? 0 : sizenode(t);
	table_resize(ks, t, nasize, nsize);
}

static void rehash(ktap_State *ks, Table *t, const Tvalue *ek)
{
	int nasize, na;
	int nums[MAXBITS+1];  /* nums[i] = number of keys with 2^(i-1) < k <= 2^i */
	int i;
	int totaluse;

	for (i = 0; i <= MAXBITS; i++)
		nums[i] = 0;  /* reset counts */

	nasize = numusearray(t, nums);  /* count keys in array part */
	totaluse = nasize;  /* all those keys are integer keys */
	totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
	/* count extra key */
	nasize += countint(ek, nums);
	totaluse++;
	/* compute new size for array part */
	na = computesizes(nums, &nasize);
	/* resize the table to new computed sizes */
	table_resize(ks, t, nasize, totaluse - na);
}


static Node *getfreepos(Table *t)
{
	while (t->lastfree > t->node) {
		t->lastfree--;
	if (isnil(gkey(t->lastfree)))
		return t->lastfree;
	}
	return NULL;  /* could not find a free place */
}


static Tvalue *table_newkey(ktap_State *ks, Table *t, const Tvalue *key)
{
	Node *mp;

	mp = mainposition(t, key);
	if (!isnil(gval(mp)) || isdummy(mp)) {  /* main position is taken? */
		Node *othern;
		Node *n = getfreepos(t);  /* get a free place */
		if (n == NULL) {  /* cannot find a free place? */
			rehash(ks, t, key);  /* grow table */
			/* whatever called 'newkey' take care of TM cache and GC barrier */
			return table_set(ks, t, key);  /* insert key into grown table */
		}

		othern = mainposition(t, gkey(mp));
		if (othern != mp) {  /* is colliding node out of its main position? */
			/* yes; move colliding node into free position */
			while (gnext(othern) != mp)
				othern = gnext(othern);  /* find previous */
			gnext(othern) = n;  /* redo the chain with `n' in place of `mp' */
			*n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
			gnext(mp) = NULL;  /* now `mp' is free */
			setnilvalue(gval(mp));
		} else {  /* colliding node is in its own main position */
			/* new node will go into free position */
			gnext(n) = gnext(mp);  /* chain new position */
			gnext(mp) = n;
			mp = n;
		}
	}
	setobj(ks, gkey(mp), key);
	return gval(mp);
}


/*
 * search function for short strings
 */
const Tvalue *table_getstr(Table *t, Tstring *key)
{
	Node *n = hashstr(t, key);

	do {  /* check whether `key' is somewhere in the chain */
		if (ttisshrstring(gkey(n)) && eqshrstr(rawtsvalue(gkey(n)), key))
			return gval(n);  /* that's it */
		else
			n = gnext(n);
	} while (n);

	return ktap_nilobject;
}


/*
 * main search function
 */
const Tvalue *table_get(Table *t, const Tvalue *key)
{
	switch (ttype(key)) {
	case KTAP_TNIL:
		return ktap_nilobject;
	case KTAP_TSHRSTR:
		return table_getstr(t, rawtsvalue(key));
	case KTAP_TNUMBER: {
		ktap_Number n = nvalue(key);
		int k = (int)n;
		if ((ktap_Number)k == nvalue(key)) /* index is int? */
			return table_getint(t, k);  /* use specialized version */
		/* else go through */
	}
	default: {
		Node *n = mainposition(t, key);
		do {  /* check whether `key' is somewhere in the chain */
			if (rawequalobj(gkey(n), key))
				return gval(n);  /* that's it */
			else
				n = gnext(n);
		} while (n);

		return ktap_nilobject;
	}
	}
}


Tvalue *table_set(ktap_State *ks, Table *t, const Tvalue *key)
{
	const Tvalue *p = table_get(t, key);

	if (p != ktap_nilobject)
		return (Tvalue *)p;
	else
		return table_newkey(ks, t, key);
}


void table_setvalue(ktap_State *ks, Table *t, const Tvalue *key, Tvalue *val)
{
	setobj(ks, table_set(ks, t, key), val);
}


void table_setint(ktap_State *ks, Table *t, int key, Tvalue *v)
{
	const Tvalue *p = table_getint(t, key);
	Tvalue *cell;

	if (p != ktap_nilobject)
		cell = (Tvalue *)p;
	else {
		Tvalue k;
		setnvalue(&k, key);
		cell = table_newkey(ks, t, &k);
	}

	setobj(ks, cell, v);
}

void table_free(ktap_State *ks, Table *t)
{
	if (t->sizearray > 0)
		ktap_free(ks, t->array);
	if (!isdummy(t->node))
		ktap_free(ks, t->node);

	ktap_free(ks, t);
}

void table_dump(ktap_State *ks, Table *t)
{
	int i, count = 0;

	ktap_printf(ks, "{");
	for (i = 0; i < t->sizearray; i++) {
		Tvalue *v = &t->array[i];

		if (isnil(v))
			continue;

		if (count)
			ktap_printf(ks, ", ");

		ktap_printf(ks, "(%d: ", i + 1);
		showobj(ks, v);
		ktap_printf(ks, ")");
		count++;
	}

	for (i = 0; i < sizenode(t); i++) {
		Node *n = &t->node[i];

		if (isnil(gkey(n)))
			continue;

		if (count)
			ktap_printf(ks, ", ");

		ktap_printf(ks, "(");
		showobj(ks, gkey(n));
		ktap_printf(ks, ": ");
		showobj(ks, gval(n));
		ktap_printf(ks, ")");
		count++;
	}
	ktap_printf(ks, "}");
}

struct table_hist_record {
	Tvalue key;
	Tvalue val;
};

#define DISTRIBUTION_STR "------------- Distribution -------------"
/* histogram: key should be number or string, value must be number */
void table_histogram(ktap_State *ks, Table *t)
{
	struct table_hist_record *thr;
	char dist_str[40];
	int i, ratio, total = 0, count = 0;

	thr = ktap_malloc(ks, sizeof(*thr) * (t->sizearray + sizenode(t)));

	ktap_printf(ks, "%20s%s%s\n", "value ", DISTRIBUTION_STR, " count");
	for (i = 0; i < t->sizearray; i++) {
		Tvalue *v = &t->array[i];

		if (isnil(v))
			continue;

		if (!ttisnumber(v))
			goto error;

		setnvalue(&thr[count++].key, i + 1);
		total += nvalue(v);
	}

	for (i = 0; i < sizenode(t); i++) {
		Node *n = &t->node[i];
		int num;

		if (isnil(gkey(n)))
			continue;

		if (!ttisnumber(gval(n)))
			goto error;

		num = nvalue(gval(n));
		setobj(ks, &thr[count].key, gkey(n));
		setobj(ks, &thr[count].val, gval(n));
		count++;
		total += nvalue(gval(n));
	}

	dist_str[sizeof(dist_str) - 1] = '\0';
	for (i = 0; i < count; i++) {
		Tvalue *key = &thr[i].key;
		Tvalue *val = &thr[i].val;

		memset(dist_str, ' ', sizeof(dist_str) - 1);
		ratio = (nvalue(val) * (sizeof(dist_str) - 1)) / total;
		memset(dist_str, '@', ratio);

		if (ttisstring(key)) {
			char buf[21] = {0};
			char *keystr;

			if (strlen(svalue(key)) > 20) {
				strncpy(buf, svalue(key), 16);
				memset(buf + 16, '.', 3);
				keystr = buf;
			} else
				keystr = svalue(key);

			ktap_printf(ks, "%20s |%s%-10d\n", keystr, dist_str,
					nvalue(val));
		} else
			ktap_printf(ks, "%20d | %s%-10d\n", nvalue(key),
					dist_str, nvalue(val));
	}

	goto out;

 error:
	ktap_printf(ks, "error: table histogram only handle "
			" (key: string/number val: number)\n");
 out:
	ktap_free(ks, thr);
}

