/*$Id: smg.c,v 1.19 2000/01/11 21:01:44 bsmith Exp bsmith $*/
/*
     Additive Multigrid V Cycle routine    
*/
#include "src/sles/pc/impls/mg/mgimpl.h"

/*
       MGACycle_Private - Given an MG structure created with MGCreate() runs 
                  one cycle down through the levels and back up. Applys
                  the smoothers in an additive manner.

    Iput Parameters:
.   mg - structure created with  MGCreate().

*/
#undef __FUNC__  
#define  __FUNC__ /*<a name=""></a>*/"MGACycle_Private"
int MGACycle_Private(MG *mg)
{
  int    i,l = mg[0]->levels,its,ierr;
  Scalar zero = 0.0;

  PetscFunctionBegin;
  /* compute RHS on each level */
  for (i=l-1; i>0; i--) {
    ierr = MatRestrict(mg[i]->restrct,mg[i]->b,mg[i-1]->b);CHKERRQ(ierr);
  }
  /* solve seperately on each level */
  for (i=0; i<l; i++) {
    ierr = VecSet(&zero,mg[i]->x);CHKERRQ(ierr); 
    ierr = SLESSolve(mg[i]->smoothd,mg[i]->b,mg[i]->x,&its);CHKERRQ(ierr); 
  }
  for (i=1; i<l; i++) {  
    ierr = MatInterpolateAdd(mg[i]->interpolate,mg[i-1]->x,mg[i]->x,mg[i]->x);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
