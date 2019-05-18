let kernel = 7
let stride = 2
let pad = 3

def conv(
  Out:float<N:3, H:112, W:112, C:64>,
  In:float <N:3, H:224, W:224, C:3>,
  Filter:float <CI:3, K0:7, K1:7, CO:64>,
  Bias:float<O:64>) {

  for (n in 0 .. Out.N) {
    // For each output channel:
    #widen 8
    #vectorize 8
    for (d in 0 .. Out.C) {
      // For each pixel in the output buffer.
      for (outx in 0 .. Out.W) {
        for (outy in 0 .. Out.H) {
          // Convolve centere at this particular location.
          let inX = outx * stride - pad;
          let inY = outy * stride - pad;

          // Init the output buffer.
          Out[n, outx, outy, d] = Bias[d]

            // For each element in the filter:
            for (fx in 0 .. kernel) {
              for (fy in 0 .. kernel) {
                let px = inX + fx;
                let py = inY + fy;
                // If the pixel is inbounds.
                if (px in 0 .. In.H) {
                  if (py in 0 .. In.W) {
                    for (fd in 0 .. Filter.CI) {
                      Out[n, outx, outy, d] += Filter[fd,fx,fy,d] * In[n, px, py, fd];
                    } // Depth channel (fd).
                  }// y in bounds
                } // x in bounds
              } // FY
            } // FX
        } // OY
      } // OX
    } // Out channel
  } // N
}


