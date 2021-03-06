/*
    Copyright (C) 2017 Daniel Schultz

    This file is part of FLINT.

    FLINT is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include <gmp.h>
#include <stdlib.h>
#include <pthread.h>

#include "profiler.h"
#include "flint.h"
#include "fmpz.h"
#include "fmpz_mpoly.h"


/*
    Set poly1 to poly2*poly3 using Johnson's heap method. The function
    realocates its output and returns the length of the product. This
    version of the function assumes the exponent vectors all fit in a
    single word. Assumes input polys are nonzero.
    Only terms t with start >= t > end are written;
    "start" and "end" are not monomials but arrays of indicies into exp3
*/
slong _fmpz_mpoly_mul_heap_part1(fmpz ** poly1, ulong ** exp1, slong * alloc,
              const fmpz * poly2, const ulong * exp2, slong len2,
              const fmpz * poly3, const ulong * exp3, slong len3,
                                      slong * start, slong * end, ulong maskhi)
{
    slong i, k;
    slong next_free, Q_len = 0, heap_len = 1; /* heap zero index unused */
    mpoly_heap1_s * heap;
    mpoly_heap_t * chain;
    mpoly_heap_t ** Q;
    mpoly_heap_t * x;
    fmpz * p1 = *poly1;
    ulong * e1 = *exp1;
    ulong exp, cy;
    ulong c[3], p[2]; /* for accumulating coefficients */
    int first, small;
    TMP_INIT;

    TMP_START;

    /* whether input coeffs are small, thus output coeffs fit in three words */
    small = _fmpz_mpoly_fits_small(poly2, len2) &&
                                           _fmpz_mpoly_fits_small(poly3, len3);

    heap = (mpoly_heap1_s *) TMP_ALLOC((len2 + 1)*sizeof(mpoly_heap1_s));
    /* alloc array of heap nodes which can be chained together */
    chain = (mpoly_heap_t *) TMP_ALLOC(len2*sizeof(mpoly_heap_t));
    /* space for temporary storage of pointers to heap nodes */
    Q = (mpoly_heap_t **) TMP_ALLOC(len2*sizeof(mpoly_heap_t *));
   
    /* start with no heap nodes in use */
    next_free = 0;

    /* put all the starting nodes on the heap */
    for (i = 0; i < len2; i++)
        if (start[i] < end[i])
        {
            x = chain + next_free++;
            x->i = i;
            x->j = start[i];
            x->next = NULL;
            _mpoly_heap_insert1(heap, exp2[x->i] + exp3[x->j], x, &heap_len,
                                                                       maskhi);
        }

    /* output poly index starts at -1, will be immediately updated to 0 */
    k = -WORD(1);

    /* while heap is nonempty */
    while (heap_len > 1)
    {
        /* get exponent field of heap top */
        exp = heap[1].exp;
      
        /* realloc output poly ready for next product term */
        k++;
        _fmpz_mpoly_fit_length(&p1, &e1, alloc, k + 1, 1);

        /* whether we are on first coeff product for this output exponent */
        first = 1;

        /* set temporary coeff to zero */
        c[0] = c[1] = c[2] = 0;

        /* while heap nonempty and contains chain with current output exponent */
        while (heap_len > 1 && heap[1].exp == exp)
        {
            /* pop chain from heap */
            x = _mpoly_heap_pop1(heap, &heap_len, maskhi);

            /* if output coeffs will fit in three words */
            if (small)
            {
                /* compute product of input poly coeffs */
                if (first)
                {
                    smul_ppmm(c[1], c[0], poly2[x->i], poly3[x->j]);
                    c[2] = -(c[1] >> (FLINT_BITS - 1));

                    /* set output monomial */
                    e1[k] = exp;
                    first = 0; 
                } else /* addmul product of input poly coeffs */
                {
                    smul_ppmm(p[1], p[0], poly2[x->i], poly3[x->j]);
                    add_sssaaaaaa(cy, c[1], c[0], 0, c[1], c[0], 0, p[1], p[0]);
                    c[2] += (0 <= (slong) p[1]) ? cy : cy - 1;
                }
      
                /* temporarily store pointer to this node */
                if (x->j < len3 - 1)
                    Q[Q_len++] = x;

                /* for every node in this chain */
                while ((x = x->next) != NULL)
                {          
                    /* addmul product of input poly coeffs */
                    smul_ppmm(p[1], p[0], poly2[x->i], poly3[x->j]);
                    add_sssaaaaaa(cy, c[1], c[0], 0, c[1], c[0], 0, p[1], p[0]);
                    c[2] += (0 <= (slong) p[1]) ? cy : cy - 1;

                    /* temporarily store pointer to this node */
                    if (x->j < len3 - 1)
                        Q[Q_len++] = x;
                }
           } else /* output coeffs require multiprecision */
           {
                if (first) /* compute product of input poly coeffs */
                {
                    fmpz_mul(p1 + k, poly2 + x->i, poly3 + x->j);
               
                    e1[k] = exp;
                    first = 0; 
                } else /* addmul product of input poly coeffs */
                    fmpz_addmul(p1 + k, poly2 + x->i, poly3 + x->j);

                /* temporarily store pointer to this node */
                if (x->j < len3 - 1 || x->j == 0)
                    Q[Q_len++] = x;

               /* for each node in this chain */
               while ((x = x->next) != NULL)
               {
                    /* addmul product of input poly coeffs */
                    fmpz_addmul(p1 + k, poly2 + x->i, poly3 + x->j);

                    /* temporarily store pointer to this node */
                    if (x->j < len3 - 1 || x->j == 0)
                        Q[Q_len++] = x;
                }
            }
        }

        /* for each node temporarily stored */
        while (Q_len > 0)
        {
            /* take node from store */
            x = Q[--Q_len];

            if (x->j + 1 < end[x->i])
            {
                x->j++;
                x->next = NULL;
                _mpoly_heap_insert1(heap, exp2[x->i] + exp3[x->j], x, &heap_len,
                                                                       maskhi);
            }
        }

        /* set output poly coeff from temporary accumulation, if not multiprec */
        if (small)
            fmpz_set_signed_uiuiui(p1 + k, c[2], c[1], c[0]);

        if (fmpz_is_zero(p1 + k))
            k--;
    }

    k++;

    (*poly1) = p1;
    (*exp1) = e1;
   
    TMP_END;

    return k;
}


