/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: People and Biographies: Peter, Joseph, Richard; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-006-0000:0004", .category = 9U, .observations = UINT64_C(4940),
        .description = "People and Biographies; article-title cues include Peter, Joseph, Richard, Jean, Robert, Charles",
        .cluster = 27U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0709849596F, 0.0576253794F, 0.0528540760F, 0.0224370304F,
            -0.00415103137F, 0.0611919872F, -0.0713191777F, -0.0619195774F,
            -0.0668817014F, -0.0556571893F, 0.0124172829F, -0.0194192305F,
            0.0580786914F, 0.00399595918F, -0.0654979795F, 0.0447071157F,
            0.0517566688F, 0.0560628399F, 0.00141945085F, 0.0526751354F,
            -0.0514704995F, -0.0534386560F, 0.0436931662F, 0.00671561249F,
            -0.0581621230F, 0.00633390667F, 0.0704958960F, -0.00627425686F,
            0.0349975079F, 0.00735971518F, -0.0483332947F, -0.0660465658F
        }
    },
    {
        .id = "encyclopedia:cluster-006-0000:0010", .category = 9U, .observations = UINT64_C(4561),
        .description = "People and Biographies; article-title cues include Peter, Joseph, Richard, Jean, Robert, Charles; observed target words include American (85)",
        .cluster = 28U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0565612502F, 0.0862114727F, 0.0745193064F, 0.0749327838F,
            -0.0727106705F, -0.0646101534F, -0.0722841844F, -0.0786534995F,
            -0.0786924362F, 0.0700879693F, 0.0829300284F, -0.0733178034F,
            0.0760955811F, -0.0672069490F, 0.0715221390F, -0.0751912370F,
            0.0672845393F, 0.0753977746F, -0.0710827485F, 0.0729691163F,
            0.0718062669F, 0.0580987148F, 0.0757466108F, -0.0694289580F,
            0.0706046149F, 0.0681630000F, 0.0702817664F, 0.0832400098F,
            -0.0700364411F, -0.0729044676F, 0.0774262547F, -0.0851779506F
        }
    },
    {
        .id = "encyclopedia:cluster-006-0002:0015", .category = 9U, .observations = UINT64_C(4914),
        .description = "People and Biographies; article-title cues include Peter, Joseph, Richard, Jean, Robert, Charles; observed target words include Age (85)",
        .cluster = 29U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0480134338F, 0.0682549179F, -0.0109001445F, 0.0827406123F,
            0.0300503522F, -0.0361179672F, -0.00424494967F, 0.0372811332F,
            -0.0675354227F, -0.0688185468F, 0.0768646523F, 0.0532416888F,
            0.0254816320F, -0.0119194090F, -0.0700294673F, -0.0410704389F,
            -0.0355304852F, -0.0706051290F, -0.0359261036F, -0.0701013282F,
            0.0650412440F, 0.0671155974F, 0.0462746285F, 0.0323287137F,
            0.0207090545F, 0.0671755672F, 0.0878487825F, -0.0662762150F,
            -0.0168478824F, 0.0509633534F, 0.00962907448F, 0.0585059635F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 9U,
        .key = "encyclopedia/cluster-006",
        .name = "People and Biographies: Peter, Joseph, Richard",
        .description = "People and Biographies; article-title cues include Peter, Joseph, Richard, Jean, Robert, Charles",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_006(void) { return &module; }
