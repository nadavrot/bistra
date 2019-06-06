
let batch = 32
let sx = 512
let sy = 512

func batched_add(Out:float<x:sx, y:sy>, In:float<b:batch, x:sx, y:sy>) {
  for (x in 0 .. In.x) {
    for (y in 0 .. In.y) {
      Out[x,y] = 0.0
        for (b in 0 .. In.b) {
          Out[x,y] += In[b,x,y]
        }
    }
  }
}
