#include <Teuchos_ScalarTraits.hpp>
#include <Teuchos_RCP.hpp>
#include <Teuchos_GlobalMPISession.hpp>
#include <Teuchos_oblackholestream.hpp>
#include <Teuchos_Tuple.hpp>
#include <Teuchos_VerboseObject.hpp>

#include <Tpetra_DefaultPlatform.hpp>
#include <Tpetra_Map.hpp>
#include <Tpetra_MultiVector.hpp>
#include <Tpetra_CrsMatrix.hpp>

#include "Amesos2.hpp"
#include "Amesos2_Version.hpp"
#include "Amesos2_KLU2.hpp"

#include "petscksp.h"
#include "Tpetra_PETScAIJMatrix.hpp"
#include "Tpetra_Vector.hpp"

  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::ArrayView;

  typedef Tpetra::PETScAIJMatrix<>             PETScAIJMatrix;
  typedef PETScAIJMatrix::scalar_type          Scalar;
  typedef PETScAIJMatrix::local_ordinal_type   LO;
  typedef PETScAIJMatrix::global_ordinal_type  GO;
  typedef PETScAIJMatrix::node_type            Node;

  typedef Tpetra::CrsMatrix<Scalar,LO,GO>      CrsMatrix;
  typedef Tpetra::Vector<Scalar,LO,GO>         Vector;
  typedef Tpetra::Map<LO,GO>                   Map;
  typedef Tpetra::Operator<Scalar,LO,GO>       OP;
  typedef Tpetra::MultiVector<Scalar,LO,GO>    MV;
  typedef Tpetra::Vector<Scalar,LO,GO>         Vector;
  typedef Amesos2::Solver<CrsMatrix,MV>        Solver;

/* 
   This example demonstrates how to apply a Trilinos preconditioner to a PETSc
   linear system.

   For information on configuring and building Trilinos with the PETSc aij
   interface enabled, please see EpetraExt's doxygen documentation at
   http://trilinos.sandia.gov/packages/epetraext, development version
   or release 9.0 or later.

   The PETSc matrix is a 2D 5-point Laplace operator stored in AIJ format.
   This matrix is wrapped as an Epetra_PETScAIJMatrix, and an ML AMG
   preconditioner is created for it.  The associated linear system is solved twice,
   the first time using AztecOO's preconditioned CG, the second time using PETSc's.

   To invoke this example, use something like:

       mpirun -np 5 ./TrilinosCouplings_petsc.exe -m 150 -n 150 -petsc_smoother -ksp_monitor_true_residual
*/

