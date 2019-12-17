
let sx = 1024
let sy = 1024

let sx2 = sx * 2

func concat(O:float<width:sx2, height:sy>,
            A:float<width:sx, height:sy>,
            B:float<height:sy, width:sx>) {
  for (i in 0 .. A.height) {
    for (j in 0 .. A.width) {
      O[i,j] = A[j,i]
    }
  }
  for (i in 0 .. B.height) {
    for (j in 0 .. B.width) {
      O[i + sx, j] = B[j,i]
    }
  }
}

