/*
    ec.c

    This is part of OsEID (Open source Electronic ID)

    Copyright (C) 2015,2016 Peter Popovec, popovec.peter@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    elliptic curve cryptography routines

    WARNING:

    tested curves (with fast reduction algo)
    nistp192/prime192v1/secp192r1
    nistp256/secp256r1/prime256v1
    secp384r1
    secp256k1

*/
#include <string.h>
#include <stdint.h>
#include "rnd.h"
#include "ec.h"
#include <stdio.h>
// prototypes
extern uint8_t mod_len;

uint8_t
mp_get_len ()
{
  return mod_len;
}

static void
mp_set_len (uint8_t a)
{
  mod_len = a;
}

extern void rsa_mul_384 (uint8_t * r, uint8_t * a, uint8_t * b);
extern void rsa_mul_256 (uint8_t * r, uint8_t * a, uint8_t * b);
extern void rsa_mul_192 (uint8_t * r, uint8_t * a, uint8_t * b);
extern void rsa_square_384 (uint8_t * r, uint8_t * a);
extern void rsa_square_256 (uint8_t * r, uint8_t * a);
extern void rsa_square_192 (uint8_t * r, uint8_t * a);

#ifdef EC_DEBUG
#include <stdio.h>
#define  DPRINT(msg...) fprintf(stderr,msg)
static void __attribute__ ((unused)) hex_print_f (FILE * f, bignum_t * t)
{
  int8_t i;
  uint8_t *T = (void *) t;

  fprintf (f, "0x");
  for (i = mp_get_len () - 1; i >= 0; i--)
    fprintf (f, "%02X", T[i]);
  fprintf (f, "\n");
}
#else
#define DPRINT(msg...)
#endif

typedef struct
{
  uint8_t value[MP_BYTES * 2];
} bigbignum_t;


// functions map ..  FINAL is function that not call any other functions (except memcpy/memset)        
// 

// ec mathematics (point in projective representation!)
static uint8_t ecisinf (ec_point_t * point);	// [is_zero]
static void ec_double (ec_point_t * a);	// [add_mod, sub_mod, field_mul, field_sqr, ecisinf]
static uint8_t ec_add_ (ec_point_t * a, ec_point_t * b);	// [ecisinf, field_mul, field_sqr, (ec_double)]
static void ec_mul (ec_point_t * result, bignum_t * f, ec_point_t * point);	// [ec_add_, ec_double, mp_shiftr]
static void ec_projectify (ec_point_t * r);

//return projective representation to affinite
static void ec_affinify (ec_point_t * point, struct ec_param *ec);
/**************************************************************************
*                   field mathematics (mod p192)                          *
***************************************************************************/
static void field_sqr (bignum_t * r, bignum_t * a);
static void field_mul (bignum_t * r, bignum_t * a, bignum_t * b);
static void fast192reduction (bignum_t * result, bigbignum_t * bn);
/**************************************************************************
*                       modular arithmetic                               *
***************************************************************************/
static void add_mod (bignum_t * result, bignum_t * a, bignum_t * b,
		     bignum_t * mod);
static void sub_mod (bignum_t * result, bignum_t * a, bignum_t * b,
		     bignum_t * mod);
static void inv_mod (bignum_t * result, bignum_t * a, bignum_t * mod);
static void mul_mod (bignum_t * result, bignum_t * a, bignum_t * b,
		     bignum_t * mod);
static void mp_mod (bigbignum_t * result, bignum_t * mod);
/**************************************************************************
*                     basic multiple precision arithmetic                *
***************************************************************************/
#ifdef HAVE_MP_ADD
extern uint8_t mp_add (bignum_t * result, bignum_t * a, bignum_t * b);
#else
static uint8_t mp_add (bignum_t * result, bignum_t * a, bignum_t * b);
#endif

#ifdef HAVE_MP_SUB
extern uint8_t mp_sub (bignum_t * result, bignum_t * a, bignum_t * b);
#else
static uint8_t mp_sub (bignum_t * result, bignum_t * a, bignum_t * b);
#endif

static void mp_mul (bigbignum_t * r, bignum_t * b, bignum_t * a);
static void mp_square (bigbignum_t * r, bignum_t * a);

#ifndef HAVE_MP_SHIFTL
static uint8_t mp_shiftl (bignum_t * result);
#else
extern uint8_t mp_shiftl (bignum_t * result);
#endif

#ifndef HAVE_MP_SHIFTR
static uint8_t mp_shiftr (bignum_t * result);
#else
extern uint8_t mp_shiftr (bignum_t * result);
#endif

#ifndef HAVE_MP_SHIFTR_C
static uint8_t mp_shiftr_c (bignum_t * result, uint8_t carry);
#else
extern uint8_t mp_shiftr_c (bignum_t * result, uint8_t carry);
#endif

#ifndef HAVE_MP_SHIFTR_2N
static uint8_t mp_shiftr_2N (bigbignum_t * result);
#else
extern uint8_t mp_shiftr_2N (bigbignum_t * result);
#endif

#ifdef HAVE_MP_SUB_2N
extern uint8_t mp_sub_2N (bigbignum_t * result, bigbignum_t * a,
			  bigbignum_t * b);
#else
static uint8_t mp_sub_2N (bigbignum_t * result, bigbignum_t * a,
			  bigbignum_t * b);
#endif

static uint8_t is_zero (bignum_t * k);

#ifdef HAVE_MP_CMP
extern int8_t mp_cmp (bignum_t * c, bignum_t * d);
#else
static int8_t mp_cmp (bignum_t * c, bignum_t * d);
#endif

static uint8_t mp_test_even (bignum_t * r);
static uint8_t mp_is_1 (bignum_t * r);
static void mp_set_to_1 (bignum_t * r);

#define mp_set(r,c) memcpy (r, c, mp_get_len ())
#define mp_clear(r) memset (r, 0, mp_get_len ());


// to fast access prime, A, curve_type .. fill this in any public fcion!
static bignum_t *field_prime __attribute__ ((section (".noinit")));
#ifndef NIST_ONLY
static bignum_t *param_a __attribute__ ((section (".noinit")));
#endif
static uint8_t curve_type __attribute__ ((section (".noinit")));

//Change point from affine to projective
static void
ec_projectify (ec_point_t * r)
{
  DPRINT ("%s\n", __FUNCTION__);

  memset (&(r->Z), 0, mp_get_len ());
  r->Z.value[0] = 1;
}



#define field_add(r,a,b) add_mod(r,a,b,field_prime)
#define field_sub(r,a,b) sub_mod(r,a,b,field_prime)

