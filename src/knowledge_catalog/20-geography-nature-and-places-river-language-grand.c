/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Geography, Nature, and Places: River, Language, Grand; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-017-0000:0013", .category = 20U, .observations = UINT64_C(4349),
        .description = "Geography, Nature, and Places; article-title cues include River, Language, Grand, Hurricane, Rivers, District; observed target words include River (161), United (30), World (29)",
        .cluster = 60U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0836529955F, 0.0958066508F, -0.0604566187F, 0.0910506845F,
            0.0553485490F, -0.0683153123F, -0.0700360164F, 0.0777725503F,
            -0.0716890246F, -0.0737482980F, 0.0898448750F, 0.0633560568F,
            0.0820677578F, -0.0664183572F, -0.0733148232F, -0.0834495798F,
            -0.0611477308F, -0.0714315102F, -0.0680713356F, -0.0729082823F,
            0.0794797614F, 0.0808481500F, 0.0800622925F, 0.0692908019F,
            0.0758485422F, 0.0809023604F, 0.0879750997F, -0.0659440309F,
            -0.0775827840F, 0.0730843022F, 0.0633020326F, 0.0691958442F
        }
    },
    {
        .id = "encyclopedia:cluster-017-0001:0002", .category = 20U, .observations = UINT64_C(4562),
        .description = "Geography, Nature, and Places; article-title cues include River, Language, Grand, Hurricane, Rivers, District; observed target words include River (70), Season (34)",
        .cluster = 61U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0819171742F, 0.0948855504F, -0.0567813516F, 0.0842034221F,
            0.0590160266F, -0.0670630857F, -0.0642472208F, 0.0691039264F,
            -0.0628263801F, -0.0705119073F, 0.0847588032F, 0.0639113486F,
            0.0739992335F, -0.0597135350F, -0.0701371506F, -0.0828341171F,
            -0.0593518317F, -0.0694397464F, -0.0627747700F, -0.0717906132F,
            0.0796826109F, 0.0790625885F, 0.0793466941F, 0.0678508580F,
            0.0709509254F, 0.0735728666F, 0.0862055644F, -0.0607209802F,
            -0.0726947635F, 0.0672051385F, 0.0606563352F, 0.0690650567F
        }
    },
    {
        .id = "encyclopedia:cluster-017-0002:0015", .category = 20U, .observations = UINT64_C(5135),
        .description = "Geography, Nature, and Places; article-title cues include River, Language, Grand, Hurricane, Rivers, District; observed target words include References (139)",
        .cluster = 62U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0523732454F, 0.0814056098F, -0.0498945378F, 0.0597747378F,
            -0.0774465203F, 0.0606582500F, -0.0689089671F, 0.0411388911F,
            -0.0448339246F, -0.0684385076F, 0.0857776925F, -0.0844465345F,
            0.0618058071F, 0.0440651700F, 0.0477831066F, -0.0522240512F,
            0.0502158962F, 0.0706416965F, 0.0718122646F, 0.0639403090F,
            0.0582370572F, -0.0634123161F, 0.0715368688F, -0.0667058676F,
            0.0477715768F, -0.0386831574F, -0.0378798954F, 0.0538419336F,
            -0.0416437946F, 0.0598320998F, -0.0406339355F, -0.0709286109F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 20U,
        .key = "encyclopedia/cluster-017",
        .name = "Geography, Nature, and Places: River, Language, Grand",
        .description = "Geography, Nature, and Places; article-title cues include River, Language, Grand, Hurricane, Rivers, District",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_017(void) { return &module; }
