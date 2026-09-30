/* Curated static knowledge baseline; maintain these C sources directly. */
#ifndef CGAI_KNOWLEDGE_CATALOG_H
#define CGAI_KNOWLEDGE_CATALOG_H

#include "internal/knowledge_module.h"

const cgai_knowledge_module *cgai_knowledge_build(void);
const cgai_knowledge_module *cgai_knowledge_database(void);
const cgai_knowledge_module *cgai_knowledge_native_core(void);

#endif
