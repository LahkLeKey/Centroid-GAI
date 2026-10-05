#ifndef CENTROID_EXTENSION_FIXTURES_H
#define CENTROID_EXTENSION_FIXTURES_H
static const unsigned char extension_development[] =
    "unsigned mask_bits(unsigned x, unsigned mask) { return (x ^ mask) & mask; }\n";
static const unsigned char extension_audit[] =
    "unsigned intersect_bits(unsigned left, unsigned right) { unsigned both = left & right; return both; }\n";
#endif
