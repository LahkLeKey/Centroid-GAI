/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Film, Television, and Comics: Series, Movie, Comics; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-008-0000:0009", .category = 11U, .observations = UINT64_C(7057),
        .description = "Film, Television, and Comics; article-title cues include Series, Movie, Comics, Episodes, Bart, Friends; observed target words include Series (108)",
        .cluster = 33U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0530304424F, 0.0828899294F, -0.0289158300F, 0.0900291204F,
            0.0333746821F, -0.0421505794F, -0.0313623957F, 0.0706571490F,
            -0.0604786500F, -0.0712833330F, 0.0786398053F, 0.0587335043F,
            0.0353202224F, -0.0232128389F, -0.0786231607F, -0.0745647326F,
            -0.0312120505F, -0.0742811933F, -0.0323559940F, -0.0733043477F,
            0.0690872818F, 0.0724777579F, 0.0796751454F, 0.0427015796F,
            0.0312288161F, 0.0714506134F, 0.0831320062F, -0.0699641183F,
            -0.0456909835F, 0.0697471499F, 0.0235886034F, 0.0695968345F
        }
    },
    {
        .id = "encyclopedia:cluster-008-0001:0006", .category = 11U, .observations = UINT64_C(6081),
        .description = "Film, Television, and Comics; article-title cues include Series, Movie, Comics, Episodes, Bart, Friends",
        .cluster = 34U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0678696334F, 0.0770267472F, -0.0374231935F, 0.0697010010F,
            -0.0644003525F, 0.0592840947F, -0.0662317947F, 0.0310568195F,
            -0.0644489452F, -0.0668133050F, 0.0832962692F, -0.0814746320F,
            0.0662029758F, 0.0478499010F, 0.0336537734F, -0.0514448471F,
            0.0627919436F, 0.0756022483F, 0.0592937469F, 0.0616291724F,
            0.0462121852F, -0.0594392195F, 0.0625593588F, -0.0468710363F,
            0.0438477211F, -0.0252814759F, -0.0215604864F, 0.0353495330F,
            -0.0490126051F, 0.0682086498F, -0.0449039303F, -0.0716970116F
        }
    },
    {
        .id = "encyclopedia:cluster-008-0003:0007", .category = 11U, .observations = UINT64_C(5705),
        .description = "Film, Television, and Comics; article-title cues include Series, Movie, Comics, Episodes, Bart, Friends",
        .cluster = 35U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0272782706F, 0.0796140954F, 0.0725492537F, 0.0649990290F,
            -0.0619725548F, -0.0298604127F, -0.0686449856F, -0.0773727149F,
            -0.0677256286F, 0.0356445685F, 0.0651744008F, -0.0663209558F,
            0.0766911134F, -0.0386605822F, 0.0159992762F, -0.0410361625F,
            0.0660421550F, 0.0695021600F, -0.0536475405F, 0.0740572512F,
            0.0261111297F, 0.0274951514F, 0.0740985349F, -0.0492061675F,
            0.0299327392F, 0.0670130029F, 0.0722804442F, 0.0506729297F,
            -0.0259871613F, -0.0541227572F, 0.0434634089F, -0.0840448886F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 11U,
        .key = "encyclopedia/cluster-008",
        .name = "Film, Television, and Comics: Series, Movie, Comics",
        .description = "Film, Television, and Comics; article-title cues include Series, Movie, Comics, Episodes, Bart, Friends",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_008(void) { return &module; }
