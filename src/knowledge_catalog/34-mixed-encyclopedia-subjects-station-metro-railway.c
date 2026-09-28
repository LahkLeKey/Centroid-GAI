/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Mixed Encyclopedia Subjects: Station, Metro, Railway; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-031-0000:0003", .category = 34U, .observations = UINT64_C(4943),
        .description = "Mixed Encyclopedia Subjects; article-title cues include Station, Metro, Railway, Mrt, Chicago, House; observed target words include United (54), Dam (49), Line (42), Subway (42)",
        .cluster = 102U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0842099711F, 0.0941996500F, -0.0699525401F, 0.0955230743F,
            0.0580911636F, -0.0685936809F, -0.0693684742F, 0.0766283125F,
            -0.0777250826F, -0.0703340918F, 0.0926857889F, 0.0710731372F,
            0.0808841363F, -0.0582222044F, -0.0689749196F, -0.0872140750F,
            -0.0616078191F, -0.0724678785F, -0.0697976351F, -0.0678781942F,
            0.0835901275F, 0.0802283734F, 0.0849729851F, 0.0732544735F,
            0.0787264630F, 0.0773315057F, 0.0911836997F, -0.0594977550F,
            -0.0723725706F, 0.0705127865F, 0.0668291599F, 0.0638013333F
        }
    },
    {
        .id = "encyclopedia:cluster-031-0000:0007", .category = 34U, .observations = UINT64_C(4553),
        .description = "Mixed Encyclopedia Subjects; article-title cues include Station, Metro, Railway, Mrt, Chicago, House; observed target words include Bridge (49)",
        .cluster = 103U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0225840379F, 0.0425537899F, -0.0763328150F, 0.0514321029F,
            0.00425796490F, -0.0122691616F, 0.0268031638F, -0.0677910224F,
            0.0323812254F, 0.0173942167F, -0.0667166710F, 0.0748702213F,
            -0.0402758867F, 0.0481836013F, 0.0515355766F, -0.0242794622F,
            0.00671696337F, -0.0384511091F, 0.0643223375F, -0.0253407117F,
            -0.0325625092F, 0.0644648075F, -0.0361344516F, 0.0449997410F,
            -0.0530887134F, 0.0685805231F, 0.0316306055F, 0.0684509501F,
            0.0333777592F, -0.0713889003F, 0.0362509042F, -0.0715699717F
        }
    },
    {
        .id = "encyclopedia:cluster-031-0001:0003", .category = 34U, .observations = UINT64_C(4278),
        .description = "Mixed Encyclopedia Subjects; article-title cues include Station, Metro, Railway, Mrt, Chicago, House; observed target words include United (44), North (39), Station (34)",
        .cluster = 104U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0792563260F, 0.0955647379F, -0.0657987893F, 0.0986775234F,
            0.0584434420F, -0.0649999678F, -0.0657574087F, 0.0792836249F,
            -0.0688153878F, -0.0738979802F, 0.0896418318F, 0.0674380884F,
            0.0742424875F, -0.0623828135F, -0.0796418935F, -0.0894904062F,
            -0.0595867261F, -0.0729063228F, -0.0659364834F, -0.0714187026F,
            0.0843249485F, 0.0797244236F, 0.0823966935F, 0.0658678114F,
            0.0760466978F, 0.0769696608F, 0.0886776894F, -0.0631542355F,
            -0.0749723688F, 0.0748484433F, 0.0585261285F, 0.0683331639F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 34U,
        .key = "encyclopedia/cluster-031",
        .name = "Mixed Encyclopedia Subjects: Station, Metro, Railway",
        .description = "Mixed Encyclopedia Subjects; article-title cues include Station, Metro, Railway, Mrt, Chicago, House",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_031(void) { return &module; }
