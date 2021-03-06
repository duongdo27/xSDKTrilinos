#include "petscksp.h"
#include "AnasaziBasicEigenproblem.hpp"
#include "AnasaziConfigDefs.hpp"
#include "AnasaziTpetraAdapter.hpp"
#include "AnasaziRTRSolMgr.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Tpetra_PETScAIJMatrix.hpp"

int main(int argc,char **args)
{
  using Teuchos::RCP;
  using Teuchos::rcp;
  using std::cout;
  using std::endl;

  typedef Tpetra::PETScAIJMatrix<>           PETScAIJMatrix;
  typedef PETScAIJMatrix::scalar_type                Scalar;
  typedef PETScAIJMatrix::local_ordinal_type             LO;
  typedef PETScAIJMatrix::global_ordinal_type            GO;
  typedef PETScAIJMatrix::node_type                    Node;
  typedef Tpetra::Vector<Scalar,LO,GO,Node>          Vector;
  typedef Tpetra::Map<LO,GO,Node>                       Map;
  typedef Tpetra::Operator<Scalar,LO,GO,Node>            OP;
  typedef Tpetra::MultiVector<Scalar,LO,GO,Node>         MV;
  typedef Anasazi::RTRSolMgr<Scalar,MV,OP>           SolMgr;
  typedef Anasazi::BasicEigenproblem<Scalar,MV,OP>  Problem;
  typedef Anasazi::OperatorTraits<Scalar,MV,OP>         OPT;
  typedef Anasazi::MultiVecTraits<Scalar,MV>            MVT;

  Mat            A;
  PetscInt       m = 50,n = 50;
  PetscInt       nev = 4;
  PetscErrorCode ierr;
  MPI_Comm       comm;
  PetscInt       Istart, Iend, Ii, i, j, J, rank;
  PetscScalar    v, tol=1e-6;

  // Initialize PETSc
  PetscInitialize(&argc,&args,NULL,NULL);

  // Create the matrix
  ierr = MatCreate(PETSC_COMM_WORLD,&A);CHKERRQ(ierr);
  ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,m*n,m*n);CHKERRQ(ierr);
  ierr = MatSetType(A, MATAIJ);CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(A,5,,5,);CHKERRQ(ierr);
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

  // Wrap the PETSc matrix as a PETScAIJMatrix.
  RCP<PETScAIJMatrix> tpetraA = rcp(new PETScAIJMatrix(A));

  // Create an initial guess
  RCP<MV> initGuess = rcp(new MV(tpetraA->getDomainMap(),4,false));
  initGuess->randomize();

  // Create an eigenproblem
  RCP<Problem> problem = rcp(new Problem(tpetraA,initGuess));
  problem->setNEV(nev);
  problem->setHermitian(true);
  problem->setProblem();

  // Create the parameter list
  Teuchos::ParameterList pl;
  pl.set("Verbosity", Anasazi::IterationDetails + Anasazi::FinalSummary);
  pl.set("Convergence Tolerance", tol);

  // Create an Anasazi eigensolver
  RCP<SolMgr> solver = rcp(new SolMgr(problem, pl));

  // Solve the problem to the specified tolerances
  Anasazi::ReturnType returnCode = solver->solve();
  if (returnCode != Anasazi::Converged && rank == 0) {
    cout << "Anasazi::EigensolverMgr::solve() returned unconverged." << endl;
    return EXIT_FAILURE;
  }
  else if (rank == 0)
    cout << "Anasazi::EigensolverMgr::solve() returned converged." << endl;

  // Get the eigenvalues and eigenvectors from the eigenproblem
  Anasazi::Eigensolution<Scalar,MV> sol = problem->getSolution();
  std::vector<Anasazi::Value<Scalar> > evals = sol.Evals;
  RCP<MV> evecs = sol.Evecs;
  int numev = sol.numVecs;

  // Terminate PETSc
  ierr = PetscFinalize();CHKERRQ(ierr);
  return EXIT_SUCCESS;
}
