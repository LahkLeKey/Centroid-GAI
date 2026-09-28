/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Guides and Documentation; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "documentation-000:0006", .category = 2U, .observations = UINT64_C(1253),
        .description = "Project guides, API contracts, and technical documentation; observed target words include C (47), Md (28)",
        .cluster = 6U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0585022792F, 0.0769841596F, -0.0719051734F, 0.0921740532F,
            -0.0745386109F, 0.0676726997F, -0.0740214661F, 0.0793356001F,
            -0.0827685222F, -0.0813108459F, 0.0706354603F, -0.0707295164F,
            0.0868599340F, 0.0765608773F, 0.0708705336F, -0.0835679621F,
            0.0659797415F, 0.0830037370F, 0.0694598109F, 0.0704473928F,
            0.0858253166F, -0.0704473928F, 0.0819690302F, -0.0698829368F,
            0.0609948076F, -0.0784420297F, -0.0686132163F, 0.0650391281F,
            -0.0836151242F, 0.0815457851F, -0.0755733624F, -0.0759965032F
        }
    },
    {
        .id = "documentation-000:0008", .category = 2U, .observations = UINT64_C(1199),
        .description = "Project guides, API contracts, and technical documentation",
        .cluster = 7U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0508656912F, 0.0648721382F, 0.0838424042F, 0.0780432001F,
            -0.0704256147F, -0.0763722956F, -0.0583358072F, -0.0615794510F,
            -0.0907227322F, 0.0444276147F, 0.0410857014F, -0.0700325221F,
            0.0794683844F, -0.0511605777F, 0.0503251217F, -0.0585324019F,
            0.0636435151F, 0.0611371063F, -0.0774534643F, 0.0562225208F,
            0.0783872008F, 0.0715560392F, 0.0811393932F, -0.0364660472F,
            0.0426583625F, 0.0501285046F, 0.0686564744F, 0.0650687367F,
            -0.0635944009F, -0.0448699184F, 0.0527332425F, -0.0731286258F
        }
    },
    {
        .id = "documentation-000:0010", .category = 2U, .observations = UINT64_C(1271),
        .description = "Project guides, API contracts, and technical documentation; observed target words include Run (26)",
        .cluster = 8U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.000880869862F, 0.00834508240F, -0.0643034577F, 0.0209554248F,
            0.00148357067F, -0.00927232206F, 0.0402882174F, -0.0633298904F,
            0.000880870386F, 0.0198427364F, 0.0112658683F, 0.0537794344F,
            -0.000370894675F, 0.0152993202F, 0.0197963770F, 0.0127030676F,
            -0.00802055467F, -0.0340293571F, 0.0465006456F, 0.0000463620090F,
            0.0172928646F, -0.000695425842F, -0.0240152776F, 0.0140011879F,
            -0.0279096570F, 0.0588328466F, 0.0497923233F, 0.0375992432F,
            0.0293004662F, 0.0159947444F, -0.0192864295F, -0.0662970766F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 2U,
        .key = "documentation",
        .name = "Guides and Documentation",
        .description = "Project guides, API contracts, and technical documentation",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_documentation(void) { return &module; }
