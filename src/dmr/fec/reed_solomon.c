/**
 * Code taken and adapted from www.eccpage.com/rs.c
 * Credit goes to Mr Simon Rockliff.
 *
 * Tried before with the implementation from ITPP library but couldn't make it produce the same outputs
 * expected from the P25 transmissions that I have tested. This implementation does work.
 */

/* This program is an encoder/decoder for Reed-Solomon codes. Encoding is in
 systematic form, decoding via the Berlekamp iterative algorithm.
 In the present form , the constants mm, nn, tt, and kk=nn-2tt must be
 specified  (the double letters are used simply to avoid clashes with
 other n,k,t used in other programs into which this was incorporated!)
 Also, the irreducible polynomial used to generate GF(2**mm) must also be
 entered -- these can be found in Lin and Costello, and also Clark and Cain.
 The representation of the elements of GF(2**m) is either in index form,
 where the number is the power of the primitive element alpha, which is
 convenient for multiplication (add the powers modulo 2**m-1) or in
 polynomial form, where the bits represent the coefficients of the
 polynomial representation of the number, which is the most convenient form
 for addition.  The two forms are swapped between via lookup tables.
 This leads to fairly messy looking expressions, but unfortunately, there
 is no easy alternative when working with Galois arithmetic.
 The code is not written in the most elegant way, but to the best
 of my knowledge, (no absolute guarantees!), it works.
 However, when including it into a simulation program, you may want to do
 some conversion of global variables (used here because I am lazy!) to
 local variables where appropriate, and passing parameters (eg array
 addresses) to the functions  may be a sensible move to reduce the number
 of global variables and thus decrease the chance of a bug being introduced.
 This program does not handle erasures at present, but should not be hard
 to adapt to do this, as it is just an adjustment to the Berlekamp-Massey
 algorithm. It also does not attempt to decode past the BCH bound -- see
 Blahut "Theory and practice of error control codes" for how to do this.
 Simon Rockliff, University of Adelaide   21/9/89
 26/6/91 Slight modifications to remove a compiler dependent bug which hadn't
 previously surfaced. A few extra comments added for clarity.
 Appears to all work fine, ready for posting to net!
 Notice
 --------
 This program may be freely modified and/or given to whoever wants it.
 A condition of such distribution is that the author's contribution be
 acknowledged by his name being left in the comments heading the program,
 however no responsibility is accepted for any financial or other loss which
 may result from some unforseen errors or malfunctioning of the program
 during use.
 Simon Rockliff, 26th June 1991
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dmr/fec/reed_solomon.h"
#include "dmr/error.h"
#include "dmr/malloc.h"
#include "dmr/bits.h"

static dmr_rs_t *rs8 = NULL;

/*
 * Division by 63: magic number is 0x82082083, shift amount is 37
 * Division by 255: magic number is 0x80808081, shift amount is 39
 */

/*
 * Generate GF(2**mm) from the irreducible polynomial p(X) in pp[0]..pp[mm] lookup tables:
 * index->polynomial form alpha_to[] contains j=alpha**i;
 * polynomial form -> index form  index_of[j=alpha**i] = i
 * alpha=2 is the primitive element of GF(2**mm)
 * Also, obtain the generator polynomial of the tt-error correcting,
 * length nn=(2**mm -1) Reed Solomon code  from the product of (X+alpha**i), i=1..2*tt
 */
