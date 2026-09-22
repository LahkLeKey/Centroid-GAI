/** @file test_tokenizer.c @brief Unit tests for the tokenizer module. */

#include "internal/tokenizer.h"
#include "test_utils.h"

#include <string.h>

int test_tokenizer(void) {
    cgai_token_list tokens = {0};
    TEST_CHECK(cgai_tokenize("The red fox, can't sleep!", &tokens) == CGAI_STATUS_OK,
               "tokenization failed");
    TEST_CHECK(tokens.count == 7U, "unexpected token count");
    TEST_CHECK(strcmp(tokens.items[0], "the") == 0, "token was not lowercased");
    TEST_CHECK(strcmp(tokens.items[3], ",") == 0, "punctuation token was lost");
    TEST_CHECK(strcmp(tokens.items[4], "can't") == 0, "apostrophe token was split");
    TEST_CHECK(strcmp(tokens.items[6], "!") == 0, "final punctuation token was lost");
    cgai_token_list_destroy(&tokens);

    TEST_CHECK(cgai_tokenize("Caf\303\251--OK", &tokens) == CGAI_STATUS_OK,
               "UTF-8 tokenization failed");
    TEST_CHECK(tokens.count == 4U, "UTF-8 or repeated punctuation was grouped incorrectly");
    TEST_CHECK(strcmp(tokens.items[0], "caf\303\251") == 0, "UTF-8 spelling changed");
    TEST_CHECK(strcmp(tokens.items[1], "-") == 0 && strcmp(tokens.items[2], "-") == 0,
               "repeated punctuation was not tokenized separately");
    cgai_token_list_destroy(&tokens);

    TEST_CHECK(cgai_tokenize(" \t\n", &tokens) == CGAI_STATUS_OK, "whitespace tokenization failed");
    TEST_CHECK(tokens.count == 0U, "whitespace created tokens");
    cgai_token_list_destroy(&tokens);
    return 0;
}