/*$Id: rpath.c,v 1.24 2000/01/11 20:59:28 bsmith Exp bsmith $*/

#include "petsc.h"
#include "sys.h"
#if defined(PETSC_HAVE_PWD_H)
#include <pwd.h>
#endif
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#if defined(PETSC_HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(PETSC_HAVE_STDLIB_H)
#include <stdlib.h>
#endif
#if !defined(PARCH_win32)
#include <sys/utsname.h>
#endif
#if defined(PARCH_win32)
#include <windows.h>
#include <io.h>
#include <direct.h>
#endif
#if defined (PARCH_win32_gnu)
#include <windows.h>
#endif
#if defined(PETSC_HAVE_SYS_SYSTEMINFO_H)
#include <sys/systeminfo.h>
#endif
#include "petscfix.h"

#undef __FUNC__  
#define  __FUNC__ /*<a name=""></a>*/"PetscGetRelativePath"
/*@C
   PetscGetRelativePath - Given a filename, returns the relative path (removes
   all directory specifiers).

   Not Collective

   Input parameters:
+  fullpath  - full pathname
.  path      - pointer to buffer to hold relative pathname
-  flen     - size of path

   Level: developer

.keywords: system, get, relative, path

.seealso: PetscGetFullPath()
@*/
int PetscGetRelativePath(const char fullpath[],char path[],int flen)
{
  char  *p;
  int   ierr;

  PetscFunctionBegin;
  /* Find string after last / or entire string if no / */
  ierr = PetscStrrchr(fullpath,'/',&p);CHKERRQ(ierr);
  ierr = PetscStrncpy(path,p,flen);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
