/** @file core_contract.h @brief Private numerical status and thread-local diagnostics. */
#ifndef CGAI_CORE_CONTRACT_H
#define CGAI_CORE_CONTRACT_H

/** Numerical operation result; this private convention is distinct from the Life public status. */
typedef enum cgai_status {
    CGAI_STATUS_ERROR = 0, /**< Failed operation; inspect the thread-local diagnostic. */
    CGAI_STATUS_OK = 1     /**< Successfully completed operation. */
} cgai_status;

/**
 * @brief Borrow the current thread's diagnostic, or the static no-error sentinel.
 * @return NUL-terminated text; copy it before another operation changes the diagnostic.
 * The caller never owns or frees this storage.
 */
const char *cgai_last_error(void);

#endif
