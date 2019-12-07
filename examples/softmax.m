
func softmax(Out:float<x:100>,
             In:float<x:100>) {
    var mx : float
    mx = In[0]

    // Find Max.
    for (i in 0 .. 100) {
      mx = max(mx, In[i]);
    }

    var sum : float = 0.

    // Compute exp.
    for (i in 0 .. 100) {
      var e : float
      e = exp(In[i] - mx)
      sum += e
      Out[i] = e
    }

    // Normalize the output.
    for (i in 0 .. 100) {
      Out[i] = Out[i] / sum;
    }
}