// if( c >= d) return c-d in r
// else do modular subtract  ->  r= c-d ,r += p
static void
sub_mod (bignum_t * r, bignum_t * a, bignum_t * b, bignum_t * mod)
{
  uint8_t carry;

  DPRINT ("%s\n", __FUNCTION__);

  carry = mp_sub (r, a, b);
  if (carry)
    mp_add (r, r, mod);
}

static void
add_mod (bignum_t * r, bignum_t * a, bignum_t * b, bignum_t * mod)
{
  uint8_t carry;

  DPRINT ("%s\n", __FUNCTION__);

  carry = mp_add (r, a, b);
  if (carry)
    mp_sub (r, r, mod);
  else if (mp_cmp (r, mod) == 1)
    mp_sub (r, r, mod);
}

static void
mul_mod (bignum_t * c, bignum_t * a, bignum_t * b, bignum_t * mod)
{
  bigbignum_t bn;

  DPRINT ("%s\n", __FUNCTION__);

  mp_mul (&bn, a, b);
  mp_mod (&bn, mod);
  memset (c, 0, mp_get_len ());
  memcpy (c, &bn, mp_get_len ());
}

static uint8_t
is_zero (bignum_t * k)
{
  uint8_t i;

  DPRINT ("%s\n", __FUNCTION__);

  for (i = 0; i < mp_get_len (); i++)
    {
      if (k->value[i])
	return 0;
    }
  return 1;
}

static uint8_t
ecisinf (ec_point_t * point)
{
  return is_zero (&point->Y);
}

#ifndef NIST_ONLY
/*
FAST REDUCTION for secp384k1 curve
p = FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFE FFFFFC2F
//code based on   http://cse.iitkgp.ac.in/~debdeep/osscrypto/psec/downloads/PSEC-KEM_prime.pdf

1. c0 = a[255:0];
2. c1 = a[511:256];
3. w0 = c0;
4. w1 = {c1[223:0], 32'd0};
5. w2 = {c1[246:0], 9'd0};
6. w3 = {c1[247:0], 8'd0};
7. w4 = {c1[248:0], 7'd0};
8. w5 = {c1[249:0], 6'd0};
9. w6 = {c1[251:0], 4'd0};
10. w7 = c1;
11. k1 = c1[255:252] + c1[255:250];
12. k2 = k1 + c1[255:249];
13. k3 = k2 + c1[255:248];
14. k4 = k3 + c1[255:247];
15. s1 = k4 + c1[255:224];
16. k11 = {s1, 2'd0} + {s1, 1'd0} + s1;
17. k12 = {k11, 7'd0};
18. k13 = {s1, 4'd0} + s1;
19. k14 = {s1, 6'd0} + k13;
20. k = {s1, 32'd0} + k12 + k14;
21. s = c0 + k + w1 + w2 + w3 + w4 + w5 + w6 + w7;
22. Return s mod p.
(code below with small optimizations)
*/
static void
secp256k1reduction (bignum_t * result, bigbignum_t * bn)
{
  DPRINT ("%s\n", __FUNCTION__);


  bignum_t w1, k;
  uint8_t *a = (uint8_t *) bn;
  uint16_t acc, k1;


  field_add (result, (bignum_t *) a, (bignum_t *) (a + 32));

  memset (&w1, 0, 4);
  memcpy (&w1.value[4], a + 32, 28);
  field_add (result, result, &w1);

  memcpy (&w1.value[1], a + 32, 31);
  field_add (result, result, &w1);

  mp_shiftl (&w1);
  field_add (result, result, &w1);

  memcpy (&w1, a + 32, 32);
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  field_add (result, result, &w1);

  mp_shiftl (&w1);
  mp_shiftl (&w1);
  field_add (result, result, &w1);

  mp_shiftl (&w1);
  field_add (result, result, &w1);

  acc = bn->value[63];
  k1 = acc >> 4;
  k1 += acc >> 2;
  k1 += (acc >> 1);
  k1 += acc;
  k1 += (acc << 1);
  acc = bn->value[62] >> 7;
  k1 += acc;

// there is enough to calculate 80 bites for k, use 16 bytes
// because mp_add in ASM is designed to use 64 bit in one loop
  mp_set_len (16);

  memset (&w1, 0, 32);
  memcpy (&w1, a + 60, 4);

  memset (&k, 0, 32);
  k.value[0] = k1 & 0xff;
  k.value[1] = (k1 >> 8) & 0xff;
  mp_add (&w1, &w1, &k);

  memset (&k, 0, 2);
  memcpy (&k.value[4], &w1, 28);	//32
  mp_add (&k, &k, &w1);
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  mp_add (&k, &k, &w1);		//4
  mp_shiftl (&w1);
  mp_shiftl (&w1);
  mp_add (&k, &k, &w1);		//6
  mp_shiftl (&w1);
  mp_add (&k, &k, &w1);		//7
  mp_shiftl (&w1);
  mp_add (&k, &k, &w1);		//8
  mp_shiftl (&w1);
  mp_add (&k, &k, &w1);		//9

  mp_set_len (32);		// secp256k1 uses always 32 bytes for number
  field_add (result, result, &k);
}

#endif


