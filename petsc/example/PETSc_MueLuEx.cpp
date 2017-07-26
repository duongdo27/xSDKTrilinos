// @HEADER
// ***********************************************************************
//
//       xSDKTrilinos: Extreme-scale Software Development Kit Package
//                 Copyright (2016) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Alicia Klinvex    (amklinv@sandia.gov)
//                    James Willenbring (jmwille@sandia.gov)
//                    Michael Heroux    (maherou@sandia.gov)         
//
// ***********************************************************************
// @HEADER

/*
   This example demonstrates how to apply a Trilinos preconditioner to a PETSc
   linear system.

   The PETSc matrix is a 2D 5-point Laplace operator stored in AIJ format.
   This matrix is wrapped as an Epetra_PETScAIJMatrix, and a MueLu AMG
   preconditioner is created for it.  The associated linear system is solved twice,
   the first time using Belos's preconditioned CG, the second time using PETSc's.

   To invoke this example, use something like:

       mpirun -np 5 ./PETSc_MueLu_example.exe -mx 150 -my 150 -petsc_smoother -ksp_monitor_true_residual
*/

#include "MueLu_CreateTpetraPreconditioner.hpp"
#include "petscksp.h"
#include "Tpetra_ConfigDefs.hpp"
#include "Tpetra_PETScAIJMatrix.hpp"
#include "Tpetra_Vector.hpp"
#include "Tpetra_Map.hpp"
#include "BelosPseudoBlockCGSolMgr.hpp"
#include "BelosTpetraAdapter.hpp"

  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::ArrayView;

  typedef Tpetra::PETScAIJMatrix<>                                         PETScAIJMatrix;
  typedef PETScAIJMatrix::scalar_type                                      Scalar;
  typedef PETScAIJMatrix::local_ordinal_type                               LO;
  typedef PETScAIJMatrix::global_ordinal_type                              GO;
  typedef PETScAIJMatrix::node_type                                        Node;
  typedef Tpetra::CrsMatrix<Scalar,LO,GO,Node>                             CrsMatrix;
  typedef Tpetra::Vector<Scalar,LO,GO,Node>                                Vector;
  typedef MueLu::TpetraOperator<Scalar,LO,GO,Node>                         MueLuOp;
  typedef Tpetra::Map<LO,GO,Node>                                          Map;
  typedef Tpetra::Operator<Scalar,LO,GO,Node>                              OP;
  typedef Tpetra::MultiVector<Scalar,LO,GO,Node>                           MV;
  typedef Tpetra::Vector<Scalar,LO,GO,Node>                                Vector;
  typedef Belos::LinearProblem<Scalar,MV,OP>                               LP;
  typedef Belos::PseudoBlockCGSolMgr<Scalar,MV,OP>                         SolMgr;

PetscErrorCode ShellApplyML(PC pc,Vec x,Vec y);