static char help[] = "Demonstrates how to solve a PETSc linear system with KSP\
and a Trilinos AMG preconditioner.  In particular, it shows how to wrap a PETSc\
AIJ matrix as a Tpetra matrix, create the AMG preconditioner, and wrap it as a\
shell preconditioner for a PETSc Krylov method.\
Input parameters include:\n\
  -random_exact_sol : use a random exact solution vector\n\
  -view_exact_sol   : write exact solution vector to stdout\n\
  -m <mesh_x>       : number of mesh points in x-direction\n\
  -n <mesh_n>       : number of mesh points in y-direction\n\n";

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **args)
{
  Vec            x,b,u;  /* approx solution, RHS, exact solution */
  Mat            A;        /* linear system matrix */
  PetscRandom    rctx;     /* random number generator context */
  PetscReal      norm;     /* norm of solution error */
  PetscInt       i,j,Ii,J,Istart,Iend;
  PetscInt       m = 50,n = 50; /* #mesh points in x & y directions, resp. */
  PetscErrorCode ierr;
  PetscBool     flg;
  PetscScalar    v,one = 1.0;
  PetscInt rank=0;
  MPI_Comm comm;

  PetscInitialize(&argc,&args,(char *)0,help);
  ierr = PetscOptionsGetInt(PETSC_NULL,"-m",&m,PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(PETSC_NULL,"-n",&n,PETSC_NULL);CHKERRQ(ierr);

  ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
  ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,m*n,m*n);CHKERRQ(ierr);
  ierr = MatSetType(A, MATAIJ);CHKERRQ(ierr);
  ierr = MatSetFromOptions(A);CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(A,5,PETSC_NULL,5,PETSC_NULL);CHKERRQ(ierr);
  ierr = MatSetUp(A);CHKERRQ(ierr);
  PetscObjectGetComm( (PetscObject)A, &comm);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
  if (!rank) printf("Matrix has %d (%dx%d) rows\n",m*n,m,n);

  ierr = MatGetOwnershipRange(A,&Istart,&Iend);CHKERRQ(ierr);

  for (Ii=Istart; Ii<Iend; Ii++) {
    v = -1.0; i = Ii/n; j = Ii - i*n;
    if (i>0)   {J = Ii - n; ierr = MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES);CHKERRQ(ierr);}
    if (i<m-1) {J = Ii + n; ierr = MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES);CHKERRQ(ierr);}
    if (j>0)   {J = Ii - 1; ierr = MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES);CHKERRQ(ierr);}
    if (j<n-1) {J = Ii + 1; ierr = MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES);CHKERRQ(ierr);}
    v = 4.0; ierr = MatSetValues(A,1,&Ii,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
  }

  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  RCP<CrsMatrix> epA = xSDKTrilinos::deepCopyPETScAIJMatrixToTpetraCrsMatrix<Scalar,LO,GO,Node>(A);

  ierr = VecCreate(PETSC_COMM_WORLD,&u);CHKERRQ(ierr);
  ierr = VecSetSizes(u,PETSC_DECIDE,m*n);CHKERRQ(ierr);
  ierr = VecSetFromOptions(u);CHKERRQ(ierr);
  ierr = VecDuplicate(u,&b);CHKERRQ(ierr);

  ierr = VecDuplicate(b,&x);CHKERRQ(ierr);

  ierr = PetscOptionsHasName(PETSC_NULL,"-random_exact_sol",&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscRandomCreate(PETSC_COMM_WORLD,&rctx);CHKERRQ(ierr);
    ierr = PetscRandomSetFromOptions(rctx);CHKERRQ(ierr);
    ierr = VecSetRandom(u,rctx);CHKERRQ(ierr);
    ierr = PetscRandomDestroy(&rctx);CHKERRQ(ierr);
  } else {
    ierr = VecSet(u,one);CHKERRQ(ierr);
  }
  ierr = MatMult(A,u,b);CHKERRQ(ierr);
  ierr = VecNorm(b,NORM_2,&norm);CHKERRQ(ierr);
  if (rank==0) printf("||b|| = %f\n",norm);

  /* Copy the PETSc vectors to Tpetra vectors. */
  PetscScalar *vals;
  ierr = VecGetArray(u,&vals);CHKERRQ(ierr);
  PetscInt length;
  ierr = VecGetLocalSize(u,&length);CHKERRQ(ierr);
  PetscScalar* valscopy = (PetscScalar*) malloc(length*sizeof(PetscScalar));
  memcpy(valscopy,vals,length*sizeof(PetscScalar));
  ierr = VecRestoreArray(u,&vals);CHKERRQ(ierr);
  ArrayView<PetscScalar> epuView(valscopy,length);
  RCP<Vector> epu = rcp(new Vector(epA->getRowMap(),epuView));
  RCP<Vector> epb = rcp(new Vector(epA->getRowMap()));
  epA->apply(*epu, *epb);

  /* Check norms of the Tpetra and PETSc vectors. */
  norm = epu->norm2();
  if (rank == 0) printf("||tpetra u||_2 = %f\n",norm);
  ierr = VecNorm(u,NORM_2,&norm);CHKERRQ(ierr);
  if (rank == 0) printf("||petsc u||_2  = %f\n",norm);
  norm = epb->norm2();
  if (rank == 0) printf("||tpetra b||_2 = %f\n",norm);
  ierr = VecNorm(b,NORM_2,&norm);CHKERRQ(ierr);
  if (rank == 0) printf("||petsc b||_2  = %f\n",norm);

  /* Create an Amesos2 linear solver */
  RCP<Solver> solver = Amesos2::create<CrsMatrix,MV>("KLU2", epA, epu, epb);

  /* Perform a linear solve with Amesos2 */
  solver->symbolicFactorization().numericFactorization().solve();
} /*main*/

/* ***************************************************************** */
