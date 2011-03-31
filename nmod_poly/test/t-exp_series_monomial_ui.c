/*=============================================================================

    This file is part of FLINT.

    FLINT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    FLINT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FLINT; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

=============================================================================*/
/******************************************************************************

    Copyright (C) 2010 William Hart
    Copyright (C) 2011 Fredrik Johansson

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <mpir.h>
#include "flint.h"
#include "nmod_poly.h"
#include "ulong_extras.h"

int
main(void)
{
    int i, result = 1;
    flint_rand_t state;
    flint_randinit(state);

    printf("exp_series_monomial_ui....");
    fflush(stdout);

    for (i = 0; i < 10000; i++)
    {
        nmod_poly_t A, expA, res;
        long n;
        mp_limb_t mod;
        ulong power;
        mp_limb_t coeff;

        mod = n_randtest_prime(state, 0);
        n = n_randtest(state) % 100;
        n = FLINT_MIN(n, mod);

        nmod_poly_init(A, mod);
        nmod_poly_init(expA, mod);
        nmod_poly_init(res, mod);

        coeff = n_randlimb(state) % mod;
        power = 1 + n_randint(state, 2*n + 1);

        nmod_poly_set_coeff_ui(A, power, coeff);

        nmod_poly_exp_series(expA, A, n);
        nmod_poly_exp_series_monomial_ui(res, coeff, power, n);

        result = nmod_poly_equal(expA, res);

        if (!result)
        {
            printf("FAIL:\n");
            printf("n = %ld, mod = %lu\n", n, mod);
            printf("power = %lu, coeff = %lu\n", power, coeff);
            printf("A: "); nmod_poly_print(A), printf("\n\n");
            printf("exp(A): "); nmod_poly_print(expA), printf("\n\n");
            printf("res: "); nmod_poly_print(res), printf("\n\n");
            abort();
        }

        nmod_poly_clear(A);
        nmod_poly_clear(expA);
        nmod_poly_clear(res);
    }

    flint_randclear(state);

    printf("PASS\n");
    return 0;
}