int main(int argc,char **args)
{
  Vec                   x,b;            /* approx solution, RHS  */
  Mat                   A;              /* linear system matrix */
  KSP                   ksp;            /* linear solver context */
  PC                    pc;
  PetscRandom           rctx;           /* random number generator context */
  PetscInt              i,j,Ii,J,Istart,Iend;
  PetscInt              m = 50,n = 50;  /* #mesh points in x & y directions, resp. */
  PetscErrorCode        ierr;
  PetscScalar           v;
  PetscInt              rank=0;
  MPI_Comm              comm;
  PetscViewerAndFormat* vf;

  //
  // Initialize PETSc
  //
  PetscInitialize(&argc,&args,NULL,NULL);

  //
  // Create the PETSc matrix
  //
  ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
  ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,m*n,m*n);CHKERRQ(ierr);
  ierr = MatSetType(A, MATAIJ);CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(A,5,NULL,5,NULL);CHKERRQ(ierr);
  ierr = MatSetUp(A);CHKERRQ(ierr);
  ierr = PetscObjectGetComm( (PetscObject)A, &comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
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

  //
  // Create the random solution vector and corresponding RHS
  //
  ierr = VecCreate(PETSC_COMM_WORLD,&x);CHKERRQ(ierr);
  ierr = VecSetSizes(x,PETSC_DECIDE,m*n);CHKERRQ(ierr);
  ierr = VecSetFromOptions(x);CHKERRQ(ierr);
  ierr = VecDuplicate(x,&b);CHKERRQ(ierr);
  ierr = PetscRandomCreate(PETSC_COMM_WORLD,&rctx);CHKERRQ(ierr);
  ierr = PetscRandomSetFromOptions(rctx);CHKERRQ(ierr);
  ierr = VecSetRandom(x,rctx);CHKERRQ(ierr);
  ierr = PetscRandomDestroy(&rctx);CHKERRQ(ierr);
  ierr = MatMult(A,x,b);CHKERRQ(ierr);

  //
  // Copy the PETSc matrix to a Tpetra CrsMatrix
  //
  RCP<CrsMatrix> tpetraA = xSDKTrilinos::deepCopyPETScAIJMatrixToTpetraCrsMatrix<Scalar,LO,GO,Node>(A);

  //
  // Copy the PETSc vectors to Tpetra vectors.
  //
  RCP<Vector> tpetraX = xSDKTrilinos::deepCopyPETScVecToTpetraVector<Scalar,LO,GO,Node>(x);  
  RCP<Vector> tpetraB = xSDKTrilinos::deepCopyPETScVecToTpetraVector<Scalar,LO,GO,Node>(b);

  //
  // Set initial guess to 0
  //
  tpetraX->putScalar(0);

  //
  // Create the MueLu AMG preconditioner.
  //

  // Parameter list that holds options for AMG preconditioner. 
  Teuchos::ParameterList mlList;
  mlList.set("parameterlist: syntax", "ml");
  // Specify how much information ML prints to screen.
  // 0 is the minimum (no output), 10 is the maximum.
  mlList.set("ML output",0);
  mlList.set("smoother: type (level 0)","symmetric Gauss-Seidel");

  // how many fine grid pre- or post-smoothing sweeps to do
  mlList.set("smoother: sweeps (level 0)",1);

  RCP<MueLuOp> Prec = MueLu::CreateTpetraPreconditioner(Teuchos::rcp_dynamic_cast<OP>(tpetraA), mlList);

  //
  // Create parameter list for the Belos solver manager
  //
  RCP<Teuchos::ParameterList> cgPL = rcp(new Teuchos::ParameterList());
  cgPL->set("Maximum Iterations", 200);
  cgPL->set("Verbosity", Belos::StatusTestDetails + Belos::FinalSummary);
  cgPL->set("Convergence Tolerance", 1e-8);

  //
  // Construct a preconditioned linear problem 
  //
  RCP<LP> Problem = rcp(new LP(tpetraA, tpetraX, tpetraB));
  Problem->setLeftPrec(Prec);
  Problem->setProblem();

  //
  // Create a Belos iterative solver manager and perform the linear solve
  //
  SolMgr solver(Problem,cgPL);
  solver.solve();

  //
  // Check the residual
  //
  Vector R( tpetraA->getRowMap() );
  tpetraA->apply(*tpetraX,R);
  R.update(1,*tpetraB,-1);
  std::vector<double> normR(1), normB(1);
  R.norm2(normR);
  tpetraB->norm2(normB);
  if(rank == 0) std::cout << "Belos relative residual: " << normR[0] / normB[0] << std::endl;
  if(normR[0] / normB[0] > 1e-8)
    return EXIT_FAILURE;
  
  //
  // Check the error
  //
  RCP<Vector> trueX = xSDKTrilinos::deepCopyPETScVecToTpetraVector<Scalar,LO,GO,Node>(x);
  Vector errorVec( tpetraA->getRowMap() );
  errorVec.update(1,*tpetraX,-1,*trueX,0);
  std::vector<double> normErrorVec(1);
  errorVec.norm2(normErrorVec);
  if(rank == 0) std::cout << "Belos error: " << normErrorVec[0] << std::endl;
  if(normErrorVec[0] > 1e-6)
    return EXIT_FAILURE;

  //
  // Create a PETSc KSP linear solver
  //
  ierr = KSPCreate(PETSC_COMM_WORLD,&ksp);CHKERRQ(ierr);
  ierr = KSPSetOperators(ksp,A,A);CHKERRQ(ierr);
  ierr = KSPSetTolerances(ksp,1e-8,1.e-50,PETSC_DEFAULT,
                          PETSC_DEFAULT);CHKERRQ(ierr);
  ierr = KSPSetType(ksp,KSPCG);CHKERRQ(ierr);

  //
  // Wrap the ML preconditioner as a PETSc shell preconditioner.
  //
  ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
  ierr = PCSetType(pc,PCSHELL);CHKERRQ(ierr);
  ierr = PCShellSetApply(pc,ShellApplyML);CHKERRQ(ierr);
  ierr = PCShellSetContext(pc,(void*)Prec.get());CHKERRQ(ierr);
  ierr = PCShellSetName(pc,"MueLu AMG");CHKERRQ(ierr); 

  //
  // Solve the linear system using PETSc 
  //
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);
  ierr = PetscViewerAndFormatCreate(PETSC_VIEWER_STDOUT_WORLD,PETSC_VIEWER_DEFAULT,&vf);CHKERRCONTINUE(ierr);
  ierr = KSPMonitorSet(ksp, (PetscErrorCode (*)(KSP,PetscInt,PetscReal,void*))KSPMonitorDefault, vf, (PetscErrorCode (*)(void**))PetscViewerAndFormatDestroy);CHKERRCONTINUE(ierr);
  ierr = KSPSolve(ksp,b,x);CHKERRQ(ierr);

  //
  // Check the residual
  //
  RCP<Vector> kspX = xSDKTrilinos::deepCopyPETScVecToTpetraVector<Scalar,LO,GO,Node>(x);
  tpetraA->apply(*kspX,R);
  R.update(1,*tpetraB,-1);
  R.norm2(normR);
  tpetraB->norm2(normB);
  if(rank == 0) std::cout << "KSP relative residual: " << normR[0] / normB[0] << std::endl;
  if(normR[0] / normB[0] > 1e-8)
    return EXIT_FAILURE;

  //
  // Free PETSc memory
  //
  ierr = KSPDestroy(&ksp);CHKERRQ(ierr);
  ierr = VecDestroy(&x);CHKERRQ(ierr);
  ierr = VecDestroy(&b);CHKERRQ(ierr);  
  ierr = MatDestroy(&A);CHKERRQ(ierr);

  //
  // Terminate PETSc
  //
  ierr = PetscFinalize();CHKERRQ(ierr);
  return EXIT_SUCCESS;
} /*main*/

