/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Web Application; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "web-000:0007", .category = 41U, .observations = UINT64_C(2984),
        .description = "Browser interface and web application code; observed target words include Classname (219), Div (192), P (151), Span (98)",
        .cluster = 123U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0820298269F, 0.104699455F, -0.0937595665F, -0.0400077812F,
            0.106772877F, -0.0554303415F, -0.0696483180F, 0.0784357935F,
            0.0989137143F, 0.0117495907F, 0.0907183886F, 0.0484596230F,
            0.0605448298F, 0.0422589444F, -0.0766585693F, -0.0588662997F,
            -0.0617296696F, -0.0509081371F, 0.104659967F, -0.0399683155F,
            -0.125256240F, 0.0829381943F, -0.130528778F, -0.0276262779F,
            0.0611767434F, -0.0201618969F, -0.0137045439F, 0.000888621144F,
            0.0910541713F, 0.0385266952F, 0.111038193F, -0.136374027F
        }
    },
    {
        .id = "web-000:0014", .category = 41U, .observations = UINT64_C(2659),
        .description = "Browser interface and web application code; observed target words include Usestate (66), Text (62), Mt (59), Button (39)",
        .cluster = 124U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.116344221F, 0.0474019349F, 0.0791141167F, 0.109828822F,
            0.140034035F, -0.125430226F, -0.0242660418F, -0.119070001F,
            -0.0714463964F, -0.0920780972F, 0.0392910503F, -0.100410476F,
            0.0991253033F, -0.0966876447F, 0.0739950091F, -0.0278782696F,
            -0.0493742563F, -0.0850753784F, 0.0575072989F, 0.0220500007F,
            0.0583936647F, -0.0434130467F, 0.0330638923F, -0.0496845283F,
            -0.119003437F, -0.0878676549F, 0.0491527021F, 0.0992360041F,
            0.0686098039F, -0.0332190543F, 0.0375625640F, -0.0977069438F
        }
    },
    {
        .id = "web-000:0015", .category = 41U, .observations = UINT64_C(4668),
        .description = "Browser interface and web application code; observed target words include Text (280), Slate (255), Xs (110), Indigo (72)",
        .cluster = 125U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0755378380F, 0.0958360881F, 0.0885397568F, 0.0636087656F,
            -0.0737073943F, -0.0840965137F, 0.100632921F, 0.0853714719F,
            -0.0547094457F, -0.0970227495F, -0.0492939427F, 0.0718013793F,
            -0.0950786546F, -0.0797665194F, 0.0618793964F, 0.0636592954F,
            -0.0645808727F, -0.0586225763F, 0.0720792562F, -0.0723440647F,
            -0.108004771F, -0.0969974622F, -0.0752474740F, -0.0617405549F,
            -0.0575117096F, 0.0346130766F, 0.0961894393F, 0.0493571162F,
            -0.0827834979F, 0.0930967554F, 0.0916199684F, -0.100456320F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 41U,
        .key = "web",
        .name = "Web Application",
        .description = "Browser interface and web application code",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_web(void) { return &module; }