/*
    Set poly1 to poly2*poly3 using Johnson's heap method. The function
    realocates its output and returns the length of the product. This
    version of the function assumes the exponent vectors take N words.
    Only terms t with start >= t > end are written;
    "start" and "end" are not monomials but arrays of indicies into exp3
*/
slong _fmpz_mpoly_mul_heap_part(fmpz ** poly1, ulong ** exp1, slong * alloc,
                 const fmpz * poly2, const ulong * exp2, slong len2,
                 const fmpz * poly3, const ulong * exp3, slong len3,
            slong * start, slong * end, slong N, ulong maskhi, ulong masklo)
{
    slong i, k;
    slong next_free, Q_len = 0, heap_len = 1; /* heap zero index unused */
    mpoly_heap_s * heap;
    mpoly_heap_t * chain;
    mpoly_heap_t ** Q;
    mpoly_heap_t * x;
    fmpz * p1 = *poly1;
    ulong * e1 = *exp1;
    ulong cy;
    ulong c[3], p[2]; /* for accumulating coefficients */
    ulong * exp, * exps;
    ulong ** exp_list;
    slong exp_next;
    int first, small;

    TMP_INIT;

    /* if exponent vectors fit in single word, call special version */
    if (N == 1)
        return _fmpz_mpoly_mul_heap_part1(poly1, exp1, alloc,
                                          poly2, exp2, len2,
                                          poly3, exp3, len3,
                                                    start, end, maskhi);

    TMP_START;

    /* whether input coeffs are small, thus output coeffs fit in three words */
    small = _fmpz_mpoly_fits_small(poly2, len2) &&
                                           _fmpz_mpoly_fits_small(poly3, len3);


    heap = (mpoly_heap_s *) TMP_ALLOC((len2 + 1)*sizeof(mpoly_heap_s));
    /* alloc array of heap nodes which can be chained together */
    chain = (mpoly_heap_t *) TMP_ALLOC(len2*sizeof(mpoly_heap_t));
    /* space for temporary storage of pointers to heap nodes */
    Q = (mpoly_heap_t **) TMP_ALLOC(len2*sizeof(mpoly_heap_t *));
    /* allocate space for exponent vectors of N words */
    exps = (ulong *) TMP_ALLOC(len2*N*sizeof(ulong));
    /* list of pointers to allocated exponent vectors */
    exp_list = (ulong **) TMP_ALLOC(len2*sizeof(ulong *));

    for (i = 0; i < len2; i++)
    {
      exp_list[i] = exps + i*N;
    }

    /* start with no heap nodes and no exponent vectors in use */
    next_free = 0;
    exp_next = 0;


    /* put all the starting nodes on the heap */
    for (i = 0; i < len2; i++)
        if (start[i] < end[i])
        {
            x = chain + next_free++;
            x->i = i;
            x->j = start[i];
            x->next = NULL;
            mpoly_monomial_add(exp_list[exp_next], exp2 + x->i*N,
                                                             exp3 + x->j*N, N);
            if (!_mpoly_heap_insert(heap, exp_list[exp_next++], x, &heap_len,
                                                            N, maskhi, masklo))
               exp_next--;
        }

    /* output poly index starts at -1, will be immediately updated to 0 */
    k = -WORD(1);

    /* while heap is nonempty */
    while (heap_len > 1)
    {
        /* get pointer to exponent field of heap top */
        exp = heap[1].exp;

        /* realloc output poly ready for next product term */
        k++;
        _fmpz_mpoly_fit_length(&p1, &e1, alloc, k + 1, N);

        /* whether we are on first coeff product for this output exponent */
        first = 1;

        /* set temporary coeff to zero */
        c[0] = c[1] = c[2] = 0;

        /* while heap nonempty and contains chain with current output exponent */
        while (heap_len > 1 && mpoly_monomial_equal(heap[1].exp, exp, N))
        {
            /* pop chain from heap and set exponent field to be reused */
            exp_list[--exp_next] = heap[1].exp;

            x = _mpoly_heap_pop(heap, &heap_len, N, maskhi, masklo);

            /* if output coeffs will fit in three words */
            if (small)
            {
                /* compute product of input poly coeffs */
                if (first)
                {
                    smul_ppmm(c[1], c[0], poly2[x->i], poly3[x->j]);
                    c[2] = -(c[1] >> (FLINT_BITS - 1));

                    /* set output monomial */
                    mpoly_monomial_set(e1 + k*N, exp, N);

                    first = 0; 
                } else /* addmul product of input poly coeffs */
                {
                    smul_ppmm(p[1], p[0], poly2[x->i], poly3[x->j]);
                    add_sssaaaaaa(cy, c[1], c[0], 0, c[1], c[0], 0, p[1], p[0]);
                    c[2] += (0 <= (slong) p[1]) ? cy : cy - 1;
                }

                /* temporarily store pointer to this node */
                if (x->j < len3 - 1 || x->j == 0)
                    Q[Q_len++] = x;

                /* for every node in this chain */
                while ((x = x->next) != NULL)
                {
                    /* addmul product of input poly coeffs */
                    smul_ppmm(p[1], p[0], poly2[x->i], poly3[x->j]);
                    add_sssaaaaaa(cy, c[1], c[0], 0, c[1], c[0], 0, p[1], p[0]);
                    c[2] += (0 <= (slong) p[1]) ? cy : cy - 1;

                    /* temporarily store pointer to this node */
                    if (x->j < len3 - 1 || x->j == 0)
                        Q[Q_len++] = x;
                }
            } else /* output coeffs require multiprecision */
            {
                if (first) /* compute product of input poly coeffs */
                {
                    fmpz_mul(p1 + k, poly2 + x->i, poly3 + x->j);

                    /* set output monomial */
                    mpoly_monomial_set(e1 + k*N, exp, N);

                    first = 0; 
                } else /* addmul product of input poly coeffs */
                    fmpz_addmul(p1 + k, poly2 + x->i, poly3 + x->j);

                /* temporarily store pointer to this node */
                if (x->j < len3 - 1 || x->j == 0)
                    Q[Q_len++] = x;

                /* for each node in this chain */
                while ((x = x->next) != NULL)
                {
                    /* addmul product of input poly coeffs */
                    fmpz_addmul(p1 + k, poly2 + x->i, poly3 + x->j);

                    /* temporarily store pointer to this node */
                    if (x->j < len3 - 1 || x->j == 0)
                        Q[Q_len++] = x;
                }
            }
        }
      
        /* for each node temporarily stored */
        while (Q_len > 0)
        {
            /* take node from store */
            x = Q[--Q_len];

            /* do not put next on heap if it is past the end */
            if (x->j + 1 < end[x->i])
            {
                x->j++;
                x->next = NULL;

                mpoly_monomial_add(exp_list[exp_next], exp2 + x->i*N,
                                                       exp3 + x->j*N, N);
                if (!_mpoly_heap_insert(heap, exp_list[exp_next++], x,
                                                 &heap_len, N, maskhi, masklo))
                   exp_next--;
            }
        }     
   
        /* set output poly coeff from temporary accumulation, if not multiprec */
        if (small)
            fmpz_set_signed_uiuiui(p1 + k, c[2], c[1], c[0]);

        if (fmpz_is_zero(p1 + k))
            k--;

    }


    k++;

    (*poly1) = p1;
    (*exp1) = e1;

    TMP_END;

    return k;
}