/* ***************************************************************** */

PetscErrorCode ShellApplyML(PC pc,Vec x,Vec y)
{
  PetscErrorCode  ierr;
  MueLuOp *mlp = 0;
  void* ctx;

  ierr = PCShellGetContext(pc,&ctx); CHKERRQ(ierr);  
  mlp = (MueLuOp*)ctx;

  /* Wrap x and y as Tpetra_Vectors. */
  PetscInt length;
  ierr = VecGetLocalSize(x,&length);CHKERRQ(ierr);
  const PetscScalar *xvals;
  PetscScalar *yvals;

  ierr = VecGetArrayRead(x,&xvals);CHKERRQ(ierr);
  ierr = VecGetArray(y,&yvals);CHKERRQ(ierr);

  ArrayView<const PetscScalar> xView(xvals,length);
  ArrayView<PetscScalar> yView(yvals,length);

  Vector tpetraX(mlp->getDomainMap(),xView);
  Vector tpetraY(mlp->getDomainMap(),yView);

  /* Apply ML. */
  mlp->apply(tpetraX,tpetraY);

  /* Rip the data out of tpetra vectors */
  Teuchos::ArrayRCP<const Scalar> tpetraXData = tpetraX.getData();
  Teuchos::ArrayRCP<const Scalar> tpetraYData = tpetraY.getData();

  for(int i=0; i< tpetraYData.size(); i++)
  {
    yvals[i] = tpetraYData[i];
  }
  
  /* Clean up and return. */
  ierr = VecRestoreArrayRead(x,&xvals);CHKERRQ(ierr);
  ierr = VecRestoreArray(y,&yvals);CHKERRQ(ierr);

  return 0;
} /*ShellApplyML*/

/*--- Trilinos example metadata
Categories: iterative solvers, preconditioners, external interfaces
Topics: Solve a linear system
Prerequisites: Ifpack2_ex1.cpp, Simple.cpp
*/
