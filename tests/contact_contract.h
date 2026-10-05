#ifndef CENTROID_TEST_CONTACT_CONTRACT_H
#define CENTROID_TEST_CONTACT_CONTRACT_H

/* Included after the native test's CHECK and read_artifact helpers. */
static void check_contact_artifacts(const char *directory,
                                    const c_model *original_model,
                                    const unsigned char *original_cells,
                                    uint64_t original_generation,
                                    size_t expected_receipts) {
  char path[4096], parent_digest[C_DIGEST_HEX], proof_digest[C_DIGEST_HEX];
  size_t parent_length = 0, proof_length = 0, payload_length = 0;
  unsigned char *parent =
      read_artifact(directory, "model-parent.centroid", &parent_length);
  unsigned char *proof = read_artifact(directory, "contact.bin", &proof_length);
  c_hash(parent, parent_length, parent_digest);
  c_hash(proof, proof_length, proof_digest);
  int n = snprintf(path, sizeof(path), "%s/model-parent.centroid", directory);
  CHECK(n > 0 && (size_t)n < sizeof(path));
  c_trainer *frozen = NULL;
  CHECK(c_trainer_load(path, &frozen) == C_OK);
  CHECK(!memcmp(frozen->model, original_model, sizeof(*original_model)) &&
        frozen->generation == original_generation &&
        !memcmp(frozen->cells, original_cells, C_WORLD_CELLS));
  n = snprintf(path, sizeof(path), "%s/contact.bin", directory);
  CHECK(n > 0 && (size_t)n < sizeof(path));
  unsigned char *payload = NULL;
  CHECK(c_envelope_read(path, "CCONT001", &payload, &payload_length) == C_OK);
  c_reader reader = {payload, payload_length, 0, C_OK};
  CHECK(c_get_u32(&reader) == 1u && c_get_u64(&reader) == original_generation);
  const unsigned groups = c_get_u32(&reader);
  CHECK(groups == original_model->groups);
  for (unsigned group = 0; group < groups; ++group)
    CHECK(c_get_u64(&reader) == original_model->expert[group].uid);
  const unsigned eligible = c_get_u32(&reader);
  const unsigned participants = c_get_u32(&reader);
  const unsigned graph = c_get_u32(&reader);
  char bound_parent[C_DIGEST_HEX] = {0};
  unsigned char cells[C_WORLD_CELLS], evolved[C_WORLD_CELLS];
  c_get_bytes(&reader, bound_parent, C_DIGEST_HEX - 1u);
  c_get_bytes(&reader, cells, sizeof(cells));
  CHECK(reader.status == C_OK && reader.offset == reader.length &&
        !strcmp(bound_parent, parent_digest) &&
        !memcmp(cells, original_cells, sizeof(cells)));
  unsigned physical, restricted = 0, recomputed = 0;
  c_life_evolve(cells, evolved, &physical);
  for (unsigned first = 0; first < groups; ++first)
    for (unsigned second = first + 1u; second < groups; ++second)
      if ((physical & (1u << (first * C_MAX_GROUPS + second))) &&
          (eligible & (1u << first)) && (eligible & (1u << second))) {
        restricted |= 1u << (first * C_MAX_GROUPS + second);
        recomputed |= (1u << first) | (1u << second);
      }
  CHECK(eligible == (1u << groups) - 1u && graph == restricted &&
        participants == recomputed && participants);
  /* Tampering changes the full artifact identity even if a consumer never
   * trusts its contents; the retained proof itself remains immutable. */
  char tampered_digest[C_DIGEST_HEX];
  proof[proof_length - 1u] ^= 1u;
  c_hash(proof, proof_length, tampered_digest);
  CHECK(strcmp(tampered_digest, proof_digest));
  if (expected_receipts) {
    size_t length = 0, counted = 0;
    unsigned char *receipts =
        read_artifact(directory, "train-receipts.txt", &length);
    char parent_binding[96], proof_binding[96];
    CHECK(snprintf(parent_binding, sizeof(parent_binding), "model-parent=%s",
                   parent_digest) > 0);
    CHECK(snprintf(proof_binding, sizeof(proof_binding), "contact=%s",
                   proof_digest) > 0);
    for (const char *line = (const char *)receipts; *line;) {
      const char *end = strchr(line, '\n');
      char record[2048];
      CHECK(end && (size_t)(end - line) < sizeof(record));
      memcpy(record, line, (size_t)(end - line));
      record[end - line] = 0;
      CHECK(strstr(record, parent_binding) && strstr(record, proof_binding));
      const char *binding = strstr(record, "TRAIN ");
      const char *receipt = strstr(record, "receipt=");
      char receipt_digest[C_DIGEST_HEX];
      CHECK(binding && receipt);
      c_hash(binding, strlen(binding), receipt_digest);
      CHECK(!memcmp(receipt + strlen("receipt="), receipt_digest,
                    C_DIGEST_HEX - 1u));
      ++counted;
      line = end + 1;
    }
    CHECK(counted == expected_receipts);
    free(receipts);
  }
  size_t report_length = 0;
  unsigned char *report = read_artifact(directory, "report.md", &report_length);
  CHECK(strstr((const char *)report, parent_digest) &&
        strstr((const char *)report, proof_digest));
  free(report);
  free(payload);
  free(proof);
  free(parent);
  c_trainer_destroy(frozen);
}

#endif
