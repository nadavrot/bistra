#ifndef BISTRA_BASE_BASE_H
#define BISTRA_BASE_BASE_H

namespace bistra {

/// Debug location marker.
struct DebugLoc {
  /// The position in the file.
  const char *pos_;

  /// \returns the start of the range.
  const char *getStart() { return pos_; }

  /// \returns true if the location if valid.
  bool isValid() { return pos_; }

  DebugLoc(const char *pos) : pos_(pos) {}

  /// \returns a new empty location.
  static DebugLoc npos() { return DebugLoc(nullptr); }
};

} // end namespace bistra

#endif
