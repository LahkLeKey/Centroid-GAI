/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Public C API; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "public-api-000:0003", .category = 38U, .observations = UINT64_C(999),
        .description = "Public C headers and supported library interfaces; observed target words include Param (53), Cgai (39), Brief (29), Return (24)",
        .cluster = 114U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.133835912F, -0.0619337857F, -0.120092601F, -0.0409942493F,
            -0.0979733393F, -0.0588076115F, 0.0442973971F, -0.0717841834F,
            0.0739666298F, 0.0973245278F, 0.129176110F, 0.0743794665F,
            0.136962146F, 0.0877100453F, 0.0724920183F, 0.0562122725F,
            0.0786263794F, -0.0568611361F, 0.117497250F, 0.101453438F,
            0.0809267461F, -0.0645290688F, -0.0731408373F, -0.102633081F,
            0.131830335F, 0.0687170252F, 0.0566251986F, 0.111185923F,
            0.0989170820F, 0.109475322F, -0.0813986734F, -0.137139127F
        }
    },
    {
        .id = "public-api-000:0010", .category = 38U, .observations = UINT64_C(746),
        .description = "Public C headers and supported library interfaces; observed target words include Cgai (15), Model (14), Define (12), Endif (6)",
        .cluster = 115U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0736174211F, 0.0813583285F, -0.0204580706F, 0.0743283406F,
            -0.0561609305F, 0.0551341139F, -0.0621640682F, 0.0178514495F,
            -0.0849128515F, -0.0800155848F, 0.0478671603F, -0.0672193617F,
            0.0834121108F, 0.0390993990F, 0.0210109781F, -0.0373616628F,
            0.0582146794F, 0.0500788689F, 0.0334122106F, 0.0764610767F,
            0.0316744596F, -0.0433648154F, 0.0881514177F, -0.0383095369F,
            0.0258292910F, -0.0406791642F, 0.000789885235F, 0.0462873764F,
            -0.0519745797F, 0.0707738623F, -0.0694310516F, -0.0695890337F
        }
    },
    {
        .id = "public-api-000:0011", .category = 38U, .observations = UINT64_C(1271),
        .description = "Public C headers and supported library interfaces; observed target words include Cgai (33), Model (28)",
        .cluster = 116U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0318503864F, -0.0212799627F, -0.0100604678F, 0.0113585852F,
            -0.0415399671F, 0.0136303054F, 0.0218362883F, 0.0128885191F,
            0.0124249011F, 0.0147429835F, 0.000139087133F, -0.0168756153F,
            -0.00560974563F, 0.0251743477F, -0.0111731365F, 0.00996774249F,
            -0.0184519030F, -0.0404736623F, 0.0182200819F, -0.0430698581F,
            -0.0213727057F, 0.0321749449F, -0.0186373349F, 0.0131666847F,
            -0.00477525266F, 0.00449707499F, 0.0216044784F, -0.0293468591F,
            0.0388046354F, -0.000463616132F, 0.0141402949F, -0.0186837111F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 38U,
        .key = "public-api",
        .name = "Public C API",
        .description = "Public C headers and supported library interfaces",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_public_api(void) { return &module; }
