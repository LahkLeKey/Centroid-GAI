/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Developer and Knowledge Tools; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "tooling-000:0003", .category = 40U, .observations = UINT64_C(1775),
        .description = "Knowledge builders, evaluation, and repository tools; observed target words include Self (103), Path (34), Repo (24)",
        .cluster = 120U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0549086593F, 0.0837903917F, -0.0860478655F, 0.0998580754F,
            0.0698143542F, -0.0641708300F, 0.0659302473F, -0.0607182086F,
            0.0706774518F, 0.0856828168F, -0.0433227457F, 0.0733332708F,
            -0.0581952035F, -0.0792092755F, 0.0643699244F, -0.101717137F,
            0.0620793402F, 0.112605907F, 0.106895871F, 0.0909279883F,
            -0.0650340095F, 0.0825953782F, -0.0748936012F, 0.0716401488F,
            -0.0609174073F, 0.0631084219F, 0.0797071531F, 0.0916582793F,
            -0.0883385167F, -0.0796408728F, 0.0719721764F, -0.0705114678F
        }
    },
    {
        .id = "tooling-000:0007", .category = 40U, .observations = UINT64_C(2048),
        .description = "Knowledge builders, evaluation, and repository tools; observed target words include Repo (43), Txt (37), Write (37), Assertequal (37)",
        .cluster = 121U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0873812810F, 0.0650827959F, -0.0950635970F, 0.102975816F,
            -0.0542644784F, 0.0517900437F, -0.0725924447F, 0.0659460202F,
            -0.0781167224F, -0.0531135313F, 0.0973077267F, -0.0729663819F,
            0.0763615444F, 0.0460355841F, 0.0804471597F, -0.0779440477F,
            0.0684779584F, 0.0959553421F, 0.110600404F, 0.0694563314F,
            0.0744338557F, -0.0587241538F, 0.0832669437F, -0.0782604069F,
            0.0705783665F, -0.0744049475F, -0.0550125279F, 0.103810266F,
            -0.0707797408F, 0.0523942374F, -0.0670106187F, -0.0620617606F
        }
    },
    {
        .id = "tooling-000:0010", .category = 40U, .observations = UINT64_C(1643),
        .description = "Knowledge builders, evaluation, and repository tools; observed target words include Self (85), Def (42), Return (29)",
        .cluster = 122U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0508560091F, -0.0808746964F, -0.0803009644F, 0.124019802F,
            0.115878597F, -0.109674096F, 0.106410325F, -0.0936424509F,
            -0.0472337082F, 0.0951487422F, -0.0971213877F, 0.0657398254F,
            0.0849633589F, 0.0625119954F, 0.100671984F, 0.0829548165F,
            0.0865055248F, -0.0426788926F, 0.0855729803F, -0.0828113332F,
            0.0530795865F, 0.113260493F, 0.0434679352F, -0.0618305877F,
            -0.104079112F, 0.0772163719F, -0.0707250237F, 0.109315351F,
            0.0456915051F, -0.0784001499F, -0.0832059830F, -0.0734149143F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 40U,
        .key = "tooling",
        .name = "Developer and Knowledge Tools",
        .description = "Knowledge builders, evaluation, and repository tools",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_tooling(void) { return &module; }