/*
FAST REDUCTION for secp384r1 curve
P384 = 39402006196394479212279040100143613805079739270465446667948293404245721771496870329047266088258938001861606973112319
P384 = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF FFFE FFFF FFFF 0000 0000 0000 0000 FFFF FFFF
(2^384-2^128-2^96+2^32-1)

1: {Note: the Ai are 32bit quantities.}
2:*t      ( a11 || a10 || a9  || a8  || a7  || a6  || a5  || a4  || a3  || a2  || a1  || a0  )
3:*s1     ( 0   ||  0  ||  0  ||  0  ||  0  || a23 || a22 || a21 ||  0  ||  0  ||  0  ||  0  )
4: s2     ( a23 || a22 || a21 || a20 || a19 || a18 || a17 || a16 || a15 || a14 || a13 || a12 )
5: s3     ( a20 || a19 || a18 || a17 || a16 || a15 || a14 || a13 || a12 || a23 || a22 || a21 )
6: s4     ( a19 || a18 || a17 || a16 || a15 || a14 || a13 || a12 || a20 ||  0  || a23 ||  0  )
7:*s5     ( 0   ||  0  ||  0  ||  0  || a23 || a22 || a21 || a20 ||  0  ||  0  ||  0  ||  0  )
8:*s6     ( 0   ||  0  ||  0  ||  0  ||  0  ||  0  || a23 || a22 || a21 ||  0  ||  0  || a20 )
9: d1     ( a22 || a21 || a20 || a19 || a18 || a17 || a16 || a15 || a14 || a13 || a12 || a23 )
10: d2    ( 0   ||  0  ||  0  ||  0  ||  0  ||  0  ||  0  || a23 || a22 || a21 || a20 ||  0  )
11: d3    ( 0   ||  0  ||  0  ||  0  ||  0  ||  0  ||  0  || a23 || a23 ||  0  ||  0  ||  0  )
12: d1 = p384 - d1
13: r = t + 2 s1 + s2 + s3 + s4 + s5 + s6 - d1 - d2 - d3
14: Reduce r mod p384 by subtraction of up to four multiples of p384 
*/
void
fast384reduction (bignum_t * result, bigbignum_t * bn)
{
  bignum_t tmp;
  uint8_t *ptr = (void *) bn;
  uint8_t *ttmp = (void *) &tmp;
  uint8_t *r = (void *) result;

// 2x S1
  mp_set_len (32);
  memset (ttmp, 0, 48);
  memcpy (ttmp + 4 * 4, ptr + 21 * 4, 3 * 4);
  mp_shiftl (&tmp);
// 1x S6
  memset (result, 0, 48);
  memcpy (r + 3 * 4, ptr + 21 * 4, 3 * 4);
  memcpy (result, ptr + 20 * 4, 4);
  mp_add (result, result, &tmp);

  mp_set_len (40);
// 1x S5
  memset (ttmp, 0, 48);
  memcpy (ttmp + 4 * 4, ptr + 20 * 4, 4 * 4);
  mp_add (result, result, &tmp);

  mp_set_len (48);
// T
  field_add (result, result, (bignum_t *) bn);
// 1x S2
  field_add (result, result, (bignum_t *) & bn->value[48]);
// 1x S3 - reuse upper part of BN a20..a12, copy only a23..a21 to low part
  memcpy (&bn->value[48 - 3 * 4], &bn->value[21 * 4], 3 * 4);
  field_add (result, result, (bignum_t *) & bn->value[48 - 3 * 4]);
// 1x S4 - reuse upper part of BN
  memset (&bn->value[48 - 4 * 4], 0, 4 * 4);
  memcpy (&bn->value[48 - 3 * 4], &bn->value[23 * 4], 4);
  memcpy (&bn->value[48 - 1 * 4], &bn->value[20 * 4], 4);
  field_add (result, result, (bignum_t *) & bn->value[48 - 4 * 4]);
// D1  reuse upper part of BN
  memcpy (&bn->value[48 - 1 * 4], &bn->value[23 * 4], 4);
  field_sub (result, result, (bignum_t *) & bn->value[48 - 1 * 4]);

  mp_set_len (24);
// D2 + D3
  // D2 to ttmp
  memset (ttmp, 0, 48);
  memcpy (ttmp + 4, ptr + 20 * 4, 4 * 4);
  // D3 into low part
  memset (bn, 0, 48);
  memcpy (&bn->value[3 * 4], ptr + 23 * 4, 4);
  memcpy (&bn->value[4 * 4], ptr + 23 * 4, 4);
  mp_add (&tmp, &tmp, (bignum_t *) bn);
  mp_set_len (48);
// result - (D2+D3)
  field_sub (result, result, &tmp);
}

/*
FAST REDUCTION for nistp256/secp256r1/prime256v1 OID 1.2.840.10045.3.1.7 curve

result = bn (mod P256)

P256 = 115792089210356248762697446949407573530086143415290314195533631308867097853951
P256 = 0xFF FF FF FF  00 00 00 01  00 00 00 00   00 00 00 00  00 00 00 00  FF FF FF FF  FF FF FF FF  FF FF FF FF

// original NIST desc..
1: { Ai in 32bit quantities.}
2: t   ( A7  || A6  || A5  || A4  || A3  || A2  || A1  || A0  )
3: s1  ( A15 || A14 || A13 || A12 || A11 || 0   || 0   || 0   )
4: s2  ( 0   || A15 || A14 || A13 || A12 || 0   || 0   || 0   )
5: s3  ( A15 || A14 || 0   || 0   || 0   || A10 || A9  || A8  )
6: s4  ( A8  || A13 || A15 || A14 || A13 || A11 || A10 || A9  )
7: d1  ( A10 || A8  || 0   || 0   || 0   || A13 || A12 || A11 )
8: d2  ( A11 || A9  || 0   || 0   || A15 || A14 || A13 || A12 )
9: d3  ( A12 || 0   || A10 || A9  || A8  || A15 || A14 || A13 )
10: d4 ( A13 || 0   || A11 || A10 || A9  || 0   || A15 || A14 )
11: d1 = 2p256 - d1
12: d2 = 2p256 - d2
13: d3 = 2p256 - d3
14: d4 = 2p256 - d4
15: r = t + 2 s1 + 2 s2 + s3 + s4 + d1 + d2 + d3 + d4

// first some changes to minimize memory copy
move some part of S4 to S2, from S3 to S1, then use field_sub/add

: t   ( A7  || A6  || A5  || A4  || A3  || A2  || A1  || A0  )
: s1  ( A15 || A14 || A13 || A12 || A11 || A10 || A9  || A8  )
: s1x ( A15 || A14 || A13 || A12 || A11 || 0   || 0   || 0   )
: s3  ( A15 || A14 || 0   || 0   || 0   || 0   || 0   || 0   )
: s2  ( 0   || A15 || A14 || A13 || A12 || A11 || A10 || A9  )
: s2x ( 0   || A15 || A14 || A13 || A12 || 0   || 0   || 0   )
: s4  ( A8  || A13 || A15 || A14 || A13 || 0   || 0   || 0   )

: d1  ( A10 || A8  || 0   || 0   || 0   || A13 || A12 || A11 )
: d2  ( A11 || A9  || 0   || 0   || A15 || A14 || A13 || A12 )
: d3  ( A12 || 0   || A10 || A9  || A8  || A15 || A14 || A13 )
: d4  ( A13 || 0   || A11 || A10 || A9  || 0   || A15 || A14 )

: r = t + s1 + s1x + 2 s2 + s3 + s4 - d1 - d2 - d3 - d4
*/

