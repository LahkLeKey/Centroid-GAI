/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Application Service; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "service-000:0003", .category = 39U, .observations = UINT64_C(4410),
        .description = "Persistence API, service logic, and shared application code; observed target words include Await (67)",
        .cluster = 117U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0413815603F, 0.0975411832F, 0.0149518512F, 0.103353634F,
            -0.0372927450F, -0.0456172004F, -0.0710448474F, -0.0452563912F,
            -0.0946284086F, -0.00887224544F, 0.0835514516F, -0.0918223560F,
            0.0901252553F, -0.0227016807F, 0.0838586986F, -0.0927175283F,
            0.0579902343F, 0.0448288508F, 0.00983428955F, 0.0999196023F,
            0.0905929655F, 0.00232494506F, 0.0885620415F, -0.0929714143F,
            0.0476215109F, -0.0145509811F, 0.0315872915F, 0.104128577F,
            -0.0616246387F, -0.0297968015F, 0.0278059468F, -0.0956038535F
        }
    },
    {
        .id = "service-000:0007", .category = 39U, .observations = UINT64_C(2362),
        .description = "Persistence API, service logic, and shared application code; observed target words include Assert (162), Const (122), Return (53), Throw (51)",
        .cluster = 118U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0890369713F, -0.100787200F, -0.118100591F, 0.0805298761F,
            0.0695279166F, -0.137833953F, 0.0538363606F, -0.115506127F,
            -0.0672579333F, 0.121992424F, -0.104254805F, 0.0454291068F,
            0.116429158F, 0.104180016F, 0.0713992044F, 0.0486722030F,
            0.109269246F, -0.0966707692F, 0.104229800F, -0.0878395513F,
            0.0549838915F, 0.118499890F, 0.0410133786F, -0.0677569956F,
            -0.0548341796F, 0.0509424359F, -0.0450049751F, 0.121219009F,
            0.0905586705F, -0.0527386516F, -0.0426848382F, -0.131023407F
        }
    },
    {
        .id = "service-000:0015", .category = 39U, .observations = UINT64_C(3316),
        .description = "Persistence API, service logic, and shared application code; observed target words include String (127), Number (63)",
        .cluster = 119U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0716666952F, -0.0677571520F, -0.0222836826F, 0.0545008108F,
            -0.0346160829F, -0.105323263F, -0.0711691529F, 0.0153355813F,
            -0.0503603779F, 0.0263530631F, -0.0837503299F, -0.100010060F,
            0.0887614563F, 0.0681127086F, -0.0851008743F, 0.0288052857F,
            0.0492053442F, -0.0121724792F, -0.0314529911F, -0.00661045499F,
            -0.0747586638F, 0.101662412F, -0.0882996097F, 0.0154777197F,
            0.0152111631F, -0.0122080436F, 0.0329101495F, -0.0100400904F,
            0.0716490075F, 0.00934707560F, 0.0131853838F, -0.0296583232F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 39U,
        .key = "service",
        .name = "Application Service",
        .description = "Persistence API, service logic, and shared application code",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_service(void) { return &module; }
