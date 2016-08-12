/* multilarge_nlinear/trust.c
 * 
 * Copyright (C) 2016 Patrick Alken
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_multilarge_nlinear.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_permutation.h>

#include "common.c"
#include "nielsen.c"

/*
 * This module contains a high level driver for a general trust
 * region nonlinear least squares solver. This container handles
 * the computation of all of the quantities relevant to all trust
 * region methods, including:
 *
 * residual vector: f_k = f(x_k)
 * Jacobian matrix: J_k = J(x_k)
 * gradient vector: g_k = J_k^T f_k
 * scaling matrix:  D_k
 */

typedef struct
{
  size_t n;                  /* number of observations */
  size_t p;                  /* number of parameters */
  double delta;              /* trust region radius */
  double mu;                 /* LM parameter */
  long nu;                   /* for updating LM parameter */
  gsl_vector *diag;          /* D = diag(J^T J) */
  gsl_vector *x_trial;       /* trial parameter vector */
  gsl_vector *f_trial;       /* trial function vector */
  gsl_vector *workn;         /* workspace, length n */

  void *trs_state;           /* workspace for trust region subproblem */
  void *solver_state;        /* workspace for linear least squares solver */

  double avratio;            /* current |a| / |v| */

  /* tunable parameters */
  gsl_multilarge_nlinear_parameters params;
} trust_state_t;

static void * trust_alloc (const gsl_multilarge_nlinear_parameters * params,
                           const size_t n, const size_t p);
static void trust_free(void *vstate);
static int trust_init(void *vstate, const gsl_vector * swts,
                      gsl_multilarge_nlinear_fdf *fdf, const gsl_vector *x,
                      gsl_vector *f, gsl_vector *g, gsl_matrix *JTJ);
static int trust_iterate(void *vstate, const gsl_vector *swts,
                         gsl_multilarge_nlinear_fdf *fdf,
                         gsl_vector *x, gsl_vector *f,
                         gsl_vector *g, gsl_matrix *JTJ, gsl_vector *dx);
static int trust_rcond(double * rcond, const gsl_matrix * JTJ, void * vstate);
static double trust_avratio(void *vstate);
static void trust_trial_step(const gsl_vector * x, const gsl_vector * dx,
                             gsl_vector * x_trial);
static double trust_calc_rho(const gsl_multilarge_nlinear_trust_state * trust_state,
                             const gsl_vector * f_trial, const gsl_vector * dx,
                             trust_state_t * state);
static int trust_eval_step(const gsl_multilarge_nlinear_trust_state * trust_state,
                           const gsl_vector * f_trial, const gsl_vector * dx,
                           double * rho, trust_state_t * state);
static double trust_scaled_norm(const gsl_vector *D, const gsl_vector *a);

static void *
trust_alloc (const gsl_multilarge_nlinear_parameters * params,
             const size_t n, const size_t p)
{
  trust_state_t *state;
  
  state = calloc(1, sizeof(trust_state_t));
  if (state == NULL)
    {
      GSL_ERROR_NULL ("failed to allocate lm state", GSL_ENOMEM);
    }

  state->diag = gsl_vector_alloc(p);
  if (state->diag == NULL)
    {
      GSL_ERROR_NULL ("failed to allocate space for diag", GSL_ENOMEM);
    }

  state->workn = gsl_vector_alloc(n);
  if (state->workn == NULL)
    {
      GSL_ERROR_NULL ("failed to allocate space for workn", GSL_ENOMEM);
    }

  state->x_trial = gsl_vector_alloc(p);
  if (state->x_trial == NULL)
    {
      GSL_ERROR_NULL ("failed to allocate space for x_trial", GSL_ENOMEM);
    }

  state->f_trial = gsl_vector_alloc(n);
  if (state->f_trial == NULL)
    {
      GSL_ERROR_NULL ("failed to allocate space for f_trial", GSL_ENOMEM);
    }

  state->trs_state = (params->trs->alloc)(params, n, p);
  if (state->trs_state == NULL)
    {
      GSL_ERROR_NULL ("failed to allocate space for trs state", GSL_ENOMEM);
    }

  if (params->solver != gsl_multilarge_nlinear_solver_none)
    {
      state->solver_state = (params->solver->alloc)(n, p);
      if (state->solver_state == NULL)
        {
          GSL_ERROR_NULL ("failed to allocate space for solver state", GSL_ENOMEM);
        }
    }

  state->n = n;
  state->p = p;
  state->delta = 0.0;
  state->params = *params;

  return state;
}