/*
    The workers calculate product terms from 4*n divisions, where n is the
    number of threads. 
    This contains the address of a mul_heap_threaded_div_t structure
*/

typedef struct
{
    pthread_mutex_t mutex;
    slong nthreads;
    slong ndivs;
    const fmpz * coeff2; const ulong * exp2; slong len2;
    const fmpz * coeff3; const ulong * exp3; slong len3;
    slong N;
    ulong maskhi; ulong masklo;    
    volatile int idx;
}
mul_heap_threaded_base_t;

typedef struct
{
    slong * t1, * t2, *t3;
    slong * line;
    ulong * exp;
    slong score;
    slong lower;
    slong upper;
    slong threadidx;
    slong time;
    slong len1;
    slong alloc1;
    ulong * exp1;
    fmpz * coeff1;
}
mul_heap_threaded_div_t;

typedef struct
{
    slong idx;
    slong time;
    mul_heap_threaded_base_t * basep;
    mul_heap_threaded_div_t * divp;
    volatile slong stage;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
}
mul_heap_threaded_arg_t;


/*
    The workers simply take the next available division and calculate all
    product terms in this division.
*/
void * _fmpz_mpoly_mul_heap_threaded_worker(void * arg_ptr)
{
    mul_heap_threaded_arg_t * arg = (mul_heap_threaded_arg_t *) arg_ptr;

    mul_heap_threaded_div_t * divs;
    mul_heap_threaded_base_t * base;
    slong i, j, * dummy;

    divs = arg->divp;
    base = arg->basep;

    pthread_mutex_lock(&base->mutex);
    i = base->idx - 1;
    base->idx = i;
    pthread_mutex_unlock(&base->mutex);
    while (i >= 0)
    {
        if (i > 0)
        {
            divs[i].len1 = _fmpz_mpoly_mul_heap_part(
                         &divs[i].coeff1, &divs[i].exp1, &divs[i].alloc1,
                          base->coeff2,  base->exp2,  base->len2,
                          base->coeff3,  base->exp3,  base->len3,
                           divs[i].line, divs[i-1].line,
                                          base->N, base->maskhi, base->masklo);
        } else
        {
            dummy = flint_malloc(base->len2*sizeof(slong));
            for (j = 0; j < base->len2; j++)
                dummy[j] = base->len3;

            divs[i].len1 = _fmpz_mpoly_mul_heap_part(
                         &divs[i].coeff1, &divs[i].exp1, &divs[i].alloc1,
                          base->coeff2,  base->exp2,  base->len2,
                          base->coeff3,  base->exp3,  base->len3,
                           divs[i].line, dummy,
                                          base->N, base->maskhi, base->masklo);
            flint_free(dummy);
        }

        pthread_mutex_lock(&base->mutex);
        i = base->idx - 1;
        base->idx = i;
        pthread_mutex_unlock(&base->mutex);
    }
    flint_cleanup();
    return NULL;
}


