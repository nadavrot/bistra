// Perform GEMM on matrices C = A * B;
def gemm(C:float<I:512,J:512>,
         A:float<I:512,K:512>,
         B:float<K:512,J:512>) {
  #widen 4
  for (i in 0 .. C.I) {
    #widen 3
    #vectorize 8
    for (j in 0 .. C.J) {
      C[i,j] = 0.0;
      for (k in 0 .. A.K) {
        C[i,j] += A[i,k] * B[k,j];
      }
    }
  }
}
