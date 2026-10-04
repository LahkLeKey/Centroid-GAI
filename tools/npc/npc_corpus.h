/** @file npc_corpus.h @brief Verified, provenance-preserving bounded NPC teacher corpus. */
#ifndef CGAI_NPC_CORPUS_H
#define CGAI_NPC_CORPUS_H
#include "npc_teacher.h"
/** Maximum examples including any separately recorded recovery collection. */
#define NPC_CORPUS_CAPACITY 8192U
/** Selected siblings per family, covering both cue values and every rotation. */
#define NPC_CORPUS_VARIANTS 8U
/** Selected-step source identity parallel to one training record. */
typedef struct npc_corpus_provenance {
    uint32_t family;  /**< Pinned training-family ID. */
    uint32_t variant; /**< Selected family-local variant. */
    uint32_t step;    /**< Pre-action authoritative decision index. */
} npc_corpus_provenance;
/** @brief Generate canonical verified train-family teacher rollouts.
 * @param examples Writable example array; unchanged on failure.
 * @param capacity Available records, at most8192.
 * @param count Writable exact successful record count.
 * @param history Nonzero keeps history; zero masks it after full-history verification.
 * @return OK on verified publication, ERROR on invalid arguments or failed replay. */
cgai_status npc_corpus_generate(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                int history);
/** @brief Generate verified canonical records with their explicit source identities.
 * @param examples Writable record array, unchanged on failure.
 * @param capacity Available records, at most8192.
 * @param count Writable exact successful count.
 * @param history Nonzero keeps history; zero masks it after verification.
 * @param provenance Optional writable parallel array with the same capacity.
 * @return OK on verified publication, ERROR otherwise. */
cgai_status npc_corpus_generate_ex(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                   int history, npc_corpus_provenance *provenance);
/** @brief Write deterministic selected-step provenance and categorical records as TSV.
 * @param path Trusted destination for generated local data.
 * @param history Nonzero keeps history; zero masks it after verification.
 * @return OK after complete generation, write and close, ERROR otherwise. */
cgai_status npc_corpus_write(const char *path, int history);
/** @brief Validate categories and reject incompatible full-history duplicate targets.
 * @param examples Borrowed complete categorical records.
 * @param count Record count, one through8192.
 * @param history Nonzero rejects conflicts; zero allows declared history ablation ambiguity.
 * @return One for valid records, zero otherwise. */
int npc_corpus_validate(const cgai_gameplay_example *examples, size_t count, int history);
#endif