static void
fast256reduction (bignum_t * result, bigbignum_t * bn)
{
  uint8_t *ptr_l = (void *) bn;

  // T+s1
  field_add (result, (bignum_t *) bn, (bignum_t *) & bn->value[32]);
  // s1x
  memset (ptr_l, 0, 3 * 4);
  memcpy (ptr_l + 3 * 4, ptr_l + 11 * 4, 5 * 4);
  field_add (result, result, (bignum_t *) ptr_l);
  // s3
  memset (ptr_l, 0, 6 * 4);
  field_add (result, result, (bignum_t *) ptr_l);
  // s2
  memcpy (ptr_l + 0 * 4, ptr_l + 9 * 4, 7 * 4);
  memset (ptr_l + 7 * 4, 0, 4);
  field_add (result, result, (bignum_t *) ptr_l);
  //s2x
  memset (ptr_l, 0, 3 * 4);
  field_add (result, result, (bignum_t *) ptr_l);
  //s4
  memcpy (ptr_l + 3 * 4, ptr_l + 13 * 4, 3 * 4);
  memcpy (ptr_l + 6 * 4, ptr_l + 13 * 4, 1 * 4);
  memcpy (ptr_l + 7 * 4, ptr_l + 8 * 4, 1 * 4);
  field_add (result, result, (bignum_t *) ptr_l);
  //d1
  memcpy (ptr_l, ptr_l + 11 * 4, 3 * 4);
  memset (ptr_l + 3 * 4, 0, 3 * 4);
  memcpy (ptr_l + 6 * 4, ptr_l + 8 * 4, 4);
  memcpy (ptr_l + 7 * 4, ptr_l + 10 * 4, 4);
  field_sub (result, result, (bignum_t *) ptr_l);
  //d2
  memcpy (ptr_l, ptr_l + 12 * 4, 4 * 4);
  memcpy (ptr_l + 6 * 4, ptr_l + 9 * 4, 4);
  memcpy (ptr_l + 7 * 4, ptr_l + 11 * 4, 4);
  field_sub (result, result, (bignum_t *) ptr_l);
  //d3
  memcpy (ptr_l, ptr_l + 4 * 13, 3 * 4);
  memcpy (ptr_l + 3 * 4, ptr_l + 4 * 8, 5 * 4);
  memset (ptr_l + 6 * 4, 0, 4);
  field_sub (result, result, (bignum_t *) ptr_l);
  //d4
  memcpy (ptr_l, ptr_l + 14 * 4, 2 * 4);
  memcpy (ptr_l + 4 * 3, ptr_l + 9 * 4, 5 * 4);
  memset (ptr_l + 6 * 4, 0, 4);
  memset (ptr_l + 2 * 4, 0, 4);
  field_sub (result, result, (bignum_t *) ptr_l);
}

/*
 FAST REDUCTION for nistp192/prime192v1/secp192r1 OID 1.2.840.10045.3.1.1 curve

 result = bn (mod P192)

 P192 = 6277101735386680763835789423207666416083908700390324961279
 P192 = 0xFF FF FF FF FF FF FF FF  FF FF FF FF FF FF FF FE   FF FF FF FF FF FF FF FF

 (Ai  in 64 bit quantities)
 
 T =  ( A2 || A1 || A0 )
 S1 = ( 0  || A3 || A3 )
 S2 = ( A4 || A4 || 0  )
 S3 = ( A5 || A5 || A5 )
 R =   T + S1 + S2 + S3
*/

static void
fast192reduction (bignum_t * result, bigbignum_t * bn)
{
  DPRINT ("%s\n", __FUNCTION__);

// use field_add - code is small but fast enough

  field_add (result, (bignum_t *) bn, (bignum_t *) & bn->value[3 * 8]);

  // A5 copy over A2 (A2 is not needed anymore)
  // this create line A4,A3,A5
  memcpy (&bn->value[2 * 8], &bn->value[5 * 8], 8);
  field_add (result, result, (bignum_t *) & bn->value[2 * 8]);

  // clear low part
  memset (bn, 0, 3 * 8);
  //A5 copy over A1
  memcpy (&bn->value[1 * 8], &bn->value[5 * 8], 8);
  field_add (result, result, (bignum_t *) & bn->value[0 * 8]);

}

static void
field_mul (bignum_t * r, bignum_t * a, bignum_t * b)
{
  bigbignum_t bn;

  DPRINT ("%s\n", __FUNCTION__);

  mp_mul (&bn, a, b);

// known curves/primes:
  if (curve_type == C_PRIME192V1)
    return fast192reduction (r, &bn);
  if (curve_type == C_PRIME256V1)
    return fast256reduction (r, &bn);
#ifdef NIST_ONLY
  return fast384reduction (r, &bn);
#else

  if (curve_type == C_secp384r1)
    return fast384reduction (r, &bn);

  if (curve_type == C_secp256k1)
    return secp256k1reduction (r, &bn);

  mp_mod (&bn, field_prime);
  memcpy (r, &bn, mp_get_len ());

#endif
}

static void
field_sqr (bignum_t * r, bignum_t * a)
{
  bigbignum_t bn;

  DPRINT ("%s\n", __FUNCTION__);

  mp_square (&bn, a);

// known curves/primes:
  if (curve_type == C_PRIME192V1)
    return fast192reduction (r, &bn);
  if (curve_type == C_PRIME256V1)
    return fast256reduction (r, &bn);
#ifdef NIST_ONLY
  return fast384reduction (r, &bn);
#else
  if (curve_type == C_secp384r1)
    return fast384reduction (r, &bn);

  if (curve_type == C_secp256k1)
    return secp256k1reduction (r, &bn);

// any other curves ..
  mp_mod (&bn, field_prime);
  memcpy (r, &bn, mp_get_len ());
#endif
}

//#define field_sqr(r,a) field_mul(r,a,a)

static void
ec_affinify (ec_point_t * point, struct ec_param *ec)
{
  bignum_t n0, n1;

  DPRINT ("%s\n", __FUNCTION__);

  if (is_zero (&(point->Z)))
    {
      DPRINT ("Zero in Z, cannot affinify\n");
      return;
    }
  inv_mod (&n0, &point->Z, &ec->prime);	// n0=Z^-1
  field_sqr (&n1, &n0);		// n1=Z^-2
  field_mul (&point->X, &point->X, &n1);	// X*=n1
  field_mul (&n0, &n0, &n1);	// n0=Z^-3      
  field_mul (&point->Y, &point->Y, &n0);
  memset (&point->Z, 0, mp_get_len ());
  point->Z.value[0] = 1;
}

