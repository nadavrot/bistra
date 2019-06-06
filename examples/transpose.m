
let m = 1024
let n = 1024

func transpose(A:float<m:m, n:n>,
              B:float<n:n, m:m>) {
  for (i in 0 .. A.m) {
    for (j in 0 .. A.n) {
      A[i,j] = B[j,i];
    }
  }
}
