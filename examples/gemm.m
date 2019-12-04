
let m = 512
let n = 512
let k = 512

func gemm(C:float<I:m, J:n>,
         A:float<I:m, K:k>,
         B:float<K:k, J:n>) {
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C[i,j] = 0.0;
      for (k in 0 .. A.K) {
        C[i,j] += A[i,k] * B[k,j];
      }
    }
  }
}

script for "x86" {
  widen "i" to 4
  vectorize "j" to 8 as "j8"
  widen "j8" to 3 as "j8_3"
}
