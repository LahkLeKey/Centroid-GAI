/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: History, Government, and Conflict: Nhl, Ron, Forest; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-027-0000:0003", .category = 30U, .observations = UINT64_C(5192),
        .description = "History, Government, and Conflict; article-title cues include Nhl, Ron, Forest, Iphone, Route, State; observed target words include Called (53)",
        .cluster = 90U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0182610303F, -0.0317667313F, -0.0435926877F, -0.0342862383F,
            -0.0492218696F, -0.0161614139F, 0.0262055304F, 0.0577906594F,
            0.0419243015F, 0.0403581150F, -0.0470087752F, -0.0361248292F,
            -0.0366808996F, 0.0183518380F, -0.0375888981F, -0.0442395657F,
            0.0612861998F, -0.0102370707F, -0.0184199251F, -0.0259104893F,
            -0.0427755937F, 0.0101122195F, -0.0391436256F, 0.0540113971F,
            0.0325951688F, -0.0496757478F, 0.00711601926F, -0.000397226133F,
            0.00404035160F, -0.0131311500F, -0.00612862036F, 0.0416973084F
        }
    },
    {
        .id = "encyclopedia:cluster-027-0000:0004", .category = 30U, .observations = UINT64_C(4498),
        .description = "History, Government, and Conflict; article-title cues include Nhl, Ron, Forest, Iphone, Route, State",
        .cluster = 91U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0774233490F, 0.0737683028F, 0.0766635239F, 0.0221789647F,
            -0.0108078253F, 0.0492575504F, -0.0739255622F, -0.0614407957F,
            -0.0735062435F, -0.0608906411F, -0.00690390542F, -0.0140173873F,
            0.0716330186F, 0.00461133523F, -0.0781438574F, 0.0643359944F,
            0.0858860463F, 0.0603928156F, -0.0108733047F, 0.0770958215F,
            -0.0689998269F, -0.0500434749F, 0.0677159801F, 0.0122095598F,
            -0.0594233423F, 0.0110305315F, 0.0831089616F, -0.000602614600F,
            0.0728250816F, -0.00204365770F, -0.0517596416F, -0.0668774545F
        }
    },
    {
        .id = "encyclopedia:cluster-027-0000:0010", .category = 30U, .observations = UINT64_C(4800),
        .description = "History, Government, and Conflict; article-title cues include Nhl, Ron, Forest, Iphone, Route, State",
        .cluster = 92U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0840671286F, 0.0946737304F, -0.0667455122F, 0.0949437842F,
            0.0533522107F, -0.0694462955F, -0.0718891397F, 0.0712753236F,
            -0.0704405680F, -0.0772662014F, 0.0899841711F, 0.0670032129F,
            0.0777201802F, -0.0651618391F, -0.0700967833F, -0.0831710175F,
            -0.0592815802F, -0.0737428218F, -0.0721837059F, -0.0660949573F,
            0.0769223794F, 0.0771556348F, 0.0802001804F, 0.0729571283F,
            0.0834533423F, 0.0744548738F, 0.0894195512F, -0.0636517182F,
            -0.0714104772F, 0.0724415109F, 0.0650145039F, 0.0669171959F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 30U,
        .key = "encyclopedia/cluster-027",
        .name = "History, Government, and Conflict: Nhl, Ron, Forest",
        .description = "History, Government, and Conflict; article-title cues include Nhl, Ron, Forest, Iphone, Route, State",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_027(void) { return &module; }
