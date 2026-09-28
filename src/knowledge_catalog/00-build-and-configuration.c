/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Build and Configuration; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "build-000:0006", .category = 0U, .observations = UINT64_C(253),
        .description = "Build systems, compiler settings, and project configuration; observed target words include Centroid (32), Cgai (17), Target (16), Add (9)",
        .cluster = 0U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.00489105377F, 0.0374980904F, -0.0770923272F, 0.0892035142F,
            0.0831479505F, -0.0768594146F, 0.0749961585F, -0.0726670697F,
            0.0202629324F, 0.0782568976F, -0.0703380257F, 0.0640495270F,
            0.0165364239F, -0.0228249133F, 0.0889705941F, 0.00209616590F,
            0.0838466585F, -0.00815175753F, 0.122043468F, -0.0214274749F,
            0.00722012855F, 0.0857099220F, 0.0116453683F, -0.0284146946F,
            -0.0666114837F, 0.0847782865F, 0.00302780024F, 0.0619533844F,
            -0.0121111870F, -0.0917655081F, 0.0139744403F, -0.0479789153F
        }
    },
    {
        .id = "build-000:0012", .category = 0U, .observations = UINT64_C(290),
        .description = "Build systems, compiler settings, and project configuration; observed target words include Gai (53), C (13), Private (7), Public (7)",
        .cluster = 1U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0599415377F, 0.0306819286F, -0.0751808807F, -0.0601446927F,
            0.0148329875F, -0.0365744866F, -0.102205381F, 0.0607543252F,
            0.0973287374F, -0.00447021751F, 0.0150361713F, -0.0203191619F,
            -0.0589255691F, 0.0386063829F, -0.0408415236F, -0.0896075442F,
            -0.102205366F, 0.0144266048F, 0.141421467F, -0.0493755750F,
            -0.00182872114F, -0.0629893765F, 0.0889979303F, -0.0656308904F,
            -0.0286500156F, 0.0375904515F, -0.0613638721F, -0.0727425739F,
            -0.118054315F, -0.0117851105F, 0.0424670316F, 0.0977351740F
        }
    },
    {
        .id = "build-000:0014", .category = 0U, .observations = UINT64_C(230),
        .description = "Build systems, compiler settings, and project configuration; observed target words include C (26), Src (26), Tests (10), Centroid (7)",
        .cluster = 2U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.00486776046F, 0.0166528802F, -0.0632809177F, 0.0912065059F,
            -0.0832643881F, 0.0530330129F, -0.0179338697F, 0.0517520346F,
            -0.0197272580F, -0.0466280617F, -0.0491900370F, -0.0179338697F,
            -0.0112727154F, 0.0450908653F, 0.0778842270F, -0.0655867383F,
            0.00179338572F, 0.0689172819F, 0.0363801084F, 0.0494462326F,
            0.0422726870F, -0.0422726944F, -0.0330495499F, -0.0420164876F,
            -0.0148594975F, -0.0748098493F, -0.0350991338F, -0.00333057484F,
            -0.101710640F, 0.0338181444F, -0.0753222406F, 0.0194710530F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 0U,
        .key = "build",
        .name = "Build and Configuration",
        .description = "Build systems, compiler settings, and project configuration",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_build(void) { return &module; }
