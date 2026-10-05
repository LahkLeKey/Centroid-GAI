#ifndef CENTROID_RESEARCH_FIXTURES_H
#define CENTROID_RESEARCH_FIXTURES_H

/* Frozen independently authored C families. This file is quarantined from
 * automatic source admission, including the TRAIN constants: the research
 * harness explicitly admits only the two declared TRAIN records. */
static const unsigned char r_train_search[] =
    "#include <stddef.h>\n"
    "size_t research_binary_search(const int *items, size_t count, int key) {\n"
    "  size_t first = 0;\n"
    "  size_t last = count;\n"
    "  while (first < last) {\n"
    "    size_t middle = first + (last - first) / 2;\n"
    "    if (items[middle] < key) first = middle + 1;\n"
    "    else last = middle;\n"
    "  }\n"
    "  return first < count && items[first] == key ? first : count;\n"
    "}\n";
static const unsigned char r_train_reverse[] =
    "#include <stddef.h>\n"
    "void research_reverse_bytes(unsigned char *items, size_t count) {\n"
    "  size_t first = 0;\n"
    "  size_t last = count;\n"
    "  while (first < last) {\n"
    "    unsigned char temporary;\n"
    "    --last;\n"
    "    if (first == last) break;\n"
    "    temporary = items[first];\n"
    "    items[first] = items[last];\n"
    "    items[last] = temporary;\n"
    "    ++first;\n"
    "  }\n"
    "}\n";
static const unsigned char r_dev_count[] =
    "#include <stddef.h>\n"
    "size_t research_development_count(const unsigned char *items, size_t "
    "count,\n"
    "                                  unsigned char needle) {\n"
    "  size_t matches = 0;\n"
    "  size_t position;\n"
    "  for (position = 0; position < count; ++position) {\n"
    "    if (items[position] == needle) {\n"
    "      ++matches;\n"
    "    }\n"
    "  }\n"
    "  return matches;\n"
    "}\n";
static const unsigned char r_dev_insert[] =
    "#include <stddef.h>\n"
    "void research_development_insertion_sort(int *items, size_t count) {\n"
    "  size_t position;\n"
    "  for (position = 1; position < count; ++position) {\n"
    "    int held = items[position];\n"
    "    size_t destination = position;\n"
    "    while (destination > 0 && held < items[destination - 1]) {\n"
    "      items[destination] = items[destination - 1];\n"
    "      --destination;\n"
    "    }\n"
    "    items[destination] = held;\n"
    "  }\n"
    "}\n";
static const unsigned char r_audit_ring[] =
    "#include <stddef.h>\n"
    "int research_audit_ring_push(int *storage, size_t capacity, size_t "
    "*tail,\n"
    "                             size_t *used, int value) {\n"
    "  size_t next;\n"
    "  if (capacity == 0 || *used >= capacity) return 0;\n"
    "  next = *tail + 1;\n"
    "  if (next == capacity) next = 0;\n"
    "  storage[*tail] = value;\n"
    "  *tail = next;\n"
    "  ++*used;\n"
    "  return 1;\n"
    "}\n";
static const unsigned char r_audit_gcd[] =
    "unsigned research_audit_unsigned_gcd(unsigned first, unsigned second) {\n"
    "  unsigned remainder;\n"
    "  while (second != 0) {\n"
    "    remainder = first % second;\n"
    "    first = second;\n"
    "    second = remainder;\n"
    "  }\n"
    "  return first;\n"
    "}\n"
    "int research_audit_coprime(unsigned first, unsigned second) {\n"
    "  return research_audit_unsigned_gcd(first, second) == 1;\n"
    "}\n";

#endif
