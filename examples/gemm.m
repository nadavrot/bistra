
let m = 512
let n = 512
let k = 512

def gemm(C:float<I:m, J:n>,
         A:float<I:m, K:k>,
         B:float<K:k, J:n>) {
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
