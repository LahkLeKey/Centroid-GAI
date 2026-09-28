/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Education and Institutions: California, University, Department; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-002-0000:0008", .category = 5U, .observations = UINT64_C(5086),
        .description = "Education and Institutions; article-title cues include California, University, Department, Academy, State, Covid; observed target words include World (35), Word (32)",
        .cluster = 15U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0820163190F, 0.0973094404F, -0.0651588663F, 0.0947487801F,
            0.0576858819F, -0.0675106794F, -0.0731645301F, 0.0726199597F,
            -0.0755628496F, -0.0760724247F, 0.0928025767F, 0.0662247315F,
            0.0719016939F, -0.0628879443F, -0.0701638386F, -0.0840900913F,
            -0.0637451261F, -0.0678350478F, -0.0663289577F, -0.0720524564F,
            0.0800350085F, 0.0851442739F, 0.0854222253F, 0.0740681738F,
            0.0790036693F, 0.0811358541F, 0.0851559266F, -0.0665027648F,
            -0.0763853714F, 0.0760378316F, 0.0616365597F, 0.0679278001F
        }
    },
    {
        .id = "encyclopedia:cluster-002-0002:0003", .category = 5U, .observations = UINT64_C(5552),
        .description = "Education and Institutions; article-title cues include California, University, Department, Academy, State, Covid; observed target words include City (36)",
        .cluster = 16U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0830179825F, 0.0926657021F, -0.0610907190F, 0.0819778740F,
            0.0542450100F, -0.0661427304F, -0.0620671436F, 0.0707702562F,
            -0.0681485385F, -0.0700803623F, 0.0874650404F, 0.0539585017F,
            0.0775837824F, -0.0506364964F, -0.0637441799F, -0.0819990486F,
            -0.0615257882F, -0.0536719263F, -0.0661532655F, -0.0511353686F,
            0.0761087462F, 0.0703564286F, 0.0820416287F, 0.0637759492F,
            0.0800353885F, 0.0660788864F, 0.0719481185F, -0.0538311191F,
            -0.0750897229F, 0.0663869232F, 0.0560175329F, 0.0584692694F
        }
    },
    {
        .id = "encyclopedia:cluster-002-0003:0010", .category = 5U, .observations = UINT64_C(5532),
        .description = "Education and Institutions; article-title cues include California, University, Department, Academy, State, Covid",
        .cluster = 17U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0438319556F, 0.0855551437F, -0.0578284450F, 0.0754784569F,
            -0.0734866709F, 0.0571146943F, -0.0540577807F, 0.0697903633F,
            -0.0757658929F, -0.0768310875F, 0.0707916915F, -0.0455789454F,
            0.0326903202F, 0.0587551408F, 0.0396991409F, -0.0738699734F,
            0.0664243698F, 0.0503508523F, 0.0730709955F, 0.0520871468F,
            0.0750841200F, -0.0463351868F, 0.0821357667F, -0.0666799620F,
            0.0513522066F, -0.0569017343F, -0.0442155413F, 0.0385061167F,
            -0.0536953956F, 0.0689276010F, -0.0612902716F, -0.0579881594F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 5U,
        .key = "encyclopedia/cluster-002",
        .name = "Education and Institutions: California, University, Department",
        .description = "Education and Institutions; article-title cues include California, University, Department, Academy, State, Covid",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_002(void) { return &module; }