/*
    First, 4*n ranges are calculated. Then, the product terms in each of these
    ranges are calculated. Finally, the results are joined into poly1.
*/
slong _fmpz_mpoly_mul_heap_threaded(fmpz ** poly1, ulong ** exp1, slong * alloc,
                 const fmpz * coeff2, const ulong * exp2, slong len2,
                 const fmpz * coeff3, const ulong * exp3, slong len3,
                                           slong N, ulong maskhi, ulong masklo)
{
    slong i, j, k, ndivs2;
    pthread_t * threads;
    mul_heap_threaded_arg_t * args;
    mul_heap_threaded_base_t * base;
    mul_heap_threaded_div_t * divs;
    fmpz * p1;
    ulong * e1;

    base = flint_malloc(sizeof(mul_heap_threaded_base_t));
    base->nthreads = flint_get_num_threads();
    base->ndivs    = base->nthreads*4;  /* number of divisons */
    base->coeff2 = coeff2;
    base->exp2 = exp2;
    base->len2 = len2;
    base->coeff3 = coeff3;
    base->exp3 = exp3;
    base->len3 = len3;
    base->N = N;
    base->maskhi = maskhi;
    base->masklo = masklo;
    base->idx = base->ndivs;    /* decremented by worker threads */

    ndivs2 = base->ndivs*base->ndivs;

    divs    = flint_malloc(sizeof(mul_heap_threaded_div_t) * base->ndivs);
    threads = flint_malloc(sizeof(pthread_t) * base->nthreads);
    args    = flint_malloc(sizeof(mul_heap_threaded_arg_t) * base->nthreads);

    for (i = base->ndivs - 1; i >= 0; i--)
    {
        /* divisions decrease in size so that no worker finishes too early */
        divs[i].lower = (i + 1)*(i + 1)*len2*len3/ndivs2;
        divs[i].upper = (i + 1)*(i + 1)*len2*len3/ndivs2;

        divs[i].line = NULL;
        divs[i].exp = (ulong *) flint_malloc(N*sizeof(ulong));
        divs[i].t1 = (slong *) flint_malloc(len2*sizeof(slong));
        divs[i].t2 = (slong *) flint_malloc(len2*sizeof(slong));
        divs[i].t3 = (slong *) flint_malloc(len2*sizeof(slong));

        divs[i].len1 = 0;
        k = 0; /* avoid bogus warning */
        if (i == base->ndivs - 1)
        {
            /* highest division writes to original poly */
            divs[i].alloc1 = *alloc;
            divs[i].exp1 = *exp1;
            divs[i].coeff1 = *poly1;
            /* keep this many coeffs from original poly */
            k = (ndivs2 - i*i)*(*alloc)/ndivs2;
        } else {
            /* lower divisions write to a new worker poly */
            divs[i].alloc1 = len2 + len3/base->ndivs;
            divs[i].exp1 = (ulong *) flint_malloc(divs[i].alloc1*N*sizeof(ulong)); 
            divs[i].coeff1 = (fmpz *) flint_calloc(divs[i].alloc1, sizeof(fmpz));
            /* try to take this many coeffs from original poly */
            for (j = 0; j < divs[i].alloc1
                   && k < *alloc
                   && k < (ndivs2 - i*i)*(*alloc) / ndivs2; j++, k++)
                fmpz_swap(*poly1 + k, divs[i].coeff1 + j);
        }
    }

    /* search step could be done in parallel, but there is little benifit */
    for (i = 0; i < base->ndivs; i++)
    {
        mpoly_search_monomials(
            &divs[i].line, divs[i].exp, &divs[i].score,
                            divs[i].t1, divs[i].t2, divs[i].t3,
                            divs[i].lower, divs[i].lower,
                            base->exp2, base->len2, base->exp3, base->len3,
                                          base->N, base->maskhi, base->masklo);
    }

    /* do the multiplications in parallel */
    pthread_mutex_init(&base->mutex, NULL);
    for (i = 0; i < base->nthreads; i++)
    {
        args[i].idx = i;
        args[i].basep = base;
        args[i].divp = divs;
        pthread_create(&threads[i], NULL, _fmpz_mpoly_mul_heap_threaded_worker,
                                                                     &args[i]);
    }
    for (i = 0; i < base->nthreads; i++)
        pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&base->mutex);

    /* concatenate the outputs */ 
    k = 0; /* avoid bogus warning */
    for (i = base->ndivs - 1; i >= 0; i--)
    {
        flint_free(divs[i].t3);
        flint_free(divs[i].t2);
        flint_free(divs[i].t1);
        flint_free(divs[i].exp);

        if (i + 1 < base->ndivs)
        {
            /* transfer from worker poly to original poly */
            for (j = 0; j < divs[i].len1; j++)
            {
                _fmpz_mpoly_fit_length(&p1, &e1, alloc, k+1, N);
                fmpz_swap(p1 + k, divs[i].coeff1 + j);
                fmpz_clear(divs[i].coeff1 + j);
                mpoly_monomial_set(e1 + N*k, divs[i].exp1 + N*j, N);
                k++;
            }
            /* clear remaining coeffs */
            for ( ; j < divs[i].alloc1; j++)
                fmpz_clear(divs[i].coeff1 + j);
            flint_free(divs[i].coeff1);
            flint_free(divs[i].exp1);
        } else
        {
            /* highest thread used original poly */
            k = divs[i].len1;
            p1 = divs[i].coeff1;
            e1 = divs[i].exp1;
            *alloc = divs[i].alloc1;
        }
    }

    flint_free(args);
    flint_free(threads);
    flint_free(divs);
    flint_free(base);

    *poly1 = p1;
    *exp1  = e1;
    return k;
}

