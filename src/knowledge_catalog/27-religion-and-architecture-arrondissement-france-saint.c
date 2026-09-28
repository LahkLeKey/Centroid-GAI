/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Religion and Architecture: Arrondissement, France, Saint; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-024-0001:0003", .category = 27U, .observations = UINT64_C(4723),
        .description = "Religion and Architecture; article-title cues include Arrondissement, France, Saint, Cathedral, Sur, Pierre; observed target words include Team (64), Tour (40)",
        .cluster = 81U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0631051287F, 0.101033144F, -0.0451018550F, 0.0971405432F,
            0.0455759428F, -0.0483955890F, -0.0477717184F, 0.0738099143F,
            -0.0756563991F, -0.0795987397F, 0.0913515985F, 0.0731236115F,
            0.0580771752F, -0.0425815620F, -0.0786755383F, -0.0894425735F,
            -0.0388138480F, -0.0758310556F, -0.0511653088F, -0.0743088648F,
            0.0846766457F, 0.0877331942F, 0.0842898712F, 0.0539350510F,
            0.0544964671F, 0.0816698447F, 0.0946576893F, -0.0721253157F,
            -0.0584265292F, 0.0784009323F, 0.0522133298F, 0.0796485767F
        }
    },
    {
        .id = "encyclopedia:cluster-024-0001:0015", .category = 27U, .observations = UINT64_C(5734),
        .description = "Religion and Architecture; article-title cues include Arrondissement, France, Saint, Cathedral, Sur, Pierre",
        .cluster = 82U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0629950315F, 0.0764882192F, -0.0338302925F, 0.0612378567F,
            -0.0599635132F, 0.0558323115F, -0.0607341602F, 0.0350737199F,
            -0.0647421628F, -0.0675064921F, 0.0755119324F, -0.0663451999F,
            0.0599019118F, 0.0378792211F, 0.0355258845F, -0.0554623678F,
            0.0516086295F, 0.0614741482F, 0.0537873581F, 0.0562330596F,
            0.0583808757F, -0.0536537543F, 0.0712985396F, -0.0539209135F,
            0.0417021699F, -0.0292264409F, -0.0166685246F, 0.0367077217F,
            -0.0501288585F, 0.0593880825F, -0.0337069295F, -0.0585659072F
        }
    },
    {
        .id = "encyclopedia:cluster-024-0003:0005", .category = 27U, .observations = UINT64_C(4981),
        .description = "Religion and Architecture; article-title cues include Arrondissement, France, Saint, Cathedral, Sur, Pierre; observed target words include References (116)",
        .cluster = 83U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0711224228F, 0.0754048675F, -0.0368033387F, 0.0576834418F,
            -0.0656450614F, 0.0676680803F, -0.0701168850F, 0.0268069431F,
            -0.0519221835F, -0.0775697380F, 0.0728141665F, -0.0712525919F,
            0.0682121813F, 0.0366850160F, 0.0347212851F, -0.0406717807F,
            0.0562993400F, 0.0773803815F, 0.0646751076F, 0.0800423473F,
            0.0366731621F, -0.0763984695F, 0.0694188401F, -0.0460899882F,
            0.0343663022F, -0.0378325023F, -0.0316690616F, 0.0417246595F,
            -0.0347685665F, 0.0566305704F, -0.0577424876F, -0.0608656518F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 27U,
        .key = "encyclopedia/cluster-024",
        .name = "Religion and Architecture: Arrondissement, France, Saint",
        .description = "Religion and Architecture; article-title cues include Arrondissement, France, Saint, Cathedral, Sur, Pierre",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_024(void) { return &module; }
