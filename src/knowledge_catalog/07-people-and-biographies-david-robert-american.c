/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: People and Biographies: David, Robert, American; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-004-0002:0010", .category = 7U, .observations = UINT64_C(6213),
        .description = "People and Biographies; article-title cues include David, Robert, American, Michael, John, Actor; observed target words include References (165)",
        .cluster = 21U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0120923901F, 0.0894645602F, -0.00252280920F, 0.0725070313F,
            -0.0675656497F, -0.00115707610F, -0.0843241736F, -0.000227619283F,
            -0.0758454278F, -0.0128226932F, 0.0903184116F, -0.0777991489F,
            0.0699177533F, -0.00724595971F, 0.0567061268F, -0.0901192650F,
            0.0715775490F, 0.0731328651F, 0.00538704544F, 0.0557578653F,
            0.0741002783F, -0.00686659804F, 0.0779985115F, -0.0764050335F,
            0.0708947182F, 0.0165215544F, 0.0165784564F, 0.0592575148F,
            -0.0779509172F, 0.00876343530F, 0.0101576177F, -0.0698607937F
        }
    },
    {
        .id = "encyclopedia:cluster-004-0003:0005", .category = 7U, .observations = UINT64_C(5026),
        .description = "People and Biographies; article-title cues include David, Robert, American, Michael, John, Actor; observed target words include Born (120), American (67)",
        .cluster = 22U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0609655045F, 0.0403193608F, -0.0356648341F, 0.0492648222F,
            0.00944966543F, -0.0437779203F, 0.0780007392F, -0.0163317528F,
            0.0519731157F, 0.0230848659F, -0.0419958308F, 0.0391000733F,
            -0.0574483015F, -0.0548221059F, 0.0567799844F, 0.00247380836F,
            -0.00297793024F, 0.0302365888F, 0.0363800600F, 0.00198137434F,
            -0.0554903932F, 0.0267779678F, -0.0635682940F, 0.0336248800F,
            -0.0415621065F, 0.0696414858F, 0.0264731348F, 0.0639786422F,
            -0.0381738767F, -0.0394165888F, 0.0315262340F, -0.0650690943F
        }
    },
    {
        .id = "encyclopedia:cluster-004-0003:0009", .category = 7U, .observations = UINT64_C(5080),
        .description = "People and Biographies; article-title cues include David, Robert, American, Michael, John, Actor; observed target words include American (98), Politician (59), Covid (40)",
        .cluster = 23U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0727519691F, 0.0817301795F, 0.0804889947F, 0.0850477368F,
            -0.0715343580F, -0.0846301019F, -0.0752577186F, -0.0829945281F,
            -0.0763249919F, 0.0849897712F, 0.0774731711F, -0.0750024021F,
            0.0818346068F, -0.0796771869F, 0.0836440921F, -0.0810575113F,
            0.0759884566F, 0.0719634071F, -0.0745733529F, 0.0723809898F,
            0.0699682236F, 0.0750605464F, 0.0766264945F, -0.0866134763F,
            0.0701771006F, 0.0742949024F, 0.0768930838F, 0.0885042772F,
            -0.0748864859F, -0.0862076357F, 0.0780185759F, -0.0737843364F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 7U,
        .key = "encyclopedia/cluster-004",
        .name = "People and Biographies: David, Robert, American",
        .description = "People and Biographies; article-title cues include David, Robert, American, Michael, John, Actor",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_004(void) { return &module; }