//NIST reference implementation .. need to be revised, working, but big
#ifdef NIST_DOUBLE
static __attribute__ ((unused))
     void ec_double_nist (ec_point_t * a)
{
  bignum_t t1, t2, t3, t4, t5;

  DPRINT ("%s\n", __FUNCTION__);

  mp_set (&t1, a->X.value);	//1
  mp_set (&t2, a->Y.value);	//2
  mp_set (&t3, a->Z.value);	//3

  if (is_zero (&(a->Z)))	//4
    {
      mp_set_to_1 (&(a->X));	//5
      mp_set_to_1 (&(a->Y));
      DPRINT ("Not projective point ?\n");
      return;
    }				//6
  field_sqr (&t4, &t3);		//7  t4 = t3^2
  field_sub (&t5, &t1, &t4);	//8  t5 = t1 - t4
  field_add (&t4, &t1, &t4);	//9  t4 = t1 + t4
  field_mul (&t5, &t4, &t5);	//10 t5 = t4 + t5
  //                            //11 t4 = 3*t5          
  field_add (&t4, &t5, &t5);	//   2*t5
  field_add (&t4, &t5, &t4);	//   3*t5
  //
  field_mul (&t3, &t3, &t2);	//12 t3 = t3 * t2
  field_add (&t3, &t3, &t3);	//13 t3 = t3*2
  field_sqr (&t2, &t2);		//14 t2 = t2^2
  field_mul (&t5, &t1, &t2);	//15 t5 = t1 * &t2
  //                            //16 t5 = 4*t5;
  field_add (&t5, &t5, &t5);	//   2x
  field_add (&t5, &t5, &t5);	//   4x
  field_sqr (&t1, &t4);		//17 t1 = t4^2
  //                            //18 t1 = t1 - 2*t5
  field_sub (&t1, &t1, &t5);
  field_sub (&t1, &t1, &t5);
  //
  field_sqr (&t2, &t2);		//19 t2=t2^2
  //                            //20 t2=8*t2
  field_add (&t2, &t2, &t2);	//   2x 
  field_add (&t2, &t2, &t2);	//   4x 
  field_add (&t2, &t2, &t2);	//   8x 
  //
  field_sub (&t5, &t5, &t1);	//21 t5 = t5 - t1
  field_mul (&t5, &t4, &t5);	//22
  field_sub (&t2, &t5, &t2);	//23

  mp_set (a->X.value, &t1);	//24
  mp_set (a->Y.value, &t2);	//25
  mp_set (a->Z.value, &t3);	//26
}
#endif

// if a == -3 curve can be handled "faster", this is true for NIST curves
// If secp256k1 is need to be computed, disable this handling and do full calculation

// a*=2
static void
ec_double (ec_point_t * a)
{
  bignum_t S, M, YY, ZZ, T;

  DPRINT ("%s\n", __FUNCTION__);

  if (ecisinf (a))
    return;
  field_sqr (&YY, &a->Y);

#ifdef NIST_ONLY
  // only if coefficient A = -3
  field_sqr (&ZZ, &a->Z);
  field_add (&T, &a->X, &ZZ);
  field_sub (&M, &a->X, &ZZ);
  field_mul (&M, &M, &T);
  field_add (&T, &M, &M);
  field_add (&M, &T, &M);	//M=3*(X-Z^2)*(X+X^2)
#else
  if (curve_type & 0x40)	// optimize for A=-3
    {
      field_sqr (&ZZ, &a->Z);
      field_add (&T, &a->X, &ZZ);
      field_sub (&M, &a->X, &ZZ);
      field_mul (&M, &M, &T);
      field_add (&T, &M, &M);
      field_add (&M, &T, &M);	//M=3*(X-Z^2)*(X+X^2)
    }
  else if (curve_type & 0x80)	// optimize for A=0
    {
      field_sqr (&M, &a->X);
      field_add (&S, &M, &M);
      field_add (&M, &S, &M);
    }
  else
    {
      field_sqr (&ZZ, &a->Z);
      field_sqr (&T, &ZZ);
      field_mul (&T, param_a, &T);	//T = a*Z^4
      field_sqr (&M, &a->X);
      field_add (&S, &M, &M);
      field_add (&M, &S, &M);
      field_add (&M, &M, &T);
    }
#endif

  field_mul (&S, &a->X, &YY);
  field_add (&S, &S, &S);
  field_add (&S, &S, &S);	// S = 4*X*Y^2

  field_sqr (&T, &M);
  field_add (&a->X, &S, &S);
  field_sub (&a->X, &T, &a->X);	// X = M^2-2*S

  field_mul (&a->Z, &a->Y, &a->Z);
  field_add (&a->Z, &a->Z, &a->Z);	// Z = 2*Y*Z

  field_sqr (&T, &YY);

  field_add (&T, &T, &T);
  field_add (&T, &T, &T);
  field_add (&T, &T, &T);

  field_sub (&a->Y, &S, &a->X);
  field_mul (&a->Y, &M, &a->Y);
  field_sub (&a->Y, &a->Y, &T);	//Y'=M*(S-X') - 8*Y^4
}

/**************************************************/
static uint8_t
ec_add_ (ec_point_t * a, ec_point_t * b)
{
  bignum_t u1, u2, s1, s2, t1, t2;

  DPRINT ("%s\n", __FUNCTION__);

  if (ecisinf (b))
    return 0;
  if (ecisinf (a))
    {
      *a = *b;
      return 0;
    }

  field_sqr (&t1, &b->Z);
  field_mul (&u1, &a->X, &t1);	//u1 = X1*Z2^2

  field_sqr (&t2, &a->Z);
  field_mul (&u2, &b->X, &t2);	//u2 = X2*Z1^2

  field_mul (&t1, &t1, &b->Z);
  field_mul (&s1, &a->Y, &t1);	//s1 = Y1*Z2^3

  field_mul (&t2, &t2, &a->Z);
  field_mul (&s2, &b->Y, &t2);	//s2 = Y2*Z1^3

  field_sub (&u2, &u2, &u1);
  field_sub (&s2, &s2, &s1);

  if (is_zero (&u2))
    {

      if (is_zero (&s2))
	{
	  return 1;		//signalize double(a) is needed
	}
      else
	{
	  memset (a, 0, sizeof (*a));
	  mp_set_to_1 (&(a->X));
	  mp_set_to_1 (&(a->Y));
	  return 0;
	}
    }

#define	H u2
#define R s2

  field_sqr (&t1, &H);		//t1 = H^2
  field_mul (&t2, &H, &t1);	//t2 = H^3
  field_mul (&t1, &u1, &t1);	//t3 = u1*h^2

  field_sqr (&a->X, &R);
  field_sub (&a->X, &a->X, &t2);

  field_sub (&a->X, &a->X, &t1);
  field_sub (&a->X, &a->X, &t1);	//X3=R^2 - H^3 - 2*U1*H^2

  field_sub (&a->Y, &t1, &a->X);
  field_mul (&a->Y, &a->Y, &R);

  field_mul (&t1, &s1, &t2);
  field_sub (&a->Y, &a->Y, &t1);

  field_mul (&a->Z, &a->Z, &b->Z);
  field_mul (&a->Z, &a->Z, &H);
  return 0;
}

#undef H
#undef R


//ec_full_add (R, S, T ): Set R to S+T . All points projective
static void
ec_full_add (ec_point_t * result, ec_point_t * s, ec_point_t * t)
{

  DPRINT ("%s\n", __FUNCTION__);

  if (is_zero (&(s->Z)))
    {
      memcpy (result, t, sizeof (ec_point_t));
      return;
    }
  memcpy (result, s, sizeof (ec_point_t));
  if (is_zero (&(t->Z)))
    {
      return;
    }
  //TODO ec_add_ is not complete, missing lines 2..8 from nist algo
  if (ec_add_ (result, t))
    {
      memcpy (result, s, sizeof (ec_point_t));
      ec_double (result);
    }
}

