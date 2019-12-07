
let m = 512
let n = 512
let k = 512

func gemm(C:float<I:m, J:n>,
         A:float<I:m, K:k>,
         B:float<K:k, J:n>) {
  // Matmul:
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C[i,j] = 0.0;
      for (k in 0 .. A.K) {
        C[i,j] += A[i,k] * B[k,j];
      }
    }
  }

  // Relu:
  for (x in 0 .. C.I) {
    for (y in 0 .. C.J) {
      C[x,y] = max(C[x, y], 0.);
    }
  }
}

script for "x86" {
  // Merge the relu into the matmul. The paraeter 3 means to recursively apply
  // loop fusion to subloops.
  fuse "i" 3 times
}
