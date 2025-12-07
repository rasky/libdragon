#pragma once

// Compact a demangled C++ symbol to fit within max_len while staying readable.
// Returns a newly allocated string; caller must free.
char *compact_symbol(const char *orig, int max_len);

// Simple truncation with trailing "..." when possible. Returns a newly allocated string.
char *simple_truncate(const char *s, int max_len);

// Ellipsis helper: keep head/tail within max_len, modifies buffer in-place.
void head_tail_ellipsis(char *buf, int max_len);