void fmpz_mpoly_mul_heap_threaded(fmpz_mpoly_t poly1, const fmpz_mpoly_t poly2,
                          const fmpz_mpoly_t poly3, const fmpz_mpoly_ctx_t ctx)
{
    slong i, bits, exp_bits, N, len = 0;
    ulong * max_degs2;
    ulong * max_degs3;
    ulong maskhi, masklo;
    ulong max;
    ulong * exp2 = poly2->exps, * exp3 = poly3->exps;
    int free2 = 0, free3 = 0;

    TMP_INIT;

    /* one of the input polynomials is zero */
    if (poly2->length == 0 || poly3->length == 0)
    {
        fmpz_mpoly_zero(poly1, ctx);
        return;
    }

    TMP_START;

    /* compute maximum degree of any variable */
    max_degs2 = (ulong *) TMP_ALLOC(ctx->n*sizeof(ulong));
    max_degs3 = (ulong *) TMP_ALLOC(ctx->n*sizeof(ulong));

    fmpz_mpoly_max_degrees(max_degs2, poly2, ctx);
    fmpz_mpoly_max_degrees(max_degs3, poly3, ctx);

    max = 0;

    for (i = 0; i < ctx->n; i++)
    {
        max_degs3[i] += max_degs2[i];
        /*check exponents won't overflow */
        if (max_degs3[i] < max_degs2[i] || 0 > (slong) max_degs3[i]) 
            flint_throw(FLINT_EXPOF, "Exponent overflow in fmpz_mpoly_mul_johnson");

        if (max_degs3[i] > max)
            max = max_degs3[i];
    }

    /* compute number of bits to store maximum degree */
    bits = FLINT_BIT_COUNT(max);
    if (bits >= FLINT_BITS)
        flint_throw(FLINT_EXPOF, "Exponent overflow in fmpz_mpoly_mul_johnson");

    exp_bits = 8;
    while (bits >= exp_bits) /* extra bit required for signs */
        exp_bits *= 2;

    exp_bits = FLINT_MAX(exp_bits, poly2->bits);
    exp_bits = FLINT_MAX(exp_bits, poly3->bits);

    masks_from_bits_ord(maskhi, masklo, exp_bits, ctx->ord);
    /* number of words exponent vectors packed into */
    N = (exp_bits*ctx->n - 1)/FLINT_BITS + 1;

    /* ensure input exponents are packed into same sized fields as output */
    if (exp_bits > poly2->bits)
    {
        free2 = 1;
        exp2 = (ulong *) flint_malloc(N*poly2->length*sizeof(ulong));
        mpoly_unpack_monomials(exp2, exp_bits, poly2->exps, poly2->bits,
                                                        poly2->length, ctx->n);
    }

    if (exp_bits > poly3->bits)
    {
        free3 = 1;
        exp3 = (ulong *) flint_malloc(N*poly3->length*sizeof(ulong));
        mpoly_unpack_monomials(exp3, exp_bits, poly3->exps, poly3->bits,
                                                        poly3->length, ctx->n);
    }

    /* deal with aliasing and do multiplication */
    if (poly1 == poly2 || poly1 == poly3)
    {
        fmpz_mpoly_t temp;

        fmpz_mpoly_init2(temp, poly2->length + poly3->length - 1, ctx);
        fmpz_mpoly_fit_bits(temp, exp_bits, ctx);
        temp->bits = exp_bits;

        /* algorithm more efficient if smaller poly first */
        if (poly2->length >= poly3->length)
            len = _fmpz_mpoly_mul_heap_threaded(
                                    &temp->coeffs, &temp->exps, &temp->alloc,
                                      poly3->coeffs, exp3, poly3->length,
                                      poly2->coeffs, exp2, poly2->length,
                                               N, maskhi, masklo);
        else
            len = _fmpz_mpoly_mul_heap_threaded(
                                   &temp->coeffs, &temp->exps, &temp->alloc,
                                      poly2->coeffs, exp2, poly2->length,
                                      poly3->coeffs, exp3, poly3->length,
                                               N, maskhi, masklo);

        fmpz_mpoly_swap(temp, poly1, ctx);

        fmpz_mpoly_clear(temp, ctx);
    } else
    {
        fmpz_mpoly_fit_length(poly1, poly2->length + poly3->length - 1, ctx);
        fmpz_mpoly_fit_bits(poly1, exp_bits, ctx);
        poly1->bits = exp_bits;

        /* algorithm more efficient if smaller poly first */
        if (poly2->length > poly3->length)
            len = _fmpz_mpoly_mul_heap_threaded(
                                &poly1->coeffs, &poly1->exps, &poly1->alloc,
                                      poly3->coeffs, exp3, poly3->length,
                                      poly2->coeffs, exp2, poly2->length,
                                               N, maskhi, masklo);
        else
            len = _fmpz_mpoly_mul_heap_threaded(
                                &poly1->coeffs, &poly1->exps, &poly1->alloc,
                                      poly2->coeffs, exp2, poly2->length,
                                      poly3->coeffs, exp3, poly3->length,
                                               N, maskhi, masklo);
    }

    if (free2)
        flint_free(exp2);

    if (free3)
        flint_free(exp3);

    _fmpz_mpoly_set_length(poly1, len, ctx);

    TMP_END;
}
