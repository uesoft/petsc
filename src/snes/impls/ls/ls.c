/*$Id: ls.c,v 1.153 2000/04/12 04:25:33 bsmith Exp bsmith $*/

#include "src/snes/impls/ls/ls.h"

/*  -------------------------------------------------------------------- 

     This file implements a truncated Newton method with a line search,
     for solving a system of nonlinear equations, using the SLES, Vec, 
     and Mat interfaces for linear solvers, vectors, and matrices, 
     respectively.

     The following basic routines are required for each nonlinear solver:
          SNESCreate_XXX()          - Creates a nonlinear solver context
          SNESSetFromOptions_XXX()  - Sets runtime options
          SNESSolve_XXX()           - Solves the nonlinear system
          SNESDestroy_XXX()         - Destroys the nonlinear solver context
     The suffix "_XXX" denotes a particular implementation, in this case
     we use _EQ_LS (e.g., SNESCreate_EQ_LS, SNESSolve_EQ_LS) for solving
     systems of nonlinear equations (EQ) with a line search (LS) method.
     These routines are actually called via the common user interface
     routines SNESCreate(), SNESSetFromOptions(), SNESSolve(), and 
     SNESDestroy(), so the application code interface remains identical 
     for all nonlinear solvers.

     Another key routine is:
          SNESSetUp_XXX()           - Prepares for the use of a nonlinear solver
     by setting data structures and options.   The interface routine SNESSetUp()
     is not usually called directly by the user, but instead is called by
     SNESSolve() if necessary.

     Additional basic routines are:
          SNESPrintHelp_XXX()       - Prints nonlinear solver runtime options
          SNESView_XXX()            - Prints details of runtime options that
                                      have actually been used.
     These are called by application codes via the interface routines
     SNESPrintHelp() and SNESView().

     The various types of solvers (preconditioners, Krylov subspace methods,
     nonlinear solvers, timesteppers) are all organized similarly, so the
     above description applies to these categories also.  

    -------------------------------------------------------------------- */
