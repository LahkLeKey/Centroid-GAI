/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Music and Performing Arts: Music, Singer, Orchestra; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-003-0000:0014", .category = 6U, .observations = UINT64_C(7159),
        .description = "Music and Performing Arts; article-title cues include Music, Singer, Orchestra, Musician, Paul, Queen; observed target words include American (600), English (135), French (132), German (130)",
        .cluster = 18U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0621521324F, 0.0819477439F, 0.0815608427F, 0.0713131949F,
            -0.0737906992F, -0.0642264709F, -0.0759966001F, -0.0806881711F,
            -0.0809764192F, 0.0696010664F, 0.0817169845F, -0.0732229054F,
            0.0737495050F, -0.0709756464F, 0.0733792037F, -0.0777743608F,
            0.0750910342F, 0.0756261721F, -0.0775111392F, 0.0725724101F,
            0.0607693568F, 0.0591889098F, 0.0709015876F, -0.0722843856F,
            0.0653128549F, 0.0721279234F, 0.0703009441F, 0.0803261399F,
            -0.0636008084F, -0.0760624185F, 0.0787375495F, -0.0760952532F
        }
    },
    {
        .id = "encyclopedia:cluster-003-0000:0015", .category = 6U, .observations = UINT64_C(6189),
        .description = "Music and Performing Arts; article-title cues include Music, Singer, Orchestra, Musician, Paul, Queen",
        .cluster = 19U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0621246174F, 0.0878885910F, -0.0539175570F, 0.0620580167F,
            -0.0376746245F, 0.0640384480F, -0.0675422400F, 0.0578402095F,
            -0.0484238677F, -0.0398168489F, 0.0621530414F, -0.0665138885F,
            0.0847561657F, 0.0850320607F, 0.0938011110F, -0.0550695844F,
            0.106197342F, 0.103798047F, 0.104493171F, 0.0773106292F,
            0.0406166203F, -0.0618485808F, 0.0665995330F, -0.0632481128F,
            0.0539745986F, -0.0329141170F, -0.0601156875F, 0.0763299689F,
            -0.0953434855F, 0.0460341759F, -0.0662376881F, -0.0862699375F
        }
    },
    {
        .id = "encyclopedia:cluster-003-0001:0015", .category = 6U, .observations = UINT64_C(6256),
        .description = "Music and Performing Arts; article-title cues include Music, Singer, Orchestra, Musician, Paul, Queen",
        .cluster = 20U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0653681532F, 0.0811921582F, -0.0598204210F, 0.0591328330F,
            -0.0360090807F, 0.0595472343F, -0.0645110980F, 0.0588879585F,
            -0.0520779267F, -0.0460968576F, 0.0669223443F, -0.0624955259F,
            0.0738924220F, 0.0691359714F, 0.0825201049F, -0.0632487908F,
            0.0868530422F, 0.0867305547F, 0.0865798369F, 0.0658296943F,
            0.0507686958F, -0.0554124117F, 0.0614028126F, -0.0657074228F,
            0.0614403859F, -0.0384673625F, -0.0542913303F, 0.0686176792F,
            -0.0958011746F, 0.0473778285F, -0.0536604002F, -0.0719520524F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 6U,
        .key = "encyclopedia/cluster-003",
        .name = "Music and Performing Arts: Music, Singer, Orchestra",
        .description = "Music and Performing Arts; article-title cues include Music, Singer, Orchestra, Musician, Paul, Queen",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_003(void) { return &module; }
