/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Music and Performing Arts: Album, Band, Song; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-021-0000:0008", .category = 24U, .observations = UINT64_C(6059),
        .description = "Music and Performing Arts; article-title cues include Album, Band, Song, Election, Love, Life",
        .cluster = 72U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0666961372F, 0.0699346960F, -0.0690302327F, 0.0551813841F,
            -0.0567471609F, 0.0699150711F, -0.0532654747F, 0.0674061403F,
            -0.0542087033F, -0.0564261787F, 0.0722976923F, -0.0750015453F,
            0.0634284690F, 0.0680964440F, 0.0606761277F, -0.0679020658F,
            0.0744858757F, 0.0762169659F, 0.0757114068F, 0.0596939065F,
            0.0572527796F, -0.0633115098F, 0.0655970275F, -0.0719769150F,
            0.0580600165F, -0.0583905876F, -0.0585948825F, 0.0748166963F,
            -0.0718990713F, 0.0670850798F, -0.0584004112F, -0.0745637938F
        }
    },
    {
        .id = "encyclopedia:cluster-021-0002:0011", .category = 24U, .observations = UINT64_C(6677),
        .description = "Music and Performing Arts; article-title cues include Album, Band, Song, Election, Love, Life; observed target words include Album (112), Song (82), Band (58)",
        .cluster = 73U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0188681427F, 0.0860805362F, -0.00420959201F, 0.0909872204F,
            0.00615995331F, -0.0761875510F, -0.0707336813F, 0.00583343394F,
            -0.0713869110F, 0.000105902109F, 0.0858686790F, 0.00386542128F,
            0.0797971115F, -0.0738048181F, -0.00997240376F, -0.0790823251F,
            -0.00770436320F, -0.00495973229F, -0.0757902488F, -0.00539216772F,
            0.0776174068F, 0.0715632141F, 0.0741576031F, 0.00755433133F,
            0.0773876905F, 0.0726927370F, 0.0837154463F, 0.00896636304F,
            -0.0815090612F, 0.00593931321F, 0.0731693581F, -0.000467735401F
        }
    },
    {
        .id = "encyclopedia:cluster-021-0004:0006", .category = 24U, .observations = UINT64_C(5993),
        .description = "Music and Performing Arts; article-title cues include Album, Band, Song, Election, Love, Life; observed target words include Album (91)",
        .cluster = 74U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0551891886F, 0.0800259039F, -0.0331942290F, 0.0833098963F,
            0.0392902792F, -0.0523673557F, -0.0397131033F, 0.0547861382F,
            -0.0591419972F, -0.0650118366F, 0.0739986822F, 0.0593188480F,
            0.0538815446F, -0.0441277735F, -0.0619638115F, -0.0680008009F,
            -0.0511383228F, -0.0561921336F, -0.0491521060F, -0.0653067380F,
            0.0661523119F, 0.0630158260F, 0.0611770898F, 0.0534195043F,
            0.0538520291F, 0.0607741736F, 0.0801439062F, -0.0513841510F,
            -0.0530850850F, 0.0528098196F, 0.0452880785F, 0.0579816401F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 24U,
        .key = "encyclopedia/cluster-021",
        .name = "Music and Performing Arts: Album, Band, Song",
        .description = "Music and Performing Arts; article-title cues include Album, Band, Song, Election, Love, Life",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_021(void) { return &module; }