/*
   SNESSolve_EQ_LS - Solves a nonlinear system with a truncated Newton
   method with a line search.

   Input Parameters:
.  snes - the SNES context

   Output Parameter:
.  outits - number of iterations until termination

   Application Interface Routine: SNESSolve()

   Notes:
   This implements essentially a truncated Newton method with a
   line search.  By default a cubic backtracking line search 
   is employed, as described in the text "Numerical Methods for
   Unconstrained Optimization and Nonlinear Equations" by Dennis 
   and Schnabel.
*/
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSolve_EQ_LS"
int SNESSolve_EQ_LS(SNES snes,int *outits)
{
  SNES_EQ_LS          *neP = (SNES_EQ_LS*)snes->data;
  int                 maxits,i,ierr,lits,lsfail;
  MatStructure        flg = DIFFERENT_NONZERO_PATTERN;
  PetscReal           fnorm,gnorm,xnorm,ynorm;
  Vec                 Y,X,F,G,W,TMP;

  PetscFunctionBegin;
  snes->reason  = SNES_CONVERGED_ITERATING;

  maxits	= snes->max_its;	/* maximum number of iterations */
  X		= snes->vec_sol;	/* solution vector */
  F		= snes->vec_func;	/* residual vector */
  Y		= snes->work[0];	/* work vectors */
  G		= snes->work[1];
  W		= snes->work[2];

  ierr = PetscObjectTakeAccess(snes);CHKERRQ(ierr);
  snes->iter = 0;
  ierr = PetscObjectGrantAccess(snes);CHKERRQ(ierr);
  ierr = SNESComputeFunction(snes,X,F);CHKERRQ(ierr);  /*  F(X)      */
  ierr = VecNorm(F,NORM_2,&fnorm);CHKERRQ(ierr);	/* fnorm <- ||F||  */
  ierr = PetscObjectTakeAccess(snes);CHKERRQ(ierr);
  snes->norm = fnorm;
  ierr = PetscObjectGrantAccess(snes);CHKERRQ(ierr);
  SNESLogConvHistory(snes,fnorm,0);
  SNESMonitor(snes,0,fnorm);

  if (fnorm < snes->atol) {*outits = 0; snes->reason = SNES_CONVERGED_FNORM_ABS; PetscFunctionReturn(0);}

  /* set parameter for default relative tolerance convergence test */
  snes->ttol = fnorm*snes->rtol;

  for (i=0; i<maxits; i++) {

    /* Solve J Y = F, where J is Jacobian matrix */
    ierr = SNESComputeJacobian(snes,X,&snes->jacobian,&snes->jacobian_pre,&flg);CHKERRQ(ierr);
    ierr = SLESSetOperators(snes->sles,snes->jacobian,snes->jacobian_pre,flg);CHKERRQ(ierr);
    ierr = SLESSolve(snes->sles,F,Y,&lits);CHKERRQ(ierr);
    snes->linear_its += lits;
    PLogInfo(snes,"SNESSolve_EQ_LS: iter=%d, linear solve iterations=%d\n",snes->iter,lits);

    /* Compute a (scaled) negative update in the line search routine: 
         Y <- X - lambda*Y 
       and evaluate G(Y) = function(Y)) 
    */
    ierr = VecCopy(Y,snes->vec_sol_update_always);CHKERRQ(ierr);
    ierr = (*neP->LineSearch)(snes,neP->lsP,X,F,G,Y,W,fnorm,&ynorm,&gnorm,&lsfail);CHKERRQ(ierr);
    PLogInfo(snes,"SNESSolve_EQ_LS: fnorm=%g, gnorm=%g, ynorm=%g, lsfail=%d\n",fnorm,gnorm,ynorm,lsfail);
    if (lsfail) {
      snes->nfailures++;
      snes->reason = SNES_DIVERGED_LS_FAILURE;
      break;
    }

    TMP = F; F = G; snes->vec_func_always = F; G = TMP;
    TMP = X; X = Y; snes->vec_sol_always = X;  Y = TMP;
    fnorm = gnorm;

    ierr = PetscObjectTakeAccess(snes);CHKERRQ(ierr);
    snes->iter = i+1;
    snes->norm = fnorm;
    ierr = PetscObjectGrantAccess(snes);CHKERRQ(ierr);
    SNESLogConvHistory(snes,fnorm,lits);
    SNESMonitor(snes,i+1,fnorm);

    /* Test for convergence */
    if (snes->converged) {
      ierr = VecNorm(X,NORM_2,&xnorm);CHKERRQ(ierr);	/* xnorm = || X || */
      ierr = (*snes->converged)(snes,xnorm,ynorm,fnorm,&snes->reason,snes->cnvP);CHKERRQ(ierr);
      if (snes->reason) {
        break;
      }
    }
  }
  if (X != snes->vec_sol) {
    ierr = VecCopy(X,snes->vec_sol);CHKERRQ(ierr);
  }
  if (F != snes->vec_func) {
    ierr = VecCopy(F,snes->vec_func);CHKERRQ(ierr);
  }
  snes->vec_sol_always  = snes->vec_sol;
  snes->vec_func_always = snes->vec_func;
  if (i == maxits) {
    PLogInfo(snes,"SNESSolve_EQ_LS: Maximum number of iterations has been reached: %d\n",maxits);
    i--;
    snes->reason = SNES_DIVERGED_MAX_IT;
  }
  ierr = PetscObjectTakeAccess(snes);CHKERRQ(ierr);
  ierr = PetscObjectGrantAccess(snes);CHKERRQ(ierr);
  *outits = i+1;
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
/*
   SNESSetUp_EQ_LS - Sets up the internal data structures for the later use
   of the SNESEQLS nonlinear solver.

   Input Parameter:
.  snes - the SNES context
.  x - the solution vector

   Application Interface Routine: SNESSetUp()

   Notes:
   For basic use of the SNES solvers the user need not explicitly call
   SNESSetUp(), since these actions will automatically occur during
   the call to SNESSolve().
 */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSetUp_EQ_LS"
int SNESSetUp_EQ_LS(SNES snes)
{
  int ierr;

  PetscFunctionBegin;
  snes->nwork = 4;
  ierr = VecDuplicateVecs(snes->vec_sol,snes->nwork,&snes->work);CHKERRQ(ierr);
  PLogObjectParents(snes,snes->nwork,snes->work);
  snes->vec_sol_update_always = snes->work[3];
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
/*
   SNESDestroy_EQ_LS - Destroys the private SNESEQLS context that was created
   with SNESCreate_EQ_LS().

   Input Parameter:
.  snes - the SNES context

   Application Interface Routine: SNESDestroy()
 */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESDestroy_EQ_LS"
int SNESDestroy_EQ_LS(SNES snes)
{
  int  ierr;

  PetscFunctionBegin;
  if (snes->nwork) {
    ierr = VecDestroyVecs(snes->work,snes->nwork);CHKERRQ(ierr);
  }
  ierr = PetscFree(snes->data);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESNoLineSearch"

/*@C
   SNESNoLineSearch - This routine is not a line search at all; 
   it simply uses the full Newton step.  Thus, this routine is intended 
   to serve as a template and is not recommended for general use.  

   Collective on SNES and Vec

   Input Parameters:
+  snes - nonlinear context
.  lsctx - optional context for line search (not used here)
.  x - current iterate
.  f - residual evaluated at x
.  y - search direction (contains new iterate on output)
.  w - work vector
-  fnorm - 2-norm of f

   Output Parameters:
+  g - residual evaluated at new iterate y
.  y - new iterate (contains search direction on input)
.  gnorm - 2-norm of g
.  ynorm - 2-norm of search length
-  flag - set to 0, indicating a successful line search

   Options Database Key:
.  -snes_eq_ls basic - Activates SNESNoLineSearch()

   Level: advanced

.keywords: SNES, nonlinear, line search, cubic

.seealso: SNESCubicLineSearch(), SNESQuadraticLineSearch(), 
          SNESSetLineSearch(), SNESNoLineSearchNoNorms()
@*/
int SNESNoLineSearch(SNES snes,void *lsctx,Vec x,Vec f,Vec g,Vec y,Vec w,
                     PetscReal fnorm,PetscReal *ynorm,PetscReal *gnorm,int *flag)
{
  int        ierr;
  Scalar     mone = -1.0;
  SNES_EQ_LS *neP = (SNES_EQ_LS*)snes->data;
  PetscTruth change_y = PETSC_FALSE;

  PetscFunctionBegin;
  *flag = 0; 
  PLogEventBegin(SNES_LineSearch,snes,x,f,g);
  ierr = VecNorm(y,NORM_2,ynorm);CHKERRQ(ierr);  /* ynorm = || y || */
  ierr = VecAYPX(&mone,x,y);CHKERRQ(ierr);            /* y <- y - x      */
  if (neP->CheckStep) {
   ierr = (*neP->CheckStep)(snes,neP->checkP,y,&change_y);CHKERRQ(ierr);
  }
  ierr = SNESComputeFunction(snes,y,g);CHKERRQ(ierr); /* Compute F(y)    */
  ierr = VecNorm(g,NORM_2,gnorm);CHKERRQ(ierr);  /* gnorm = || g || */
  PLogEventEnd(SNES_LineSearch,snes,x,f,g);
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESNoLineSearchNoNorms"

/*@C
   SNESNoLineSearchNoNorms - This routine is not a line search at 
   all; it simply uses the full Newton step. This version does not
   even compute the norm of the function or search direction; this
   is intended only when you know the full step is fine and are
   not checking for convergence of the nonlinear iteration (for
   example, you are running always for a fixed number of Newton steps).

   Collective on SNES and Vec

   Input Parameters:
+  snes - nonlinear context
.  lsctx - optional context for line search (not used here)
.  x - current iterate
.  f - residual evaluated at x
.  y - search direction (contains new iterate on output)
.  w - work vector
-  fnorm - 2-norm of f

   Output Parameters:
+  g - residual evaluated at new iterate y
.  gnorm - not changed
.  ynorm - not changed
-  flag - set to 0, indicating a successful line search

   Options Database Key:
.  -snes_eq_ls basicnonorms - Activates SNESNoLineSearchNoNorms()

   Notes:
   SNESNoLineSearchNoNorms() must be used in conjunction with
   either the options
$     -snes_no_convergence_test -snes_max_it <its>
   or alternatively a user-defined custom test set via
   SNESSetConvergenceTest(); or a -snes_max_it of 1, 
   otherwise, the SNES solver will generate an error.

   During the final iteration this will not evaluate the function at
   the solution point. This is to save a function evaluation while
   using pseudo-timestepping.

   The residual norms printed by monitoring routines such as
   SNESDefaultMonitor() (as activated via -snes_monitor) will not be 
   correct, since they are not computed.

   Level: advanced

.keywords: SNES, nonlinear, line search, cubic

.seealso: SNESCubicLineSearch(), SNESQuadraticLineSearch(), 
          SNESSetLineSearch(), SNESNoLineSearch()
@*/
int SNESNoLineSearchNoNorms(SNES snes,void *lsctx,Vec x,Vec f,Vec g,Vec y,Vec w,
                            PetscReal fnorm,PetscReal *ynorm,PetscReal *gnorm,int *flag)
{
  int        ierr;
  Scalar     mone = -1.0;
  SNES_EQ_LS *neP = (SNES_EQ_LS*)snes->data;
  PetscTruth change_y = PETSC_FALSE;

  PetscFunctionBegin;
  *flag = 0; 
  PLogEventBegin(SNES_LineSearch,snes,x,f,g);
  ierr = VecAYPX(&mone,x,y);CHKERRQ(ierr);            /* y <- y - x      */
  if (neP->CheckStep) {
   ierr = (*neP->CheckStep)(snes,neP->checkP,y,&change_y);CHKERRQ(ierr);
  }
  
  /* don't evaluate function the last time through */
  if (snes->iter < snes->max_its-1) {
    ierr = SNESComputeFunction(snes,y,g);CHKERRQ(ierr); /* Compute F(y)    */
  }
  PLogEventEnd(SNES_LineSearch,snes,x,f,g);
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESCubicLineSearch"
/*@C
   SNESCubicLineSearch - Performs a cubic line search (default line search method).

   Collective on SNES

   Input Parameters:
+  snes - nonlinear context
.  lsctx - optional context for line search (not used here)
.  x - current iterate
.  f - residual evaluated at x
.  y - search direction (contains new iterate on output)
.  w - work vector
-  fnorm - 2-norm of f

   Output Parameters:
+  g - residual evaluated at new iterate y
.  y - new iterate (contains search direction on input)
.  gnorm - 2-norm of g
.  ynorm - 2-norm of search length
-  flag - 0 if line search succeeds; -1 on failure.

   Options Database Key:
$  -snes_eq_ls cubic - Activates SNESCubicLineSearch()

   Notes:
   This line search is taken from "Numerical Methods for Unconstrained 
   Optimization and Nonlinear Equations" by Dennis and Schnabel, page 325.

   Level: advanced

.keywords: SNES, nonlinear, line search, cubic

.seealso: SNESQuadraticLineSearch(), SNESNoLineSearch(), SNESSetLineSearch(), SNESNoLineSearchNoNorms()
@*/
int SNESCubicLineSearch(SNES snes,void *lsctx,Vec x,Vec f,Vec g,Vec y,Vec w,
                        PetscReal fnorm,PetscReal *ynorm,PetscReal *gnorm,int *flag)
{
  /* 
     Note that for line search purposes we work with with the related
     minimization problem:
        min  z(x):  R^n -> R,
     where z(x) = .5 * fnorm*fnorm, and fnorm = || f ||_2.
   */
        
  PetscReal  steptol,initslope,lambdaprev,gnormprev,a,b,d,t1,t2;
  PetscReal  maxstep,minlambda,alpha,lambda,lambdatemp,lambdaneg;
#if defined(PETSC_USE_COMPLEX)
  Scalar     cinitslope,clambda;
#endif
  int        ierr,count;
  SNES_EQ_LS *neP = (SNES_EQ_LS*)snes->data;
  Scalar     mone = -1.0,scale;
  PetscTruth change_y = PETSC_FALSE;

  PetscFunctionBegin;
  PLogEventBegin(SNES_LineSearch,snes,x,f,g);
  *flag   = 0;
  alpha   = neP->alpha;
  maxstep = neP->maxstep;
  steptol = neP->steptol;

  ierr = VecNorm(y,NORM_2,ynorm);CHKERRQ(ierr);
  if (*ynorm < snes->atol) {
    PLogInfo(snes,"SNESCubicLineSearch: Search direction and size are nearly 0\n");
    *gnorm = fnorm;
    ierr = VecCopy(x,y);CHKERRQ(ierr);
    ierr = VecCopy(f,g);CHKERRQ(ierr);
    goto theend1;
  }
  if (*ynorm > maxstep) {	/* Step too big, so scale back */
    scale = maxstep/(*ynorm);
#if defined(PETSC_USE_COMPLEX)
    PLogInfo(snes,"SNESCubicLineSearch: Scaling step by %g\n",PetscRealPart(scale));
#else
    PLogInfo(snes,"SNESCubicLineSearch: Scaling step by %g\n",scale);
#endif
    ierr = VecScale(&scale,y);CHKERRQ(ierr);
    *ynorm = maxstep;
  }
  minlambda = steptol/(*ynorm);
  ierr = MatMult(snes->jacobian,y,w);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
  ierr = VecDot(f,w,&cinitslope);CHKERRQ(ierr);
  initslope = PetscRealPart(cinitslope);
#else
  ierr = VecDot(f,w,&initslope);CHKERRQ(ierr);
#endif
  if (initslope > 0.0) initslope = -initslope;
  if (initslope == 0.0) initslope = -1.0;

  ierr = VecCopy(y,w);CHKERRQ(ierr);
  ierr = VecAYPX(&mone,x,w);CHKERRQ(ierr);
  ierr = SNESComputeFunction(snes,w,g);CHKERRQ(ierr);
  ierr = VecNorm(g,NORM_2,gnorm);CHKERRQ(ierr);
  if ((*gnorm)*(*gnorm)*0.5 <= fnorm*fnorm*0.5 + alpha*initslope) { /* Sufficient reduction */
    ierr = VecCopy(w,y);CHKERRQ(ierr);
    PLogInfo(snes,"SNESCubicLineSearch: Using full step\n");
    goto theend1;
  }

  /* Fit points with quadratic */
  lambda = 1.0;
  lambdatemp = -initslope/((*gnorm)*(*gnorm) - fnorm*fnorm - 2.0*initslope);
  lambdaprev = lambda;
  gnormprev = *gnorm;
  if (lambdatemp > .5*lambda)  lambdatemp = .5*lambda;
  if (lambdatemp <= .1*lambda) lambda = .1*lambda; 
  else                         lambda = lambdatemp;
  ierr   = VecCopy(x,w);CHKERRQ(ierr);
  lambdaneg = -lambda;
#if defined(PETSC_USE_COMPLEX)
  clambda = lambdaneg; ierr = VecAXPY(&clambda,y,w);CHKERRQ(ierr);
#else
  ierr = VecAXPY(&lambdaneg,y,w);CHKERRQ(ierr);
#endif
  ierr = SNESComputeFunction(snes,w,g);CHKERRQ(ierr);
  ierr = VecNorm(g,NORM_2,gnorm);CHKERRQ(ierr);
  if ((*gnorm)*(*gnorm)*0.5 <= fnorm*fnorm*0.5 + alpha*initslope) { /* sufficient reduction */
    ierr = VecCopy(w,y);CHKERRQ(ierr);
    PLogInfo(snes,"SNESCubicLineSearch: Quadratically determined step, lambda=%g\n",lambda);
    goto theend1;
  }

  /* Fit points with cubic */
  count = 1;
  while (1) {
    if (lambda <= minlambda) { /* bad luck; use full step */
      PLogInfo(snes,"SNESCubicLineSearch:Unable to find good step length! %d \n",count);
      PLogInfo(snes,"SNESCubicLineSearch:fnorm=%g, gnorm=%g, ynorm=%g, lambda=%g, initial slope=%g\n",
               fnorm,*gnorm,*ynorm,lambda,initslope);
      ierr = VecCopy(w,y);CHKERRQ(ierr);
      *flag = -1; break;
    }
    t1 = ((*gnorm)*(*gnorm) - fnorm*fnorm)*0.5 - lambda*initslope;
    t2 = (gnormprev*gnormprev  - fnorm*fnorm)*0.5 - lambdaprev*initslope;
    a  = (t1/(lambda*lambda) - t2/(lambdaprev*lambdaprev))/(lambda-lambdaprev);
    b  = (-lambdaprev*t1/(lambda*lambda) + lambda*t2/(lambdaprev*lambdaprev))/(lambda-lambdaprev);
    d  = b*b - 3*a*initslope;
    if (d < 0.0) d = 0.0;
    if (a == 0.0) {
      lambdatemp = -initslope/(2.0*b);
    } else {
      lambdatemp = (-b + sqrt(d))/(3.0*a);
    }
    lambdaprev = lambda;
    gnormprev  = *gnorm;
    if (lambdatemp > .5*lambda)  lambdatemp = .5*lambda;
    if (lambdatemp <= .1*lambda) lambda     = .1*lambda;
    else                         lambda     = lambdatemp;
    ierr = VecCopy(x,w);CHKERRQ(ierr);
    lambdaneg = -lambda;
#if defined(PETSC_USE_COMPLEX)
    clambda = lambdaneg;
    ierr = VecAXPY(&clambda,y,w);CHKERRQ(ierr);
#else
    ierr = VecAXPY(&lambdaneg,y,w);CHKERRQ(ierr);
#endif
    ierr = SNESComputeFunction(snes,w,g);CHKERRQ(ierr);
    ierr = VecNorm(g,NORM_2,gnorm);CHKERRQ(ierr);
    if ((*gnorm)*(*gnorm)*0.5 <= fnorm*fnorm*0.5 + alpha*initslope) { /* is reduction enough? */
      ierr = VecCopy(w,y);CHKERRQ(ierr);
      PLogInfo(snes,"SNESCubicLineSearch: Cubically determined step, lambda=%g\n",lambda);
      goto theend1;
    } else {
      PLogInfo(snes,"SNESCubicLineSearch: Cubic step no good, shrinking lambda,  lambda=%g\n",lambda);
    }
    count++;
  }
  theend1:
  /* Optional user-defined check for line search step validity */
  if (neP->CheckStep) {
    ierr = (*neP->CheckStep)(snes,neP->checkP,y,&change_y);CHKERRQ(ierr);
    if (change_y == PETSC_TRUE) { /* recompute the function if the step has changed */
      ierr = SNESComputeFunction(snes,y,g);CHKERRQ(ierr);
      ierr = VecNormBegin(y,NORM_2,ynorm);CHKERRQ(ierr);
      ierr = VecNormBegin(g,NORM_2,gnorm);CHKERRQ(ierr);
      ierr = VecNormEnd(y,NORM_2,ynorm);CHKERRQ(ierr);
      ierr = VecNormEnd(g,NORM_2,gnorm);CHKERRQ(ierr);
    }
  }
  PLogEventEnd(SNES_LineSearch,snes,x,f,g);
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESQuadraticLineSearch"
/*@C
   SNESQuadraticLineSearch - Performs a quadratic line search.

   Collective on SNES and Vec

   Input Parameters:
+  snes - the SNES context
.  lsctx - optional context for line search (not used here)
.  x - current iterate
.  f - residual evaluated at x
.  y - search direction (contains new iterate on output)
.  w - work vector
-  fnorm - 2-norm of f

   Output Parameters:
+  g - residual evaluated at new iterate y
.  y - new iterate (contains search direction on input)
.  gnorm - 2-norm of g
.  ynorm - 2-norm of search length
-  flag - 0 if line search succeeds; -1 on failure.

   Options Database Key:
.  -snes_eq_ls quadratic - Activates SNESQuadraticLineSearch()

   Notes:
   Use SNESSetLineSearch() to set this routine within the SNESEQLS method.  

   Level: advanced

.keywords: SNES, nonlinear, quadratic, line search

.seealso: SNESCubicLineSearch(), SNESNoLineSearch(), SNESSetLineSearch(), SNESNoLineSearchNoNorms()
@*/
int SNESQuadraticLineSearch(SNES snes,void *lsctx,Vec x,Vec f,Vec g,Vec y,Vec w,
                           PetscReal fnorm,PetscReal *ynorm,PetscReal *gnorm,int *flag)
{
  /* 
     Note that for line search purposes we work with with the related
     minimization problem:
        min  z(x):  R^n -> R,
     where z(x) = .5 * fnorm*fnorm,and fnorm = || f ||_2.
   */
  PetscReal  steptol,initslope,maxstep,minlambda,alpha,lambda,lambdatemp,lambdaneg;
#if defined(PETSC_USE_COMPLEX)
  Scalar     cinitslope,clambda;
#endif
  int        ierr,count;
  SNES_EQ_LS *neP = (SNES_EQ_LS*)snes->data;
  Scalar     mone = -1.0,scale;
  PetscTruth change_y = PETSC_FALSE;

  PetscFunctionBegin;
  PLogEventBegin(SNES_LineSearch,snes,x,f,g);
  *flag   = 0;
  alpha   = neP->alpha;
  maxstep = neP->maxstep;
  steptol = neP->steptol;

  ierr = VecNorm(y,NORM_2,ynorm);CHKERRQ(ierr);
  if (*ynorm < snes->atol) {
    PLogInfo(snes,"SNESQuadraticLineSearch: Search direction and size is 0\n");
    *gnorm = fnorm;
    ierr = VecCopy(x,y);CHKERRQ(ierr);
    ierr = VecCopy(f,g);CHKERRQ(ierr);
    goto theend2;
  }
  if (*ynorm > maxstep) {	/* Step too big, so scale back */
    scale = maxstep/(*ynorm);
    ierr = VecScale(&scale,y);CHKERRQ(ierr);
    *ynorm = maxstep;
  }
  minlambda = steptol/(*ynorm);
  ierr = MatMult(snes->jacobian,y,w);CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
  ierr = VecDot(f,w,&cinitslope);CHKERRQ(ierr);
  initslope = PetscRealPart(cinitslope);
#else
  ierr = VecDot(f,w,&initslope);CHKERRQ(ierr);
#endif
  if (initslope > 0.0) initslope = -initslope;
  if (initslope == 0.0) initslope = -1.0;

  ierr = VecCopy(y,w);CHKERRQ(ierr);
  ierr = VecAYPX(&mone,x,w);CHKERRQ(ierr);
  ierr = SNESComputeFunction(snes,w,g);CHKERRQ(ierr);
  ierr = VecNorm(g,NORM_2,gnorm);CHKERRQ(ierr);
  if ((*gnorm)*(*gnorm)*0.5 <= fnorm*fnorm*0.5 + alpha*initslope) { /* Sufficient reduction */
    ierr = VecCopy(w,y);CHKERRQ(ierr);
    PLogInfo(snes,"SNESQuadraticLineSearch: Using full step\n");
    goto theend2;
  }

  /* Fit points with quadratic */
  lambda = 1.0;
  count = 1;
  while (1) {
    if (lambda <= minlambda) { /* bad luck; use full step */
      PLogInfo(snes,"SNESQuadraticLineSearch:Unable to find good step length! %d \n",count);
      PLogInfo(snes,"SNESQuadraticLineSearch:fnorm=%g, gnorm=%g, ynorm=%g, lambda=%g, initial slope=%g\n",
               fnorm,*gnorm,*ynorm,lambda,initslope);
      ierr = VecCopy(w,y);CHKERRQ(ierr);
      *flag = -1; break;
    }
    lambdatemp = -initslope/((*gnorm)*(*gnorm) - fnorm*fnorm - 2.0*initslope);
    if (lambdatemp > .5*lambda)  lambdatemp = .5*lambda;
    if (lambdatemp <= .1*lambda) lambda     = .1*lambda; 
    else                         lambda     = lambdatemp;
    ierr = VecCopy(x,w);CHKERRQ(ierr);
    lambdaneg = -lambda;
#if defined(PETSC_USE_COMPLEX)
    clambda = lambdaneg; ierr = VecAXPY(&clambda,y,w);CHKERRQ(ierr);
#else
    ierr = VecAXPY(&lambdaneg,y,w);CHKERRQ(ierr);
#endif
    ierr = SNESComputeFunction(snes,w,g);CHKERRQ(ierr);
    ierr = VecNorm(g,NORM_2,gnorm);CHKERRQ(ierr);
    if ((*gnorm)*(*gnorm)*0.5 <= fnorm*fnorm*0.5 + alpha*initslope) { /* sufficient reduction */
      ierr = VecCopy(w,y);CHKERRQ(ierr);
      PLogInfo(snes,"SNESQuadraticLineSearch:Quadratically determined step, lambda=%g\n",lambda);
      break;
    }
    count++;
  }
  theend2:
  /* Optional user-defined check for line search step validity */
  if (neP->CheckStep) {
    ierr = (*neP->CheckStep)(snes,neP->checkP,y,&change_y);CHKERRQ(ierr);
    if (change_y == PETSC_TRUE) { /* recompute the function if the step has changed */
      ierr = SNESComputeFunction(snes,y,g);CHKERRQ(ierr);
      ierr = VecNormBegin(y,NORM_2,ynorm);CHKERRQ(ierr);
      ierr = VecNormBegin(g,NORM_2,gnorm);CHKERRQ(ierr);
      ierr = VecNormEnd(y,NORM_2,ynorm);CHKERRQ(ierr);
      ierr = VecNormEnd(g,NORM_2,gnorm);CHKERRQ(ierr);
    }
  }
  PLogEventEnd(SNES_LineSearch,snes,x,f,g);
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSetLineSearch"
/*@C
   SNESSetLineSearch - Sets the line search routine to be used
   by the method SNESEQLS.

   Input Parameters:
+  snes - nonlinear context obtained from SNESCreate()
.  lsctx - optional user-defined context for use by line search 
-  func - pointer to int function

   Collective on SNES

   Available Routines:
+  SNESCubicLineSearch() - default line search
.  SNESQuadraticLineSearch() - quadratic line search
.  SNESNoLineSearch() - the full Newton step (actually not a line search)
-  SNESNoLineSearchNoNorms() - the full Newton step (calculating no norms; faster in parallel)

    Options Database Keys:
+   -snes_eq_ls [cubic,quadratic,basic,basicnonorms] - Selects line search
.   -snes_eq_ls_alpha <alpha> - Sets alpha
.   -snes_eq_ls_maxstep <max> - Sets maxstep
-   -snes_eq_ls_steptol <steptol> - Sets steptol

   Calling sequence of func:
.vb
   func (SNES snes,void *lsctx,Vec x,Vec f,Vec g,Vec y,Vec w,
         PetscReal fnorm,PetscReal *ynorm,PetscReal *gnorm,*flag)
.ve

    Input parameters for func:
+   snes - nonlinear context
.   lsctx - optional user-defined context for line search
.   x - current iterate
.   f - residual evaluated at x
.   y - search direction (contains new iterate on output)
.   w - work vector
-   fnorm - 2-norm of f

    Output parameters for func:
+   g - residual evaluated at new iterate y
.   y - new iterate (contains search direction on input)
.   gnorm - 2-norm of g
.   ynorm - 2-norm of search length
-   flag - set to 0 if the line search succeeds; a nonzero integer 
           on failure.

    Level: advanced

.keywords: SNES, nonlinear, set, line search, routine

.seealso: SNESCubicLineSearch(), SNESQuadraticLineSearch(), SNESNoLineSearch(), SNESNoLineSearchNoNorms(), SNESSetLineSearchCheck(), SNESSetLineSearchParams(), SNESGetLineSearchParams()
@*/
int SNESSetLineSearch(SNES snes,int (*func)(SNES,void*,Vec,Vec,Vec,Vec,Vec,PetscReal,PetscReal*,PetscReal*,int*),void *lsctx)
{
  int ierr,(*f)(SNES,int (*)(SNES,void*,Vec,Vec,Vec,Vec,Vec,PetscReal,PetscReal*,PetscReal*,int*),void*);

  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)snes,"SNESSetLineSearch_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(snes,func,lsctx);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSetLineSearch_LS"
int SNESSetLineSearch_LS(SNES snes,int (*func)(SNES,void*,Vec,Vec,Vec,Vec,Vec,
                         double,double*,double*,int*),void *lsctx)
{
  PetscFunctionBegin;
  ((SNES_EQ_LS *)(snes->data))->LineSearch = func;
  ((SNES_EQ_LS *)(snes->data))->lsP        = lsctx;
  PetscFunctionReturn(0);
}
EXTERN_C_END
/* -------------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSetLineSearchCheck"
/*@C
   SNESSetLineSearchCheck - Sets a routine to check the validity of new iterate computed
   by the line search routine in the Newton-based method SNESEQLS.

   Input Parameters:
+  snes - nonlinear context obtained from SNESCreate()
.  func - pointer to int function
-  checkctx - optional user-defined context for use by step checking routine 

   Collective on SNES

   Calling sequence of func:
.vb
   int func (SNES snes, void *checkctx, Vec x, PetscTruth *flag)
.ve
   where func returns an error code of 0 on success and a nonzero
   on failure.

   Input parameters for func:
+  snes - nonlinear context
.  checkctx - optional user-defined context for use by step checking routine 
-  x - current candidate iterate

   Output parameters for func:
+  x - current iterate (possibly modified)
-  flag - flag indicating whether x has been modified (either
           PETSC_TRUE of PETSC_FALSE)

   Level: advanced

   Notes:
   SNESNoLineSearch() and SNESNoLineSearchNoNorms() accept the new
   iterate computed by the line search checking routine.  In particular,
   these routines (1) compute a candidate iterate u_{i+1}, (2) pass control 
   to the checking routine, and then (3) compute the corresponding nonlinear
   function f(u_{i+1}) with the (possibly altered) iterate u_{i+1}.

   SNESQuadraticLineSearch() and SNESCubicLineSearch() also accept the
   new iterate computed by the line search checking routine.  In particular,
   these routines (1) compute a candidate iterate u_{i+1} as well as a
   candidate nonlinear function f(u_{i+1}), (2) pass control to the checking 
   routine, and then (3) force a re-evaluation of f(u_{i+1}) if any changes 
   were made to the candidate iterate in the checking routine (as indicated 
   by flag=PETSC_TRUE).  The overhead of this function re-evaluation can be
   very costly, so use this feature with caution!

.keywords: SNES, nonlinear, set, line search check, step check, routine

.seealso: SNESSetLineSearch()
@*/
int SNESSetLineSearchCheck(SNES snes,int (*func)(SNES,void*,Vec,PetscTruth*),void *checkctx)
{
  int ierr,(*f)(SNES,int (*)(SNES,void*,Vec,PetscTruth*),void*);

  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)snes,"SNESSetLineSearchCheck_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(snes,func,checkctx);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSetLineSearchCheck_LS"
int SNESSetLineSearchCheck_LS(SNES snes,int (*func)(SNES,void*,Vec,PetscTruth*),void *checkctx)
{
  PetscFunctionBegin;
  ((SNES_EQ_LS *)(snes->data))->CheckStep = func;
  ((SNES_EQ_LS *)(snes->data))->checkP    = checkctx;
  PetscFunctionReturn(0);
}
EXTERN_C_END
/* -------------------------------------------------------------------------- */
/*
   SNESPrintHelp_EQ_LS - Prints all options for the SNESEQLS method.

   Input Parameter:
.  snes - the SNES context

   Application Interface Routine: SNESPrintHelp()
*/
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESPrintHelp_EQ_LS"
static int SNESPrintHelp_EQ_LS(SNES snes,char *p)
{
  SNES_EQ_LS *ls = (SNES_EQ_LS *)snes->data;
  int     ierr;

  PetscFunctionBegin;
  ierr = (*PetscHelpPrintf)(snes->comm," method SNESEQLS (ls) for systems of nonlinear equations:\n");CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_eq_ls [cubic,quadratic,basic,basicnonorms,...]\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_eq_ls_alpha <alpha> (default %g)\n",p,ls->alpha);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_eq_ls_maxstep <max> (default %g)\n",p,ls->maxstep);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_eq_ls_steptol <tol> (default %g)\n",p,ls->steptol);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
/*
   SNESView_EQ_LS - Prints info from the SNESEQLS data structure.

   Input Parameters:
.  SNES - the SNES context
.  viewer - visualization context

   Application Interface Routine: SNESView()
*/
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESView_EQ_LS"
static int SNESView_EQ_LS(SNES snes,Viewer viewer)
{
  SNES_EQ_LS *ls = (SNES_EQ_LS *)snes->data;
  char       *cstr;
  int        ierr;
  PetscTruth isascii;

  PetscFunctionBegin;
  ierr = PetscTypeCompare((PetscObject)viewer,ASCII_VIEWER,&isascii);CHKERRQ(ierr);
  if (isascii) {
    if (ls->LineSearch == SNESNoLineSearch)             cstr = "SNESNoLineSearch";
    else if (ls->LineSearch == SNESQuadraticLineSearch) cstr = "SNESQuadraticLineSearch";
    else if (ls->LineSearch == SNESCubicLineSearch)     cstr = "SNESCubicLineSearch";
    else                                                cstr = "unknown";
    ierr = ViewerASCIIPrintf(viewer,"  line search variant: %s\n",cstr);CHKERRQ(ierr);
    ierr = ViewerASCIIPrintf(viewer,"  alpha=%g, maxstep=%g, steptol=%g\n",ls->alpha,ls->maxstep,ls->steptol);CHKERRQ(ierr);
  } else {
    SETERRQ1(1,1,"Viewer type %s not supported for SNES EQ LS",((PetscObject)viewer)->type_name);
  }
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
/*
   SNESSetFromOptions_EQ_LS - Sets various parameters for the SNESEQLS method.

   Input Parameter:
.  snes - the SNES context

   Application Interface Routine: SNESSetFromOptions()
*/
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESSetFromOptions_EQ_LS"
static int SNESSetFromOptions_EQ_LS(SNES snes)
{
  SNES_EQ_LS *ls = (SNES_EQ_LS *)snes->data;
  char       ver[16];
  double     tmp;
  int        ierr;
  PetscTruth flg;

  PetscFunctionBegin;
  ierr = OptionsGetDouble(snes->prefix,"-snes_eq_ls_alpha",&tmp,&flg);CHKERRQ(ierr);
  if (flg) {
    ls->alpha = tmp;
  }
  ierr = OptionsGetDouble(snes->prefix,"-snes_eq_ls_maxstep",&tmp,&flg);CHKERRQ(ierr);
  if (flg) {
    ls->maxstep = tmp;
  }
  ierr = OptionsGetDouble(snes->prefix,"-snes_eq_ls_steptol",&tmp,&flg);CHKERRQ(ierr);
  if (flg) {
    ls->steptol = tmp;
  }
  ierr = OptionsGetString(snes->prefix,"-snes_eq_ls",ver,16,&flg);CHKERRQ(ierr);
  if (flg) {
    PetscTruth isbasic,isnonorms,isquad,iscubic;

    ierr = PetscStrcmp(ver,"basic",&isbasic);CHKERRQ(ierr);
    ierr = PetscStrcmp(ver,"basicnonorms",&isnonorms);CHKERRQ(ierr);
    ierr = PetscStrcmp(ver,"quadratic",&isquad);CHKERRQ(ierr);
    ierr = PetscStrcmp(ver,"cubic",&iscubic);CHKERRQ(ierr);

    if (isbasic) {
      ierr = SNESSetLineSearch(snes,SNESNoLineSearch,PETSC_NULL);CHKERRQ(ierr);
    } else if (isnonorms) {
      ierr = SNESSetLineSearch(snes,SNESNoLineSearchNoNorms,PETSC_NULL);CHKERRQ(ierr);
    } else if (isquad) {
      ierr = SNESSetLineSearch(snes,SNESQuadraticLineSearch,PETSC_NULL);CHKERRQ(ierr);
    } else if (iscubic) {
      ierr = SNESSetLineSearch(snes,SNESCubicLineSearch,PETSC_NULL);CHKERRQ(ierr);
    }
    else {SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Unknown line search");}
  }
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------- */
/*
   SNESCreate_EQ_LS - Creates a nonlinear solver context for the SNESEQLS method,
   SNES_EQ_LS, and sets this as the private data within the generic nonlinear solver
   context, SNES, that was created within SNESCreate().


   Input Parameter:
.  snes - the SNES context

   Application Interface Routine: SNESCreate()
 */
EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"SNESCreate_EQ_LS"
int SNESCreate_EQ_LS(SNES snes)
{
  int        ierr;
  SNES_EQ_LS *neP;

  PetscFunctionBegin;
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }

  snes->setup		= SNESSetUp_EQ_LS;
  snes->solve		= SNESSolve_EQ_LS;
  snes->destroy		= SNESDestroy_EQ_LS;
  snes->converged	= SNESConverged_EQ_LS;
  snes->printhelp       = SNESPrintHelp_EQ_LS;
  snes->setfromoptions  = SNESSetFromOptions_EQ_LS;
  snes->view            = SNESView_EQ_LS;
  snes->nwork           = 0;

  neP			= PetscNew(SNES_EQ_LS);CHKPTRQ(neP);
  PLogObjectMemory(snes,sizeof(SNES_EQ_LS));
  snes->data    	= (void*)neP;
  neP->alpha		= 1.e-4;
  neP->maxstep		= 1.e8;
  neP->steptol		= 1.e-12;
  neP->LineSearch       = SNESCubicLineSearch;
  neP->lsP              = PETSC_NULL;
  neP->CheckStep        = PETSC_NULL;
  neP->checkP           = PETSC_NULL;

  ierr = PetscObjectComposeFunctionDynamic((PetscObject)snes,"SNESSetLineSearch_C","SNESSetLineSearch_LS",
                    (void*)SNESSetLineSearch_LS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)snes,"SNESSetLineSearchCheck_C","SNESSetLineSearchCheck_LS",
                    (void*)SNESSetLineSearchCheck_LS);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
EXTERN_C_END