dmr_rs_t *dmr_rs_new(unsigned int generator_polinomial, int mm, unsigned char tt)
{
    register int i, j, mask = 1;
    dmr_rs_t *rs = dmr_malloc(dmr_rs_t);
    if (rs == NULL)
        return NULL;

    rs->alpha_to = dmr_malloc_child_size(rs, 512);
    if (rs->alpha_to == NULL) {
        dmr_free(rs);
        return NULL;
    }
    rs->index_of = dmr_malloc_child_size(rs, 512);
    if (rs->index_of == NULL) {
        dmr_free(rs);
        return NULL;
    }

    rs->mm = mm;
    rs->nn = (1 << mm) - 1;
    rs->tt = tt;
    rs->n = 2*rs->tt;
    rs->alpha_to[rs->mm] = 0;
    for (i = 0; i < rs->mm; i++) {
        rs->alpha_to[i] = mask;
        rs->index_of[rs->alpha_to[i]] = i;
        if (((generator_polinomial >> i) & 1) != 0)
            rs->alpha_to[rs->mm] ^= mask;
        mask <<= 1;
    }
    rs->index_of[rs->alpha_to[rs->mm]] = rs->mm;
    mask >>= 1;
    for (i = rs->mm + 1; i < rs->nn; i++) {
        if (rs->alpha_to[i - 1] >= mask)
            rs->alpha_to[i] = rs->alpha_to[rs->mm] ^ ((rs->alpha_to[i - 1] ^ mask) << 1);
        else
            rs->alpha_to[i] = rs->alpha_to[i - 1] << 1;
        rs->index_of[rs->alpha_to[i]] = i;
    }
    rs->index_of[0] = -1;

    rs->gg[0] = 2; /* primitive element alpha = 2  for GF(2**mm)  */
    rs->gg[1] = 1; /* g(x) = (X+alpha) initially */
    for (i = 2; i <= (int)rs->n; i++) {
        rs->gg[i] = 1;
        for (j = i - 1; j > 0; j--)
            if (rs->gg[j] != 0)
                rs->gg[j] = rs->gg[j - 1] ^ rs->alpha_to[(unsigned int)(rs->index_of[rs->gg[j]] + i) % rs->nn];
            else
                rs->gg[j] = rs->gg[j - 1];
        rs->gg[0] = rs->alpha_to[(unsigned int)(rs->index_of[rs->gg[0]] + i) % rs->nn]; /* gg[0] can never be zero */
    }

    /* convert gg[] to index form for quicker encoding */
    for (i = 0; i <= (int)rs->n; i++)
        rs->gg[i] = rs->index_of[rs->gg[i]];

    return rs;
}

/* take the string of symbols in data[i], i=0..(k-1) and encode systematically
 to produce 2*tt parity symbols in bb[0]..bb[2*tt-1]
 data[] is input and bb[] is output in polynomial form.
 Encoding is done by using a feedback shift register with appropriate
 connections specified by the elements of gg[], which was generated above.
 Codeword is   c(X) = data(X)*X**(nn-kk)+ b(X)          */
void dmr_rs_encode(dmr_rs_t *rs, const unsigned char *data, unsigned char *bb)
{
    register int i, j, k = (rs->nn - rs->n);
    int feedback;

    for (i = 0; i < (int)rs->n; i++)
        bb[i] = 0;
    for (i = k - 1; i >= 0; i--) {
        feedback = rs->index_of[data[i] ^ bb[rs->n - 1]];
        if (feedback != -1) {
            for (j = rs->n - 1; j > 0; j--)
                bb[j] = bb[j - 1] ^ rs->alpha_to[(unsigned int)(rs->gg[j] + feedback) % rs->nn];
            bb[0] = rs->alpha_to[(unsigned int)(rs->gg[0] + feedback) % rs->nn];
        } else {
            for (j = rs->n - 1; j > 0; j--)
                bb[j] = bb[j - 1];
            bb[0] = 0;
        }
    }
}

/* assume we have received bits grouped into mm-bit symbols in recd[i],
 i=0..(nn-1),  and recd[i] is polynomial form.
 We first compute the 2*tt syndromes by substituting alpha**i into rec(X) and
 evaluating, storing the syndromes in s[i], i=1..2tt (leave s[0] zero) .
 Then we use the Berlekamp iteration to find the error location polynomial
 elp[i].   If the degree of the elp is >tt, we cannot correct all the errors
 and hence just put out the information symbols uncorrected. If the degree of
 elp is <=tt, we substitute alpha**i , i=1..n into the elp to get the roots,
 hence the inverse roots, the error location numbers. If the number of errors
 located does not equal the degree of the elp, we have more than tt errors
 and cannot correct them.  Otherwise, we then solve for the error value at
 the error location and correct the error.  The procedure is that found in
 Lin and Costello. For the cases where the number of errors is known to be too
 large to correct, the information symbols as received are output (the
 advantage of systematic encoding is that hopefully some of the information
 symbols will be okay and that if we are in luck, the errors are in the
 parity part of the transmitted codeword).  Of course, these insoluble cases
 can be returned as error flags to the calling routine if desired.   */