//this is needed by ec_mul by nist ..  do not compile it if not needed
static __attribute__ ((unused))
     void ec_full_sub (ec_point_t * result, ec_point_t * s, ec_point_t * t)
{
  ec_point_t u;

  DPRINT ("%s\n", __FUNCTION__);

  memcpy (&u, t, sizeof (ec_point_t));

  mp_sub (&(u.Y), field_prime, &(u.Y));
  ec_full_add (result, s, &u);
}


static void
ec_mul (ec_point_t * result, bignum_t * k, ec_point_t * point)
{
  int8_t i;
  uint8_t b, j;

  DPRINT ("%s\n", __FUNCTION__);

  memset (result, 0, sizeof (*result));	//result=inf

  for (i = mp_get_len () - 1; i >= 0; i--)
    {
      b = k->value[i];
      for (j = 0; j < 8; j++)
	{
	  ec_double (result);
	  if (b & 0x80)
	    ec_full_add (result, result, point);
	  b <<= 1;
	}
    }
}

#if 0
//not working!
static void
ec_mul_nist (ec_point_t * result, bignum_t * num, ec_point_t * s)
{
  int i, flag;
  bignum_t d3;
  bignum_t d;
  ec_point_t u;

  mp_set (&d, num);
  mp_set (&d3, num);
  mp_add (&d3, &d3, &d3);	//2x d
  mp_add (&d3, &d3, num);

  if (is_zero (num))
    {
      memset (result, 0, sizeof (*result));
      mp_set_to_1 (&(result->X));
      mp_set_to_1 (&(result->Y));
      return;
    }
  if (mp_is_1 (num))
    {
      mp_set (result, s);
      return;
    }
  if (is_zero (&(s->Z)))
    {
      memset (result, 0, sizeof (*result));
      mp_set_to_1 (&(result->X));
      mp_set_to_1 (&(result->Y));
      return;
    }
  memcpy (result, s, sizeof (ec_point_t));
  //TODO optimize this
  for (i = mp_get_len () * 8 - 1; i >= 1; i--)	// to 1 ???  why?
    {
      ec_double (result);
      flag = 0;
      if ((d3.value[mp_get_len () - 1] & 0x80) == 0x80
	  && ((d.value[mp_get_len () - 1] & 0x80) == 0))
	{
	  ec_full_add (&u, result, s);
	  flag = 1;
	}
      if ((d3.value[mp_get_len () - 1] & 0x80) == 0
	  && ((d.value[mp_get_len () - 1] & 0x80) == 0x80))
	{
	  ec_full_sub (&u, result, s);
	  flag = 1;
	}
      if (flag)
	memcpy (result, &u, sizeof (ec_point_t));
      mp_shiftl (&d);
      mp_shiftl (&d3);
    }
}
#endif

static void
ec_set_param (struct ec_param *ec)
{
  mp_set_len (ec->mp_size);
  field_prime = &ec->prime;
#ifndef NIST_ONLY
  param_a = &ec->a;
#endif
  curve_type = ec->curve_type;
}

uint8_t
ec_check_key (bignum_t * k, ec_point_t * pub_key, struct ec_param *ec)
{
  ec_point_t temp_g;

  DPRINT ("%s\n", __FUNCTION__);
  ec_set_param (ec);

  if (NULL == k)
    return 1;
  if (NULL == pub_key)
    return 1;

  if (is_zero (k))
    return 1;

  // is key below curve order ?
  if (mp_cmp (k, &ec->order) != -1)
    return 1;

  memcpy (&temp_g.X, &ec->Gx, sizeof (bignum_t));
  memcpy (&temp_g.Y, &ec->Gy, sizeof (bignum_t));

  DPRINT ("calculating public key\n");

  ec_projectify (&temp_g);
  ec_mul (pub_key, k, &temp_g);

  ec_affinify (pub_key, ec);

  if (is_zero (&(pub_key->X)))	// Rx  mod order != 0
    return 1;
  if (mp_cmp (&(pub_key->X), &ec->order) == 1)
    return 1;

  DPRINT ("key ok\n");
  return 0;
}

uint8_t
ec_key_gener (bignum_t * k, ec_point_t * pub_key, struct ec_param * ec)
{
  uint8_t i;
  ec_point_t temp_g;

  DPRINT ("%s\n", __FUNCTION__);
  ec_set_param (ec);

  if (NULL == k)
    return 1;
  if (NULL == pub_key)
    return 1;

  // clear key, 
  memset (k, 0, mp_get_len ());

  for (i = 0; i < 5; i++)
    {
      // load key bytes from rnd
      uint8_t off;
      for (off = 0; off < ec->mp_size; off++)
	while (1 == rnd_get (off + (uint8_t *) k, 1));

      if (is_zero (k))
	continue;

      // is key below curve order ?
      if (mp_cmp (k, &ec->order) != -1)
	continue;

// TODO better "k" (for example check openssl crypto/ecdsa/ecs_ossl.c)

      memcpy (&temp_g.X, &ec->Gx, sizeof (bignum_t));
      memcpy (&temp_g.Y, &ec->Gy, sizeof (bignum_t));

      DPRINT ("calculating public key\n");

      ec_projectify (&temp_g);
      ec_mul (pub_key, k, &temp_g);

      ec_affinify (pub_key, ec);

      if (is_zero (&(pub_key->X)))	// Rx  mod order != 0
	continue;
      if (mp_cmp (&(pub_key->X), &ec->order) == 1)
	continue;

      DPRINT ("key ok\n");
      return 0;
    }
  DPRINT ("key fail!\n");
  return 1;
}

uint8_t
ecdsa_sign (ecdsa_sig_t * ecsig, struct ec_param * ec)
{
  bignum_t s, r;

  ec_point_t R;
  bignum_t k;

  int i;

  DPRINT ("%s\n", __FUNCTION__);
  ec_set_param (ec);

  for (i = 0; i < 5; i++)
    {
      // generate key
      if (ec_key_gener (&k, &R, ec))
	continue;
// use r= x position of R, e = HASH, dA = private key
// k,R  temp keys, n = field order 
// s = (e + dA * r)/k  mod n

      mp_set (&r, &R.X);

      mul_mod (&s, &ec->private_key, &r, &ec->order);

      add_mod (&s, ecsig->message, &s, &ec->order);
      inv_mod (&k, &k, &ec->order);	// division by k 
      mul_mod (&s, &k, &s, &ec->order);
      if (!is_zero (&s))
	{
	  memcpy (&ecsig->R, &r, sizeof (bignum_t));
	  memcpy (&ecsig->S, &s, sizeof (bignum_t));
	  return 0;
	}
      DPRINT ("repeating, s=0\n");
    }
  return 1;
}


