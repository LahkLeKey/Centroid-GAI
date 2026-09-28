/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: History, Government, and Conflict: Province, University, War; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-000-0003:0015", .category = 3U, .observations = UINT64_C(6850),
        .description = "History, Government, and Conflict; article-title cues include Province, University, War, Battle, District, History",
        .cluster = 9U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0146754812F, 0.0843882263F, 0.0202411339F, 0.0752613619F,
            -0.0764313340F, -0.0204304084F, -0.0642158985F, -0.0302369613F,
            -0.0718634352F, 0.0323703922F, 0.0789688677F, -0.0779709369F,
            0.0716484860F, -0.0311229937F, 0.0672782958F, -0.0647751838F,
            0.0669256598F, 0.0722074360F, -0.0265810136F, 0.0737989545F,
            0.0712526292F, 0.0236992445F, 0.0713816136F, -0.0710462108F,
            0.0786762536F, 0.0344090611F, 0.0392779633F, 0.0765258670F,
            -0.0684226528F, -0.0191142429F, 0.0254885312F, -0.0833128840F
        }
    },
    {
        .id = "encyclopedia:cluster-000-0004:0012", .category = 3U, .observations = UINT64_C(7014),
        .description = "History, Government, and Conflict; article-title cues include Province, University, War, Battle, District, History; observed target words include States (149), United (131), War (57)",
        .cluster = 10U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0598916523F, 0.100847267F, -0.0431231186F, 0.100376807F,
            0.0313278362F, -0.0381075516F, -0.0444504432F, 0.0826166868F,
            -0.0811047852F, -0.0801382810F, 0.0926728770F, 0.0714852810F,
            0.0508688129F, -0.0335625038F, -0.0752406865F, -0.0934543684F,
            -0.0228510797F, -0.0719975755F, -0.0424257591F, -0.0756690800F,
            0.0792479888F, 0.0823144391F, 0.0880270377F, 0.0508184619F,
            0.0465339497F, 0.0866914690F, 0.0895308703F, -0.0686039329F,
            -0.0426189154F, 0.0830621943F, 0.0489030369F, 0.0683348179F
        }
    },
    {
        .id = "encyclopedia:cluster-000-0006:0015", .category = 3U, .observations = UINT64_C(6097),
        .description = "History, Government, and Conflict; article-title cues include Province, University, War, Battle, District, History; observed target words include United (86), Castle (74), War (63)",
        .cluster = 11U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0763317421F, 0.0966855288F, -0.0593604483F, 0.0971880779F,
            0.0499567203F, -0.0586162806F, -0.0564803518F, 0.0780133158F,
            -0.0783130080F, -0.0761768967F, 0.0862188414F, 0.0723497346F,
            0.0709386989F, -0.0610808879F, -0.0650915951F, -0.0895625278F,
            -0.0532911271F, -0.0693633929F, -0.0611001179F, -0.0720886737F,
            0.0843436420F, 0.0800621808F, 0.0804102346F, 0.0646857992F,
            0.0665317476F, 0.0782067478F, 0.0845175683F, -0.0623755828F,
            -0.0656328201F, 0.0712092370F, 0.0638063252F, 0.0681167245F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 3U,
        .key = "encyclopedia/cluster-000",
        .name = "History, Government, and Conflict: Province, University, War",
        .description = "History, Government, and Conflict; article-title cues include Province, University, War, Battle, District, History",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_000(void) { return &module; }
