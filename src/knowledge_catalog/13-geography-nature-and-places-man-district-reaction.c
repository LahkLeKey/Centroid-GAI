/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Geography, Nature, and Places: Man, District, Reaction; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-010-0000:0012", .category = 13U, .observations = UINT64_C(4686),
        .description = "Geography, Nature, and Places; article-title cues include Man, District, Reaction, Cup, Leonardo, Adolf",
        .cluster = 39U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0457975268F, 0.0783915073F, 0.0601202026F, 0.0657285228F,
            -0.0700165331F, -0.0740781948F, -0.0641064942F, -0.0656656846F,
            -0.0603590272F, 0.0624339245F, 0.0754238293F, -0.0727327242F,
            0.0668978468F, -0.0654016361F, 0.0596801229F, -0.0643955097F,
            0.0551028550F, 0.0610758439F, -0.0668728724F, 0.0690483600F,
            0.0674889982F, 0.0573789217F, 0.0703182593F, -0.0694758147F,
            0.0754487067F, 0.0610129721F, 0.0711483061F, 0.0703309253F,
            -0.0721039772F, -0.0660931617F, 0.0769075528F, -0.0718023926F
        }
    },
    {
        .id = "encyclopedia:cluster-010-0000:0013", .category = 13U, .observations = UINT64_C(4792),
        .description = "Geography, Nature, and Places; article-title cues include Man, District, Reaction, Cup, Leonardo, Adolf",
        .cluster = 40U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0764236823F, 0.0915731862F, -0.0737431347F, 0.0746652856F,
            -0.0683447421F, 0.0621349737F, -0.0737061203F, 0.0744683892F,
            -0.0781081989F, -0.0867283046F, 0.0874662846F, -0.0577450916F,
            0.0605486818F, 0.0554947890F, 0.0448213480F, -0.0808627531F,
            0.0577205010F, 0.0590239540F, 0.0724026114F, 0.0484242141F,
            0.0762760192F, -0.0521747060F, 0.0805307180F, -0.0653689429F,
            0.0685047433F, -0.0497399718F, -0.0424604006F, 0.0525435954F,
            -0.0721073598F, 0.0805431232F, -0.0517812036F, -0.0625776350F
        }
    },
    {
        .id = "encyclopedia:cluster-010-0000:0014", .category = 13U, .observations = UINT64_C(4706),
        .description = "Geography, Nature, and Places; article-title cues include Man, District, Reaction, Cup, Leonardo, Adolf",
        .cluster = 41U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0431611836F, 0.0753036886F, -0.0225259271F, 0.0806126520F,
            0.0177553203F, -0.0312783644F, -0.0260444283F, 0.0521514751F,
            -0.0814263597F, -0.0676154569F, 0.0717724562F, 0.0538043268F,
            0.0137484455F, -0.0192453451F, -0.0773319528F, -0.0542175248F,
            -0.0181434788F, -0.0818396360F, -0.0356733352F, -0.0759294704F,
            0.0619306900F, 0.0851076841F, 0.0485954471F, 0.0417463183F,
            0.0311281085F, 0.0748152360F, 0.0896781012F, -0.0768060610F,
            -0.0121582607F, 0.0666637272F, 0.0328310430F, 0.0615425035F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 13U,
        .key = "encyclopedia/cluster-010",
        .name = "Geography, Nature, and Places: Man, District, Reaction",
        .description = "Geography, Nature, and Places; article-title cues include Man, District, Reaction, Cup, Leonardo, Adolf",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_010(void) { return &module; }