int dmr_rs_decode(dmr_rs_t *rs, unsigned char *input, unsigned char *recd)
{
    //register int i, j, u, q;
    register int i, j, u;
    register unsigned int q, syn_error = 0;
    int elp[rs->nn + 2][rs->nn], d[rs->nn + 2], l[rs->nn + 2], u_lu[rs->nn + 2], s[rs->nn + 1];
    int count = 0, root[DMR_RS_MAX_TT], loc[DMR_RS_MAX_TT], z[DMR_RS_MAX_TT + 1], err[rs->nn], reg[DMR_RS_MAX_TT + 1];
    int irrecoverable_error = 0;

    for (i = 0; i < rs->nn; i++)
       recd[i] = rs->index_of[input[i]]; /* put recd[i] into index form (ie as powers of alpha) */

    /* first form the syndromes */
    for (i = 1; i <= (int)rs->n; i++) {
        s[i] = 0;
        for (j = 0; j < rs->nn; j++)
            s[i] ^= rs->alpha_to[(unsigned int)(recd[j] + i * j) % rs->nn]; /* recd[j] in index form */
        /* convert syndrome from polynomial form to index form  */
        if (s[i] != 0)
            syn_error++; /* set flag if non-zero syndrome => error */
        s[i] = rs->index_of[s[i]];
    }

    if (syn_error) /* if errors, try and correct */
    {
        dmr_log_debug("Reed-Solomon(12,9,4): detected %u syndrome errors, attempting to repair", syn_error);
        /* compute the error location polynomial via the Berlekamp iterative algorithm,
         following the terminology of Lin and Costello :   d[u] is the 'mu'th
         discrepancy, where u='mu'+1 and 'mu' (the Greek letter!) is the step number
         ranging from -1 to 2*tt (see L&C),  l[u] is the
         degree of the elp at that step, and u_l[u] is the difference between the
         step number and the degree of the elp.
         */
        /* initialise table entries */
        d[0] = 0; /* index form */
        d[1] = s[1]; /* index form */
        elp[0][0] = 0; /* index form */
        elp[1][0] = 1; /* polynomial form */
        for (i = 1; i < (int)rs->n; i++) {
            elp[0][i] = -1; /* index form */
            elp[1][i] = 0; /* polynomial form */
        }
        l[0] = 0;
        l[1] = 0;
        u_lu[0] = -1;
        u_lu[1] = 0;
        u = 0;

        do {
            u++;
            if (d[u] == -1) {
                l[u + 1] = l[u];
                for (i = 0; i <= l[u]; i++) {
                    elp[u + 1][i] = elp[u][i];
                    elp[u][i] = rs->index_of[elp[u][i]];
                }
            } else
            /* search for words with greatest u_lu[q] for which d[q]!=0 */
            {
                q = u - 1;
                while ((d[q] == -1) && (q > 0))
                    q--;
                /* have found first non-zero d[q]  */
                if (q > 0) {
                    j = q;
                    do {
                        j--;
                        if ((d[j] != -1) && (u_lu[q] < u_lu[j]))
                            q = j;
                    } while (j > 0);
                };

                /* have now found q such that d[u]!=0 and u_lu[q] is maximum */
                /* store degree of new elp polynomial */
                if (l[u] > l[q] + (int)(u - q))
                    l[u + 1] = l[u];
                else
                    l[u + 1] = l[q] + u - q;

                /* form new elp(x) */
                for (i = 0; i < (int)rs->n; i++)
                    elp[u + 1][i] = 0;
                for (i = 0; i <= l[q]; i++)
                    if (elp[q][i] != -1)
                        elp[u + 1][i + u - q] = rs->alpha_to[(unsigned int)(d[u] + rs->nn - d[q] + elp[q][i]) % rs->nn];
                for (i = 0; i <= l[u]; i++) {
                    elp[u + 1][i] ^= elp[u][i];
                    elp[u][i] = rs->index_of[elp[u][i]]; /*convert old elp value to index*/
                }
            }
            u_lu[u + 1] = u - l[u + 1];

            /* form (u+1)th discrepancy */
            if (u < rs->nn) /* no discrepancy computed on last iteration */
            {
                if (s[u + 1] != -1)
                    d[u + 1] = rs->alpha_to[s[u + 1]];
                else
                    d[u + 1] = 0;
                for (i = 1; i <= l[u + 1]; i++)
                    if ((s[u + 1 - i] != -1) && (elp[u + 1][i] != 0))
                        d[u + 1] ^= rs->alpha_to[(unsigned int)(s[u + 1 - i] + rs->index_of[elp[u + 1][i]]) % rs->nn];
                d[u + 1] = rs->index_of[d[u + 1]]; /* put d[u+1] into index form */
            }
        } while ((u < (int)rs->n) && (l[u + 1] <= rs->tt));

        u++;
        if (l[u] <= rs->tt) /* can correct error */
        {
            /* put elp into index form */
            for (i = 0; i <= l[u]; i++)
                elp[u][i] = rs->index_of[elp[u][i]];

            /* find roots of the error location polynomial */
            for (i = 1; i <= l[u]; i++)
                reg[i] = elp[u][i];
            count = 0;
            for (i = 1; i <= rs->nn; i++) {
                q = 1;
                for (j = 1; j <= l[u]; j++)
                    if (reg[j] != -1) {
                        reg[j] = ((unsigned int)(reg[j] + j) % rs->nn);
                        q ^= rs->alpha_to[reg[j]];
                    };
                if (!q) /* store root and error location number indices */
                {
                    root[count] = i;
                    loc[count] = rs->nn - i;
                    count++;
                };
            };

            if (count == l[u]) /* no. roots = degree of elp hence <= tt errors */
            {
                /* form polynomial z(x) */
                for (i = 1; i <= l[u]; i++) /* Z[0] = 1 always - do not need */
                {
                    if ((s[i] != -1) && (elp[u][i] != -1))
                        z[i] = rs->alpha_to[s[i]] ^ rs->alpha_to[elp[u][i]];
                    else if ((s[i] != -1) && (elp[u][i] == -1))
                        z[i] = rs->alpha_to[s[i]];
                    else if ((s[i] == -1) && (elp[u][i] != -1))
                        z[i] = rs->alpha_to[elp[u][i]];
                    else
                        z[i] = 0;
                    for (j = 1; j < i; j++)
                        if ((s[j] != -1) && (elp[u][i - j] != -1))
                            z[i] ^= rs->alpha_to[(unsigned int)(elp[u][i - j] + s[j]) % rs->nn];
                    z[i] = rs->index_of[z[i]]; /* put into index form */
                };

                /* evaluate errors at locations given by error location numbers loc[i] */
                for (i = 0; i < rs->nn; i++) {
                    err[i] = 0;
                    /* convert recd[] to polynomial form */
                    recd[i] = rs->alpha_to[recd[i]];
                }
                for (i = 0; i < l[u]; i++) /* compute numerator of error term first */
                {
                    err[loc[i]] = 1; /* accounts for z[0] */
                    for (j = 1; j <= l[u]; j++)
                        if (z[j] != -1)
                            err[loc[i]] ^= rs->alpha_to[(unsigned int)(z[j] + j * root[i]) % rs->nn];
                    if (err[loc[i]] != 0) {
                        err[loc[i]] = rs->index_of[err[loc[i]]];
                        q = 0; /* form denominator of error term */
                        for (j = 0; j < l[u]; j++)
                            if (j != i)
                                q += rs->index_of[1 ^ rs->alpha_to[(unsigned int)(loc[j] + root[i]) % rs->nn]];
                        q = q % rs->nn;
                        err[loc[i]] = rs->alpha_to[(unsigned int)(err[loc[i]] - q + rs->nn) % rs->nn];
                        recd[loc[i]] ^= err[loc[i]]; /* recd[i] must be in polynomial form */
                    }
                }
            } else {
                /* no. roots != degree of elp => >tt errors and cannot solve */
                irrecoverable_error = 1;
            }
        } else {
            /* elp has degree >tt hence cannot solve */
            irrecoverable_error = 2;
        }
    } else {
        /* no non-zero syndromes => no errors: output received codeword */
        for (i = 0; i < rs->nn; i++)
            /* convert recd[] to polynomial form */
            recd[i] = rs->alpha_to[recd[i]];
    }

    if (irrecoverable_error) {
        dmr_log_error("Reed-Solomon(12,9,4): irrecoverable error");
        for (i = 0; i < rs->nn; i++) /* could return error flag if desired */
            /* convert recd[] to polynomial form */
            recd[i] = rs->alpha_to[recd[i]];
    } else if (syn_error) {
        dmr_log_debug("Reed-Solomon(12,9,4): recovered");
    }

    return irrecoverable_error;
}

