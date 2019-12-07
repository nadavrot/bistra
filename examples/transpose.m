
let sx = 1024
let sy = 1024

func transpose(A:float<width:sx, height:sy>,
               B:float<height:sy, width:sx>) {
  for (i in 0 .. A.height) {
    for (j in 0 .. A.width) {
      A[i,j] = B[j,i];
    }
  }
}

script for "x86" {
  tile "i" to 64 as "i_tiled"
  tile "j" to 64 as "j_tiled"
  / Reorder the loops as [i, j, i_t, j_t].
  hoist "j" 1 times
}
