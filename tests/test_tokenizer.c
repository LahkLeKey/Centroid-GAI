/** @file test_tokenizer.c @brief Unit tests for the tokenizer module. */

#include "internal/tokenizer.h"
#include "test_utils.h"

#include <string.h>

/**
 * @brief Check spelling normalization, punctuation, UTF-8 bytes, and empty input.
 *
 * The token list is destroyed and reset between scenarios, demonstrating ownership as well as
 * scanner behavior. The escaped byte sequence encodes an accented UTF-8 spelling explicitly.
 * Assertions compare copied token strings, so failures identify the exact boundary or spelling rule.
 *
 * @return Zero after all tokenization scenarios pass.
 */
int test_tokenizer(void) {
    /* Step 1: Zero-initialize the owning list before the word/apostrophe/punctuation case. */
    cgai_token_list tokens = {0};
    TEST_CHECK(cgai_tokenize("The red fox, can't sleep!", &tokens) == CGAI_STATUS_OK,
               "tokenization failed");
    TEST_CHECK(tokens.count == 7U, "unexpected token count");
    TEST_CHECK(strcmp(tokens.items[0], "the") == 0, "token was not lowercased");
    TEST_CHECK(strcmp(tokens.items[3], ",") == 0, "punctuation token was lost");
    TEST_CHECK(strcmp(tokens.items[4], "can't") == 0, "apostrophe token was split");
    TEST_CHECK(strcmp(tokens.items[6], "!") == 0, "final punctuation token was lost");
    cgai_token_list_destroy(&tokens);

    /* Step 2: Reuse the reset list to check UTF-8 bytes and separately emitted punctuation. */
    TEST_CHECK(cgai_tokenize("Caf\303\251--OK", &tokens) == CGAI_STATUS_OK,
               "UTF-8 tokenization failed");
    TEST_CHECK(tokens.count == 4U, "UTF-8 or repeated punctuation was grouped incorrectly");
    TEST_CHECK(strcmp(tokens.items[0], "caf\303\251") == 0, "UTF-8 spelling changed");
    TEST_CHECK(strcmp(tokens.items[1], "-") == 0 && strcmp(tokens.items[2], "-") == 0,
               "repeated punctuation was not tokenized separately");
    cgai_token_list_destroy(&tokens);

    /* Step 3: Confirm whitespace alone produces an empty list that can still be destroyed. */
    TEST_CHECK(cgai_tokenize(" \t\n", &tokens) == CGAI_STATUS_OK, "whitespace tokenization failed");
    TEST_CHECK(tokens.count == 0U, "whitespace created tokens");
    cgai_token_list_destroy(&tokens);
    return 0;
}