/**********************************************************************


***********************************************************************/
//////////////////////////////////////////////////
#ifndef HAVE_MP_ADD
static uint8_t
mp_add (bignum_t * r, bignum_t * a, bignum_t * b)
{
  uint8_t *A, *B, *R;
  uint8_t carry;
  uint8_t i;
  int16_t pA, pB, Res;

  DPRINT ("%s\n", __FUNCTION__);

  A = (void *) a;
  B = (void *) b;
  R = (void *) r;

  carry = 0;
  for (i = 0; i < mp_get_len (); i++)
    {
      pA = A[i];
      pB = B[i];
      Res = pA + pB + carry;

      R[i] = Res & 255;
      carry = (Res >> 8) & 1;
    }
  return carry;
}
#endif
//////////////////////////////////////////////////
#ifndef HAVE_MP_SUB
static uint8_t
mp_sub (bignum_t * r, bignum_t * a, bignum_t * b)
{
  uint8_t *A, *B, *R;
  uint8_t carry;
  uint8_t i;
  int16_t pA, pB, Res;

  DPRINT ("%s\n", __FUNCTION__);

  A = (void *) a;
  B = (void *) b;
  R = (void *) r;

  carry = 0;
  for (i = 0; i < mp_get_len (); i++)
    {
      pA = A[i];
      pB = B[i];
      Res = pA - pB - carry;

      R[i] = Res & 255;
      carry = (Res >> 8) & 1;
    }
  return carry;
}
#endif
//////////////////////////////////////////////////
#if defined (HAVE_RSA_MUL_384) && defined (HAVE_RSA_MUL_256) && defined (HAVE_RSA_MUL_192)
static void
mp_mul (bigbignum_t * r, bignum_t * b, bignum_t * a)
{
  if (mp_get_len () > 32)
    {
      rsa_mul_384 (&r->value[0], &a->value[0], &b->value[0]);
    }
  else if (mp_get_len () > 24)
    {
      rsa_mul_256 (&r->value[0], &a->value[0], &b->value[0]);
    }
  else
    {
      rsa_mul_192 (&r->value[0], &a->value[0], &b->value[0]);
    }
}
#else
static void mp_mul (bigbignum_t * result, bignum_t * a, bignum_t * b);

// normal multiplication r = a * b
// (replace this by karatsuba or other eficient algo .. )
#ifdef USE_MP_MUL_8
// multiplication with 8x8 hardware engine
void
mp_mul (bigbignum_t * r, bignum_t * b, bignum_t * a)
{
  uint8_t i, j, c;
  uint8_t a_;
  uint16_t res;

  memset (r, 0, mp_get_len () * 2);	// r = 0

  for (i = 0; i < mp_get_len (); i++)
    {
      c = 0;
      a_ = a->value[i];

      for (j = 0; j < mp_get_len (); j++)
	{
	  res = a_ * b->value[j];
	  res += r->value[i + j];
	  res += c;

	  c = res >> 8;
	  r->value[i + j] = res & 255;
	}
      r->value[i + mp_get_len ()] = c;
    }
}
#else
// classical binary multiplication (can run on CPU
// without multiplier for example attiny85 )
// This is "generic" code, but very slow.

void
mp_mul (bigbignum_t * r, bignum_t * b, bignum_t * a)
{
  uint8_t *A, *B, *R;
  int8_t i, s, rot;
  uint8_t carry;
  int16_t pA, pB, Res;

  uint8_t m;

  DPRINT ("%s\n", __FUNCTION__);

  A = (void *) a;
  B = (void *) b;
  R = (void *) r;

  memset (R + mp_get_len (), 0, mp_get_len ());

  for (s = 0; s < mp_get_len (); s++)
    {
      m = A[s];
      for (rot = 0; rot < 8; rot++, m >>= 1)
	{
	  carry = 0;
	  if (m & 1)		//add b to result
	    {
	      for (i = 0; i < mp_get_len (); i++)
		{
		  pA = R[i + mp_get_len ()];
		  pB = B[i];
		  Res = pA + pB + carry;
		  R[i + mp_get_len ()] = Res & 255;
		  carry = (Res >> 8) & 1;
		}
	    }
	  //rotate R
	  if (carry)
	    carry = 0x80;
	  for (i = 2 * mp_get_len () - 1; i >= 0; i--)
	    {
	      Res = R[i] >> 1;
	      Res |= carry;
	      carry = (R[i] & 1) ? 0x80 : 0;
	      R[i] = Res;
	    }
	}
    }
}
#endif
#endif
//////////////////////////////////////////////////
#if defined (HAVE_RSA_SQUARE_384) && defined (HAVE_RSA_SQUARE_256) && defined (HAVE_RSA_SQUARE_192)
static void
mp_square (bigbignum_t * r, bignum_t * a)
{
  if (mp_get_len () > 32)
    {
      rsa_square_384 (&r->value[0], &a->value[0]);
    }
  else if (mp_get_len () > 24)
    {
      rsa_square_256 (&r->value[0], &a->value[0]);
    }
  else
    {
      rsa_square_192 (&r->value[0], &a->value[0]);
    }
}
#else
#if defined (HAVE_RSA_MUL_384) && defined (HAVE_RSA_MUL_256) && defined (HAVE_RSA_MUL_192)
static void
mp_square (bigbignum_t * r, bignum_t * a)
{
  if (mp_get_len () > 32)
    {
      rsa_mul_384 (&r->value[0], &a->value[0], &a->value[0]);
    }
  else if (mp_get_len () > 24)
    {
      rsa_mul_256 (&r->value[0], &a->value[0], &a->value[0]);
    }
  else
    {
      rsa_mul_192 (&r->value[0], &a->value[0], &a->value[0]);
    }
}
#else
static void
mp_square (bigbignum_t * r, bignum_t * a)
{
  mp_mul (r, a, a);
}
#endif
#endif

