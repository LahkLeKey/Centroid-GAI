/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Database and Persistence; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "database-000:0004", .category = 1U, .observations = UINT64_C(3258),
        .description = "Database schemas, migrations, and data access; observed target words include Pg (86), Codecid (82), Nullable (81), Model (56)",
        .cluster = 3U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0999817103F, -0.0844455138F, 0.129697621F, 0.142611265F,
            0.0723455772F, -0.120527983F, 0.0640622303F, -0.0937780663F,
            0.0842104331F, -0.104304373F, -0.106022574F, -0.137854800F,
            0.123765573F, -0.105624720F, 0.0890755802F, 0.109838881F,
            0.121920623F, 0.102025509F, -0.108627051F, -0.0803941414F,
            -0.0941398740F, 0.121088564F, -0.113998733F, 0.0841381773F,
            -0.0837039948F, -0.0907035097F, -0.0918970853F, 0.0714232326F,
            -0.111557148F, 0.0708807781F, -0.101157404F, 0.0875745118F
        }
    },
    {
        .id = "database-000:0008", .category = 1U, .observations = UINT64_C(2252),
        .description = "Database schemas, migrations, and data access; observed target words include True (206), False (139), Codectypes (137), Readonly (13)",
        .cluster = 4U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0334138311F, -0.101026490F, 0.106966093F, 0.110158406F,
            -0.0497675128F, -0.0907694250F, -0.0481713824F, 0.0356117934F,
            -0.0673247948F, -0.0994042307F, -0.108300604F, -0.106966101F,
            0.0856671780F, 0.0814805105F, -0.0383330695F, 0.0991163775F,
            0.130436927F, 0.114815898F, -0.0874987915F, 0.0798583478F,
            -0.100267604F, 0.139621258F, -0.105448499F, 0.108666830F,
            -0.0831288546F, 0.0772417486F, 0.0667230263F, -0.0834168866F,
            0.0380190052F, -0.0651007220F, -0.129442528F, 0.114371061F
        }
    },
    {
        .id = "database-000:0015", .category = 1U, .observations = UINT64_C(1722),
        .description = "Database schemas, migrations, and data access; observed target words include Nullable (185), Output (131), Codecid (53), Kind (33)",
        .cluster = 5U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0737766922F, 0.0559484921F, 0.0487623811F, -0.0650165379F,
            0.0209421832F, 0.0484886803F, 0.0216607787F, -0.0915365070F,
            -0.0223451778F, -0.0542374775F, -0.119835854F, -0.117406316F,
            -0.0686438307F, 0.112478673F, 0.0187521670F, -0.125858366F,
            0.134652585F, 0.0228242651F, 0.116037451F, 0.0538268983F,
            0.0543401986F, 0.150735676F, 0.00229268870F, 0.0801757351F,
            0.116721898F, 0.0461617485F, 0.103821263F, -0.0763432980F,
            -0.0577620976F, -0.0676172674F, -0.0799362212F, 0.0265541300F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 1U,
        .key = "database",
        .name = "Database and Persistence",
        .description = "Database schemas, migrations, and data access",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_database(void) { return &module; }
