/*
    Copyright (C) 2016 Daniel Schultz

    This file is part of FLINT.

    FLINT is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include "flint.h"
#include "fmpz.h"
#include "mpoly.h"
#include "fmpz_mpoly.h"
#include "profiler.h"
#include "ulong_extras.h"

int
main(void)
{

    int i, j, k;

    FLINT_TEST_INIT(state);

    flint_printf("search_monomial....");
    fflush(stdout);

    /* get two random polys and test output of search */
    for (k = 0; k < 1000*flint_test_multiplier(); k++)
    {
        fmpz_mpoly_ctx_t ctx;
        fmpz_mpoly_t f, g;
        ordering_t ord;
        slong nvars, len1, len2, exp_bound1, exp_bound2;
        slong coeff_bits, exp_bits1, exp_bits2, fg_bits;
        ulong * e, * fexp, * gexp, * temp;
        slong e_score, * e_ind, *t1, *t2, *t3, score, x;
        slong lower, upper, N;
        ulong maskhi, masklo;

        ord = mpoly_ordering_randtest(state);
        nvars = n_randint(state, 10) + 1;

        fmpz_mpoly_ctx_init(ctx, nvars, ord);

        fmpz_mpoly_init(f, ctx);
        fmpz_mpoly_init(g, ctx);

        len1 = n_randint(state, 100) + 1;
        len2 = n_randint(state, 100) + 1;

        exp_bits1 = n_randint(state, 20/(nvars + 
                            mpoly_ordering_isdeg(ord) + (nvars == 1)) + 1) + 1;
        exp_bits2 = n_randint(state, 20/(nvars + 
                            mpoly_ordering_isdeg(ord) + (nvars == 1)) + 1) + 1;
        exp_bound1 = n_randbits(state, exp_bits1);
        exp_bound2 = n_randbits(state, exp_bits2);
       
        coeff_bits = n_randint(state, 100) + 1;

        do {
            fmpz_mpoly_randtest(f, state, len1, exp_bound1, coeff_bits, ctx);
        } while (f->length == 0);
        do {
            fmpz_mpoly_randtest(g, state, len2, exp_bound2, coeff_bits, ctx);
        } while (g->length == 0);

        fg_bits = FLINT_MAX(f->bits, g->bits);
        masks_from_bits_ord(maskhi, masklo, fg_bits, ctx->ord);
        N = (ctx->n*fg_bits - 1)/FLINT_BITS + 1;

        fexp = (ulong *) flint_malloc(f->length*N*sizeof(ulong));
        gexp = (ulong *) flint_malloc(g->length*N*sizeof(ulong));
        e = (ulong *) flint_malloc(N*sizeof(ulong));
        temp = (ulong *) flint_malloc(N*sizeof(ulong));
        t1 = (slong *) flint_malloc(f->length*N*sizeof(slong));
        t2 = (slong *) flint_malloc(f->length*N*sizeof(slong));
        t3 = (slong *) flint_malloc(f->length*N*sizeof(slong));

        mpoly_unpack_monomials(fexp, fg_bits, f->exps, f->bits, f->length, ctx->n);
        mpoly_unpack_monomials(gexp, fg_bits, g->exps, g->bits, g->length, ctx->n);

        lower = n_randint(state, f->length*g->length);
        upper = n_randint(state, f->length*g->length);
        if (upper < lower)
        {
            x = lower;
            lower = upper;
            upper = x;
        }

        mpoly_search_monomials(&e_ind, e, &e_score, t1, t2, t3, lower, upper,
                          fexp, f->length, gexp, g->length, N, maskhi, masklo);

        /* make sure that e_ind is correct for e */
        score = 0;
        for (i = 0; i < f->length; i++)
        {
            x = 0;
            for (j = 0; j < g->length; j++)
            {
                mpoly_monomial_add(temp, fexp + i*N, gexp + j*N, N);
                if (mpoly_monomial_lt(temp, e, N, maskhi, masklo))
                    x = j + 1;
            }
            if (x != e_ind[i])
            {
                flint_printf("e_ind is not right  x=%wd, e_ind[%wd]=%wd\n",x,i,e_ind[i]);
                flint_printf("lower = %wd  upper = %wd\n",lower,upper);
                flint_printf("e_score = %wx\n",e_score);
                flint_printf("e = %wx\n",e[0]);

                fmpz_mpoly_print_pretty(f,NULL,ctx);printf("\n\n");
                fmpz_mpoly_print_pretty(g,NULL,ctx);printf("\n\n");
                flint_abort();
            }

            score += g->length - x;
        }

        /* make sure that e_score is correct for e */
        if (score != e_score)
        {
            printf("e_score is not right\n");
            flint_abort();
        }

        /* if e_score is outside of [lower,upper] check that nothing closer works */
        if (e_score < lower || e_score > upper)
        {
            slong returned_error, new_error, i1, j1;
            ulong * temp1;

            temp1 = (ulong *) flint_malloc(N*sizeof(ulong));

            returned_error = e_score < lower ? lower - e_score : e_score - upper;

            for (i1 = 0; i1 < f->length; i1++)
            {
                for (j1 = 0; j1 < g->length; j1++)
                {
                    mpoly_monomial_add(temp1, fexp + i1*N, gexp + j1*N, N);
                    score = 0;
                    for (i = 0; i < f->length; i++)
                    {
                        x = 0;
                        for (j = 0; j < g->length; j++)
                        {
                            mpoly_monomial_add(temp, fexp + i*N, gexp + j*N, N);
                            if (mpoly_monomial_lt(temp, temp1, N, maskhi, masklo))
                                x = j + 1;
                        }
                        score += g->length - x;
                    }
                    if (!(score < lower || score > upper))
                    {
                        printf("returned score is outside, but score inside exists\n");
                        flint_abort();
                    }
                    new_error = score < lower ? lower - score : score - upper;
                    if (new_error < returned_error)
                    {
                        printf("returned score is not closest possible\n");
                        flint_abort();
                    }
                }
            }

            flint_free(temp1);
        }

        flint_free(fexp);
        flint_free(gexp);

        flint_free(temp);
        flint_free(t3);
        flint_free(t2);
        flint_free(t1);
        flint_free(e);

        fmpz_mpoly_clear(f, ctx);
        fmpz_mpoly_clear(g, ctx);
    }

    flint_printf("PASS\n");

    FLINT_TEST_CLEANUP(state);
    
    return 0;

}

