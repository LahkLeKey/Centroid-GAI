/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: People and Biographies: Movie, Man, Love; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-001-0000:0004", .category = 4U, .observations = UINT64_C(4341),
        .description = "People and Biographies; article-title cues include Movie, Man, Love, Island, Night, Woman; observed target words include Movies (72)",
        .cluster = 12U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.00950191170F, -0.0335961357F, -0.0518262908F, -0.0548669249F,
            -0.0679796487F, 0.0415641963F, 0.00637987209F, 0.0507267714F,
            0.0720924139F, 0.0308677331F, -0.0447541252F, -0.000366503606F,
            -0.0532651544F, 0.0559935905F, 0.0155695928F, -0.0364331082F,
            0.0282750651F, -0.0510797240F, 0.0308405701F, 0.000637983088F,
            -0.0498579852F, 0.00532107195F, -0.00401797378F, 0.0576223992F,
            0.0356865413F, -0.0163297318F, 0.00344784465F, 0.00663778326F,
            -0.00895898975F, -0.0650882944F, -0.000882323715F, -0.00228046160F
        }
    },
    {
        .id = "encyclopedia:cluster-001-0000:0005", .category = 4U, .observations = UINT64_C(4703),
        .description = "People and Biographies; article-title cues include Movie, Man, Love, Island, Night, Woman",
        .cluster = 13U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0269255731F, 0.0693376139F, 0.0407454595F, 0.0703272894F,
            -0.0403947420F, -0.0518088825F, -0.0431761667F, -0.0740359426F,
            -0.0321753807F, 0.0539388359F, 0.0400062427F, -0.0310101658F,
            0.0398934558F, -0.0478246212F, 0.0472356565F, -0.0496914797F,
            0.0501048341F, 0.0401816703F, -0.0413594246F, 0.0587125309F,
            0.0223273039F, 0.0565073416F, 0.0420485437F, -0.0438778661F,
            0.0330148526F, 0.0712921023F, 0.0718056485F, 0.0645639971F,
            -0.0453436971F, -0.0711164773F, 0.0563820638F, -0.0812402219F
        }
    },
    {
        .id = "encyclopedia:cluster-001-0003:0011", .category = 4U, .observations = UINT64_C(5414),
        .description = "People and Biographies; article-title cues include Movie, Man, Love, Island, Night, Woman; observed target words include American (56), Movies (56), Websites (55)",
        .cluster = 14U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0534074605F, 0.0769710466F, 0.0569011383F, 0.0785710961F,
            -0.0754147619F, -0.0548768118F, -0.0699291453F, -0.0604493767F,
            -0.0686668307F, 0.0595786013F, 0.0744459629F, -0.0701251552F,
            0.0733033195F, -0.0540604480F, 0.0723127127F, -0.0667401403F,
            0.0724978819F, 0.0669796616F, -0.0548658408F, 0.0744677857F,
            0.0732706338F, 0.0564658828F, 0.0766009539F, -0.0716815665F,
            0.0697332919F, 0.0543108433F, 0.0561936870F, 0.0729438588F,
            -0.0764812976F, -0.0546916015F, 0.0612875111F, -0.0738583431F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 4U,
        .key = "encyclopedia/cluster-001",
        .name = "People and Biographies: Movie, Man, Love",
        .description = "People and Biographies; article-title cues include Movie, Man, Love, Island, Night, Woman",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_001(void) { return &module; }