int dmr_rs_init(void)
{
    if (rs8 == NULL) {
        dmr_log_trace("Reed-Solomon(12,9,4): init");
        rs8 = dmr_rs_new(0x11d, 8, 2);
        if (rs8 == NULL) {
            return dmr_error(DMR_ENOMEM);
        }
    }
    return 0;
}

int dmr_rs_12_9_4_encode(uint8_t bytes[12], uint8_t crc_mask)
{
    if (rs8 == NULL && dmr_rs_init() != 0) {
        dmr_log_error("Reed-Solomon: init failed");
        return dmr_error(DMR_LASTERROR);
    }

    dmr_log_trace("Reed-Solomon(12,9,4): encode using crc mask %#02x, calculate %u parities",
        crc_mask, rs8->tt);
    uint8_t bb[rs8->nn], input[rs8->nn];
    memset(bb, 0, rs8->nn);
    memset(input, 0, rs8->nn);
    memcpy(input, bytes, 12);
    dmr_rs_encode(rs8, input, bb);
    dmr_log_trace("Reed-Solomon(12,9,4): parities:");
    dmr_dump_hex(bb, rs8->n);
    bytes[ 9] = bb[0] ^ crc_mask;
    bytes[10] = bb[1] ^ crc_mask;
    bytes[11] = bb[2] ^ crc_mask;
    return 0;
}

