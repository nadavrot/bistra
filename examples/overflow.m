// Run this program with the flag '--warn' to detect the buffer overflow.
func memcpy(Dest:float<len:1024>,
           Src:float<len:512>) {
  for (i in 0 .. Dest.len) {
    Dest[i] = Src[i]
  }
}
