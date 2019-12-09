let kernel = 3
let stride = 2

let batch = 1
let channels = 128
let size_out = 28
let size_in = size_out * stride + kernel

func maxpool2d(Out: float<N:batch, H:size_out, W:size_out, C:channels>,
               In: float<N:batch, H:size_in, W:size_in, C:channels>) {

  for (n in 0 .. Out.N) {
    // For each output channel:
    for (c in 0 .. Out.C) {
      // For each pixel in the output buffer.
      for (outx in 0 .. Out.W) {
        for (outy in 0 .. Out.H) {
          // Identify the start coordinate of the max region.
          let inX = outx * stride;
          let inY = outy * stride;

          // Init the output buffer.
          Out[n, outx, outy, c] = -999.0

            // For each element in the filter:
            for (fx in 0 .. kernel) {
              for (fy in 0 .. kernel) {
                let px = inX + fx;
                let py = inY + fy;
                Out[n, outx, outy, c] = max(In[n, px, py, c], Out[n, outx, outy, c]);
              } // FY
            } // FX
        } // OY
      } // OX
    } // C
  } // N
}

script for "x86" {
  // Nothing to do here. The optimizer knows to sink the loop 'c' below the
  // kernel loops.
}