static void
trust_free(void *vstate)
{
  trust_state_t *state = (trust_state_t *) vstate;
  const gsl_multilarge_nlinear_parameters *params = &(state->params);

  if (state->diag)
    gsl_vector_free(state->diag);

  if (state->workn)
    gsl_vector_free(state->workn);

  if (state->x_trial)
    gsl_vector_free(state->x_trial);

  if (state->f_trial)
    gsl_vector_free(state->f_trial);

  if (state->trs_state)
    (params->trs->free)(state->trs_state);

  if (state->solver_state)
    (params->solver->free)(state->solver_state);

  free(state);
}

/*
trust_init()
  Initialize trust region solver

Inputs: vstate - workspace
        swts   - sqrt(W) vector
        fdf    - user callback functions
        x      - initial parameter values
        f      - (output) f(x) vector
        g      - (output) J(x)' f(x) vector

Return: success/error
*/

static int
trust_init(void *vstate, const gsl_vector *swts,
           gsl_multilarge_nlinear_fdf *fdf, const gsl_vector *x,
           gsl_vector *f, gsl_vector *g, gsl_matrix *JTJ)
{
  int status;
  trust_state_t *state = (trust_state_t *) vstate;
  const gsl_multilarge_nlinear_parameters *params = &(state->params);
  double Dx;

  /* evaluate function and Jacobian at x and apply weight transform */
  status = gsl_multilarge_nlinear_eval_f(fdf, x, swts, f);
  if (status)
   return status;

  /* compute g = J^T f and J^T J */
  status = gsl_multilarge_nlinear_eval_df(CblasTrans, x, f, f,
                                          swts, params->h_df, params->fdtype,
                                          fdf, g, JTJ, state->workn);
  if (status)
    return status;

  /* initialize diagonal scaling matrix D */
  if (JTJ != NULL)
    (params->scale->init)(JTJ, state->diag);
  else
    gsl_vector_set_all(state->diag, 1.0);

  /* compute initial trust region radius */
  Dx = trust_scaled_norm(state->diag, x);
  state->delta = 0.3 * GSL_MAX(1.0, Dx);

  /* initialize LM parameter */
  nielsen_init(JTJ, state->diag, &(state->mu), &(state->nu));

  /* initialize trust region method solver */
  {
    const gsl_multilarge_nlinear_trust_state trust_state = { x, f, g, JTJ, state->diag,
                                                             swts, &(state->mu), params,
                                                             state->solver_state, fdf,
                                                             &(state->avratio) };

    status = (params->trs->init)(&trust_state, state->trs_state);

    if (status)
      return status;
  }

  /* set default parameters */

  state->avratio = 0.0;

  return GSL_SUCCESS;
}

/*
trust_iterate()
  This function performs 1 iteration of the trust region algorithm.
It calls a user-specified method for computing the next step
(LM or dogleg), then tests if the computed step is acceptable.

Args: vstate - trust workspace
      swts   - data weights (NULL if unweighted)
      fdf    - function and Jacobian pointers
      x      - on input, current parameter vector
               on output, new parameter vector x + dx
      f      - on input, f(x)
               on output, f(x + dx)
      g      - on input, g(x) = J(x)' f(x)
               on output, g(x + dx) = J(x + dx)' f(x + dx)
      JTJ    - on input, J(x)^T J(x)
               on output, J(x + dx)^T J(x + dx)
      dx     - (output only) parameter step vector

Return:
1) GSL_SUCCESS if we found a step which reduces the cost
function

2) GSL_ENOPROG if 15 successive attempts were to made to
find a good step without success

3) If a scaling matrix D is used, inputs and outputs are
set to the unscaled quantities (ie: J and g)
*/

