let batch = 64
let channel = 128
let hw = 32
let epsilon = 0.001

func batchnorm(
  Out:   float<N:batch, H:hw, W:hw, C:channel>,
  In:    float<N:batch, H:hw, W:hw, C:channel>,
  Mean:  float<C:channel>,
  Var:   float<C:channel>,
  Gamma: float<C:channel>,
  Beta:  float<C:channel>) {

  for (n in 0 .. batch) {
    for (x in 0 .. hw) {
      for (y in 0 .. hw) {
        for (c in 0 .. channel) {
            let input = In[n, x, y, c]
            let mu = Mean[c]
            let varr = Var[c]
            let gamma = Gamma[c]
            let beta = Beta[c]
            let stdvar = 1.0 / sqrt(varr + epsilon)
            Out[n, x, y, c] = (input - mu) * gamma * stdvar + beta;
        }
      }
    }
  }
}
