/*$Id: mfregis.c,v 1.8 2000/04/01 04:24:14 bsmith Exp bsmith $*/

#include "src/snes/mf/snesmfj.h"   /*I  "snes.h"   I*/

EXTERN_C_BEGIN
extern int MatSNESMFCreate_Default(MatSNESMFCtx);
extern int MatSNESMFCreate_WP(MatSNESMFCtx);
EXTERN_C_END

#undef __FUNC__  
#define  __FUNC__ /*<a name=""></a>*/"MatSNESMFRegisterAll"
/*@C
  MatSNESMFRegisterAll - Registers all of the compute-h in the MatSNESMF package.

  Not Collective

  Level: developer

.keywords: MatSNESMF, register, all

.seealso:  MatSNESMFRegisterDestroy(), MatSNESMFRegisterDynamic), MatSNESMFCreate(), 
           MatSNESMFSetType()
@*/
int MatSNESMFRegisterAll(char *path)
{
  int ierr;

  PetscFunctionBegin;
  MatSNESMFRegisterAllCalled = PETSC_TRUE;

  ierr = MatSNESMFRegisterDynamic(MATSNESMF_DEFAULT,path,"MatSNESMFCreate_Default",MatSNESMFCreate_Default);CHKERRQ(ierr);
  ierr = MatSNESMFRegisterDynamic(MATSNESMF_WP,path,"MatSNESMFCreate_WP",MatSNESMFCreate_WP);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