//////////////////////////////////////////////////
#ifndef HAVE_MP_SHIFTL
static uint8_t
mp_shiftl (bignum_t * r)
{
  uint8_t carry;
  int8_t i;
  int16_t Res;

  DPRINT ("%s\n", __FUNCTION__);

  carry = 0;
  for (i = 0; i < mp_get_len (); i++)
    {
      Res = r->value[i] << 1;
      Res |= carry;
      carry = (Res >> 8) & 1;
      r->value[i] = Res & 255;
    }
  return carry;
}
#endif
//////////////////////////////////////////////////
#ifndef HAVE_MP_SHIFTR
static uint8_t
mp_shiftr_c (bignum_t * r, uint8_t carry)
{
  uint8_t *R;
  uint8_t c2;
  int8_t i;
  int16_t Res;

  DPRINT ("%s\n", __FUNCTION__);

  R = (void *) r;

  carry = carry ? 0x80 : 0;
  for (i = mp_get_len () - 1; i >= 0; i--)
    {
      Res = R[i];
      c2 = Res & 1;
      Res = Res >> 1;
      Res |= carry;
      carry = c2 << 7;
      R[i] = Res;
    }
  return carry;
}
#endif
#ifndef HAVE_MP_SHIFTR
static uint8_t
mp_shiftr (bignum_t * r)
{
  return mp_shiftr_c (r, 0);
}
#endif

//////////////////////////////////////////////////
#ifndef HAVE_MP_CMP
// return  1  if c > d
// return -1  if c < d
// return 0 if d == d
static int8_t
mp_cmp (bignum_t * c, bignum_t * d)
{
  uint8_t *C = (void *) c;
  uint8_t *D = (void *) d;

  int8_t i;

  DPRINT ("%s\n", __FUNCTION__);

  for (i = mp_get_len () - 1; i >= 0; i--)
    {
      if (C[i] > D[i])
	return 1;
      if (C[i] < D[i])
	return -1;
    }
  return 0;
}
#endif


static void
mp_set_to_1 (bignum_t * r)
{
  uint8_t *Res = (void *) r;

  DPRINT ("%s\n", __FUNCTION__);

  memset (r, 0, mp_get_len ());
  *Res = 1;
}

//return true if r is even

static uint8_t
mp_test_even (bignum_t * r)
{
  return 1 ^ (r->value[0] & 1);
}

//return true if r is 1
static uint8_t
mp_is_1 (bignum_t * r)
{
  uint8_t i = 0;
  uint8_t len = mp_get_len ();
  uint8_t *Res = (void *) r;

  DPRINT ("%s\n", __FUNCTION__);

  if (Res[i++] != 1)
    return 0;

  while (i < len)
    if (Res[i++] != 0)
      return 0;

  return 1;
}

//set r = c^(-1) (mod p)
//based on nist.. 
// TODO, for  example for 40^(-1) mod 50 this run in loop - this is ok,
// because this is not correct input, but this must be catched by exception
// In real code only "Prime" and "Order" numbers are used as "p"
static void
inv_mod (bignum_t * r, bignum_t * c, bignum_t * p)
{
  bignum_t U, V, X1, X2;
  bignum_t *u = &U, *v = &V, *x1 = &X1, *x2 = &X2;
  uint8_t carry;

  DPRINT ("%s\n", __FUNCTION__);

  mp_set (u, c);
  mp_set (v, p);
  mp_clear (x2);
  mp_set_to_1 (x1);

  for (;;)
    {
      if (mp_is_1 (u))
	{
	  mp_set (r, x1);
	  return;
	}
      if (mp_is_1 (v))
	{
	  mp_set (r, x2);
	  return;
	}


      while (mp_test_even (u))
	{
	  mp_shiftr (u);	// u = u / 2
	  if (mp_test_even (x1))
	    {
	      mp_shiftr (x1);	// x1 = x1 / 2
	    }
	  else
	    {
	      // x1 = (x1 + p)/2 {do not reduce sum modulo p}
	      carry = mp_add (x1, x1, p);
	      mp_shiftr_c (x1, carry);
	    }
	}

      while (mp_test_even (v))
	{
	  mp_shiftr (v);	// v = v/2
	  if (mp_test_even (x2))
	    {
	      mp_shiftr (x2);	// x1 = x2 / 2
	    }
	  else
	    {
	      // x2 = (x2 + p)/2 {do not reduce sum modulo p}
	      carry = mp_add (x2, x2, p);
	      mp_shiftr_c (x2, carry);
	    }
	}

      if (mp_cmp (u, v) > 0)
	{
	  sub_mod (u, u, v, p);
	  sub_mod (x1, x1, x2, p);
	}
      else
	{
	  sub_mod (v, v, u, p);
	  sub_mod (x2, x2, x1, p);
	}
    }
}

//////////////////////////////////////////////////
//helper for mp_mod
#ifndef HAVE_MP_SHIFTR_2N
static uint8_t
mp_shiftr_2N (bigbignum_t * r)
{
  uint8_t carry, c2;
  int8_t i;
  int16_t Res;

  DPRINT ("%s\n", __FUNCTION__);

  carry = 0;
  for (i = mp_get_len () * 2 - 1; i >= 0; i--)
    {
      Res = r->value[i];
      c2 = Res & 1;
      Res = Res >> 1;
      Res |= carry;
      carry = c2 << 7;
      r->value[i] = Res;
    }
  return carry;
}
#endif
//////////////////////////////////////////////////
// helper for mp_mod
#ifndef HAVE_MP_SUB_2N
static uint8_t
mp_sub_2N (bigbignum_t * r, bigbignum_t * a, bigbignum_t * b)
{
  uint8_t *A, *B, *R;
  uint8_t carry;
  uint8_t i;
  int16_t pA, pB, Res;

  DPRINT ("%s\n", __FUNCTION__);

  A = (void *) a;
  B = (void *) b;
  R = (void *) r;

  carry = 0;
  for (i = 0; i < 2 * mp_get_len (); i++)
    {
      pA = A[i];
      pB = B[i];
      Res = pA - pB - carry;

      R[i] = Res & 255;
      carry = (Res >> 8) & 1;
    }
  return carry;
}
#endif
//////////////////////////////////////////////////
#if defined (HAVE_RSA_MOD)
void rsa_mod (bigbignum_t * result, bignum_t * mod);

static void
mp_mod (bigbignum_t * result, bignum_t * mod)
{
  rsa_mod (result, mod);
}
#else
// "result" = "result" mod "mod"
static void
mp_mod (bigbignum_t * result, bignum_t * mod)
{
  bigbignum_t bb;
  bigbignum_t tmp;
  uint8_t *bb_ptr = (void *) &bb;
  int16_t i;

  DPRINT ("%s\n", __FUNCTION__);

  memset (bb_ptr, 0, mp_get_len ());
  memcpy (bb_ptr + mp_get_len (), mod, mp_get_len ());

  i = mp_get_len () * 8;

  while (!(bb.value[2 * mp_get_len () - 1] & 0x80))
    {
      mp_shiftl ((bignum_t *) (bb_ptr + mp_get_len ()));
      i++;
    }
  for (; i >= 0; i--)
    {
      int lz = mp_sub_2N (&tmp, result, &bb);

      if (!lz)
	memcpy (result, &tmp, 2 * mp_get_len ());
      mp_shiftr_2N (&bb);

    }
}
#endif