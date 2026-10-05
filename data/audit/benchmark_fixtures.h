#ifndef C_BENCHMARK_AUDIT_FIXTURES_H
#define C_BENCHMARK_AUDIT_FIXTURES_H

/* Frozen held-out bytes and offsets stay in the scanner's quarantined
 * data/audit tree. The benchmark includes this native header solely for frozen
 * inference; its SOURCE training context never receives these fixtures. */
static const unsigned char audit_count[] =
    "int sum_count(int n) { int total = 0; for(int i = 0; i < n; ++i) total += "
    "i; return total; }\n";
static const unsigned char audit_lookup[] =
    "int lookup_value(const int *values, int index) { return values[index]; "
    "}\n";

#define C_BENCHMARK_AUDIT_FAMILIES                                             \
  {"counted-loop",                                                             \
   "audit/counted-loop.c",                                                     \
   "AUDIT",                                                                    \
   audit_count,                                                                \
   sizeof(audit_count) - 1,                                                    \
   {0, 4, 16, 28, 43, 58, 76, sizeof(audit_count) - 1}},                       \
  {                                                                            \
    "array-lookup", "audit/array-lookup.c", "AUDIT", audit_lookup,             \
        sizeof(audit_lookup) - 1,                                              \
        {0, 4, 16, 25, 36, 48, 60, sizeof(audit_lookup) - 1}                   \
  }

#endif
