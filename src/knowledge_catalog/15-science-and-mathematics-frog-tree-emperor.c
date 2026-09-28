/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Science and Mathematics: Frog, Tree, Emperor; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-012-0000:0011", .category = 15U, .observations = UINT64_C(4763),
        .description = "Science and Mathematics; article-title cues include Frog, Tree, Emperor, Japan, Covid, Japanese; observed target words include Body (73), Heart (42)",
        .cluster = 45U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0711238384F, 0.0912894160F, -0.0621917173F, 0.0973268971F,
            0.0517501384F, -0.0661629215F, -0.0699115768F, 0.0713589191F,
            -0.0813551471F, -0.0769261941F, 0.0882462412F, 0.0659154356F,
            0.0723363981F, -0.0605709553F, -0.0727940202F, -0.0863285288F,
            -0.0587400347F, -0.0720271319F, -0.0658042431F, -0.0676846430F,
            0.0792272463F, 0.0850416869F, 0.0772973374F, 0.0709258765F,
            0.0785961524F, 0.0835943073F, 0.0869964138F, -0.0613626838F,
            -0.0728806555F, 0.0727815852F, 0.0650370643F, 0.0690083951F
        }
    },
    {
        .id = "encyclopedia:cluster-012-0000:0014", .category = 15U, .observations = UINT64_C(4757),
        .description = "Science and Mathematics; article-title cues include Frog, Tree, Emperor, Japan, Covid, Japanese",
        .cluster = 46U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0480496585F, 0.0727744102F, 0.0537849292F, 0.0571541488F,
            -0.0680548474F, -0.0643139780F, -0.0521497317F, -0.0541812591F,
            -0.0515304916F, 0.0623939671F, 0.0741368905F, -0.0577239357F,
            0.0564109981F, -0.0546396561F, 0.0443954729F, -0.0562746674F,
            0.0512826517F, 0.0531779267F, -0.0660977289F, 0.0678195879F,
            0.0630876422F, 0.0562746637F, 0.0625550002F, -0.0592352226F,
            0.0612667874F, 0.0560641289F, 0.0638184547F, 0.0644007176F,
            -0.0600156225F, -0.0538715795F, 0.0679185614F, -0.0615764186F
        }
    },
    {
        .id = "encyclopedia:cluster-012-0001:0014", .category = 15U, .observations = UINT64_C(5098),
        .description = "Science and Mathematics; article-title cues include Frog, Tree, Emperor, Japan, Covid, Japanese; observed target words include References (117)",
        .cluster = 47U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0115932534F, 0.0822853073F, -0.0113158217F, 0.0717555732F,
            -0.0808867365F, 0.00968608167F, -0.0807134509F, 0.00564058032F,
            -0.0682879016F, -0.0161242075F, 0.0898101255F, -0.0908272266F,
            0.0749801844F, 0.00197651633F, 0.0679874495F, -0.0812333822F,
            0.0723335445F, 0.0850248933F, 0.0139396284F, 0.0781706646F,
            0.0776504651F, -0.0192912258F, 0.0762635097F, -0.0830136091F,
            0.0747723654F, -0.00543252751F, -0.00552499434F, 0.0796961561F,
            -0.0743446425F, 0.00757086696F, 0.00475056795F, -0.0880992338F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 15U,
        .key = "encyclopedia/cluster-012",
        .name = "Science and Mathematics: Frog, Tree, Emperor",
        .description = "Science and Mathematics; article-title cues include Frog, Tree, Emperor, Japan, Covid, Japanese",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_012(void) { return &module; }
