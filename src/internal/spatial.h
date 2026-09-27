/** @file spatial.h @brief Private owned spatial storage and bounded query workspace. */
#ifndef CGAI_INTERNAL_SPATIAL_H
#define CGAI_INTERNAL_SPATIAL_H
#include "centroid_gai_spatial.h"

/** One tree node; a leaf has at most eight rows and no child nodes. */
typedef struct cgai_spatial_node {
    size_t begin;        /**< Start within the row permutation. */
    size_t count;        /**< Number of rows below this node. */
    size_t left;         /**< Left child index, or SIZE_MAX for leaves. */
    size_t right;        /**< Right child index, or SIZE_MAX for leaves. */
    uint64_t categories; /**< One membership bit per category 0..63. */
} cgai_spatial_node;

/** Owned immutable arrays; row count also bounds the allocated node capacity. */
struct cgai_spatial_index {
    size_t count;             /**< Number of input rows. */
    size_t dimensions;        /**< Components per vector and per bound. */
    size_t used;              /**< Number of initialized nodes; root is node zero. */
    double *vectors;          /**< Owned count*dimensions copied coordinates. */
    double *minimum;          /**< Owned count*dimensions lower-bound capacity. */
    double *maximum;          /**< Owned count*dimensions upper-bound capacity. */
    uint32_t *categories;     /**< Owned count input category IDs. */
    size_t *order;            /**< Owned row permutation used by leaves. */
    cgai_spatial_node *nodes; /**< Owned count-element node capacity. */
};

/**
 * @brief Check a coordinate array before any distance arithmetic.
 * @param values Borrowed array containing count readable doubles, or NULL.
 * @param count Number of elements to validate.
 * @return Nonzero if every element is finite and its magnitude is at most 1e100.
 */
int cgai_spatial_finite(const double *values, size_t count);
#endif
