let kernel = 3
let stride = 1
let pad = 1

let batch = 1
let channel_in = 128
let channel_out = 128
let channel_out8 = 128/8
let size_in = 28
let size_out = 28

func conv(
  Out: float<N:batch, H:size_out, W:size_out, C:channel_out>,
  In: float<N:batch, H:size_in, W:size_in, C:channel_in>,
  Filter: float<CO:channel_out8, K0:kernel, K1:kernel, CI:channel_in, L:8>,
  Bias: float<O:channel_out>) {

  for (n in 0 .. Out.N) {
    // For each output channel:
    for (d in 0 .. Out.C, 8) {
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
                      Out[n, outx, outy, d] += Filter[d/8,fx,fy,fd, 0].8 * In[n, px, py, fd];
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