int dmr_rs_12_9_4_decode_and_repair(uint8_t bytes[12], uint8_t crc_mask)
{
    if (rs8 == NULL && dmr_rs_init() != 0) {
        dmr_log_error("Reed-Solomon: init failed");
        return dmr_error(DMR_LASTERROR);
    }

    dmr_log_trace("Reed-Solomon(12,9,4): encode using crc mask %#02x, parities %#02x%02x%02x",
        crc_mask, bytes[9] ^ crc_mask, bytes[10] ^ crc_mask, bytes[11] ^ crc_mask);
    uint8_t input[rs8->nn], output[rs8->nn];
    memset(input, 0, rs8->nn);
    memset(output, 0, rs8->nn);
    memcpy(input, bytes, 12);
    input[ 9] ^= crc_mask;
    input[10] ^= crc_mask;
    input[11] ^= crc_mask;
    return dmr_rs_decode(rs8, input, output);
}

/* Simpler form where we encode the received data and compare the CRC */
int dmr_rs_12_9_4_decode_and_verify(uint8_t bytes[12], uint8_t crc_mask)
{
    if (rs8 == NULL && dmr_rs_init() != 0) {
        dmr_log_error("Reed-Solomon: init failed");
        return dmr_error(DMR_LASTERROR);
    }

    uint8_t check[12];
    memset(check, 0, 12);
    memcpy(check, bytes, 9);
    dmr_rs_12_9_4_encode(check, crc_mask);
    return memcmp(bytes + 9, check + 9, 3);
}