static int
trust_iterate(void *vstate, const gsl_vector *swts,
              gsl_multilarge_nlinear_fdf *fdf, gsl_vector *x,
              gsl_vector *f, gsl_vector *g, gsl_matrix *JTJ,
              gsl_vector *dx)
{
  int status;
  trust_state_t *state = (trust_state_t *) vstate;
  const gsl_multilarge_nlinear_parameters *params = &(state->params);
  const gsl_multilarge_nlinear_trs *trs = params->trs;

  /* collect all state parameters needed by low level methods */
  const gsl_multilarge_nlinear_trust_state trust_state = { x, f, g, JTJ, state->diag,
                                                         swts, &(state->mu), params,
                                                         state->solver_state, fdf,
                                                         &(state->avratio) };

  gsl_vector *x_trial = state->x_trial;       /* trial x + dx */
  gsl_vector *f_trial = state->f_trial;       /* trial f(x + dx) */
  double rho;                                 /* ratio actual_reduction/predicted_reduction */
  int foundstep = 0;                          /* found step dx */
  int bad_steps = 0;                          /* consecutive rejected steps */

  /* initialize trust region subproblem with this Jacobian */
  status = (trs->preloop)(&trust_state, state->trs_state);
  if (status)
    return status;

  /* loop until we find an acceptable step dx */
  while (!foundstep)
    {
      /* calculate new step */
      status = (trs->step)(&trust_state, state->delta, dx, state->trs_state);

      /* occasionally the iterative methods (ie: CG Steihaug) can fail to find a step,
       * so in this case skip rho calculation and count it as a rejected step */

      if (status == GSL_SUCCESS)
        {
          /* compute x_trial = x + dx */
          trust_trial_step(x, dx, x_trial);

          /* compute f_trial = f(x + dx) */
          status = gsl_multilarge_nlinear_eval_f(fdf, x_trial, swts, f_trial);
          if (status)
            return status;

          /* check if step should be accepted or rejected */
          status = trust_eval_step(&trust_state, f_trial, dx, &rho, state);
          if (status == GSL_SUCCESS)
            foundstep = 1;

#if 0 /*XXX*/
          fprintf(stdout, "delta = %.12e |D dx| = %.12e |dx| = %.12e, dx0 = %.12e dx1 = %.12e |x_trial| = %.12e |f_trial| = %.12e rho = %.12e\n",
                  state->delta,
                  scaled_enorm(state->diag, dx),
                  gsl_blas_dnrm2(dx),
                  gsl_vector_get(dx, 0),
                  gsl_vector_get(dx, 1),
                  gsl_blas_dnrm2(x_trial),
                  gsl_blas_dnrm2(f_trial),
                  rho);
#endif
        }
      else
        {
          /* an iterative TRS method failed to find a step vector */
          rho = -1.0;
        }

      /*
       * update trust region radius: if rho is large,
       * then the quadratic model is a good approximation
       * to the objective function, enlarge trust region.
       * If rho is small (or negative), the model function
       * is a poor approximation so decrease trust region. This
       * can happen even if the step is accepted.
       */
      if (rho > 0.75)
        state->delta *= params->factor_up;
      else if (rho < 0.25)
        state->delta /= params->factor_down;

      if (foundstep)
        {
          /* step was accepted */

          /* update x <- x + dx */
          gsl_vector_memcpy(x, x_trial);

          /* update f <- f(x + dx) */
          gsl_vector_memcpy(f, f_trial);

          /* compute new g = J^T f and J^T J */
          status = gsl_multilarge_nlinear_eval_df(CblasTrans, x, f, f,
                                                  swts, params->h_df, params->fdtype,
                                                  fdf, g, JTJ, state->workn);
          if (status)
            return status;

          /* update scaling matrix D */
          if (JTJ != NULL)
            (params->scale->update)(JTJ, state->diag);

          /* step accepted, decrease LM parameter */
          nielsen_accept(rho, &(state->mu), &(state->nu));

          bad_steps = 0;
        }
      else
        {
          /* step rejected, increase LM parameter */
          nielsen_reject(&(state->mu), &(state->nu));

          /* if more than 15 consecutive rejected steps, report no progress */
          if (++bad_steps > 15)
            {
              return GSL_ENOPROG;
            }
        }
    }

  return GSL_SUCCESS;
} /* trust_iterate() */

static int
trust_rcond(double * rcond, const gsl_matrix * JTJ, void * vstate)
{
  int status;
  trust_state_t *state = (trust_state_t *) vstate;
  const gsl_multilarge_nlinear_parameters *params = &(state->params);

  status = (params->solver->rcond)(rcond, JTJ, state->solver_state);

  return status;
}

static double
trust_avratio(void *vstate)
{
  trust_state_t *state = (trust_state_t *) vstate;
  return state->avratio;
}

/* compute x_trial = x + dx */
static void
trust_trial_step(const gsl_vector * x, const gsl_vector * dx,
                 gsl_vector * x_trial)
{
  size_t i, N = x->size;

  for (i = 0; i < N; i++)
    {
      double dxi = gsl_vector_get (dx, i);
      double xi = gsl_vector_get (x, i);
      gsl_vector_set (x_trial, i, xi + dxi);
    }
}

/*
trust_calc_rho()
  Calculate ratio of actual reduction to predicted
reduction.

rho = actual_reduction / predicted_reduction

actual_reduction = 1 - ( ||f+|| / ||f|| )^2
predicted_reduction = -2 g^T dx / ||f||^2 - ( ||J*dx|| / ||f|| )^2
                    = -2 fhat . beta - ||beta||^2

where: beta = J*dx / ||f||

Inputs: trust_state - trust state
        f_trial     - f(x + dx)
        dx          - proposed step, size p
        state       - workspace

Return: rho = actual_reduction / predicted_reduction
If actual_reduction is < 0, return rho = -1
*/

static double
trust_calc_rho(const gsl_multilarge_nlinear_trust_state * trust_state,
               const gsl_vector * f_trial, const gsl_vector * dx,
               trust_state_t * state)
{
  int status;
  const gsl_multilarge_nlinear_parameters *params = &(state->params);
  const gsl_multilarge_nlinear_trs *trs = params->trs;
  const gsl_vector * f = trust_state->f;
  const double normf = gsl_blas_dnrm2(f);
  const double normf_trial = gsl_blas_dnrm2(f_trial);
  double rho;
  double actual_reduction;
  double pred_reduction;
  double u;

  /* if ||f(x+dx)|| > ||f(x)|| reject step immediately */
  if (normf_trial >= normf)
    return -1.0;

  /* compute numerator of rho (actual reduction) */
  u = normf_trial / normf;
  actual_reduction = 1.0 - u*u;

  /*
   * compute denominator of rho (predicted reduction); this is calculated
   * inside each trust region subproblem, since it depends on the local
   * model used, which can vary according to each TRS
   */
  status = (trs->preduction)(trust_state, dx, &pred_reduction, state->trs_state);
  if (status)
    return -1.0;

  if (pred_reduction > 0.0)
    rho = actual_reduction / pred_reduction;
  else
    rho = -1.0;

  return rho;
}

/*
trust_eval_step()
  Evaluate proposed step to determine if it should be
accepted or rejected
*/

static int
trust_eval_step(const gsl_multilarge_nlinear_trust_state * trust_state,
                const gsl_vector * f_trial, const gsl_vector * dx,
                double * rho, trust_state_t * state)
{
  int status = GSL_SUCCESS;
  const gsl_multilarge_nlinear_parameters *params = &(state->params);

  if (params->trs == gsl_multilarge_nlinear_trs_lmaccel)
    {
      /* reject step if acceleration is too large compared to velocity */
      if (state->avratio > params->avmax)
        status = GSL_FAILURE;
    }

  /* compute rho */
  *rho = trust_calc_rho(trust_state, f_trial, dx, state);
  if (*rho <= 0.0)
    status = GSL_FAILURE;

  return status;
}

/* compute || diag(D) a || */
static double
trust_scaled_norm(const gsl_vector *D, const gsl_vector *a)
{
  const size_t n = a->size;
  double e2 = 0.0;
  size_t i;

  for (i = 0; i < n; ++i)
    {
      double Di = gsl_vector_get(D, i);
      double ai = gsl_vector_get(a, i);
      double u = Di * ai;

      e2 += u * u;
    }

  return sqrt (e2);
}

static const gsl_multilarge_nlinear_type trust_type =
{
  "trust-region",
  trust_alloc,
  trust_init,
  trust_iterate,
  trust_rcond,
  trust_avratio,
  trust_free
};

const gsl_multilarge_nlinear_type *gsl_multilarge_nlinear_trust = &trust_type;
