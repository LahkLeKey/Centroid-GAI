# Hugging Face import: why not, and what we do instead

## Constraint

CGAI artifacts store hashed token embeddings, online k-means centroids, and
per-centroid next-token frequency tables (see
[model-composition.md](model-composition.md)). Pretrained transformer weights
(attention matrices, layer norms, learned embeddings) have no structural
counterpart in that format. This is architectural, not an authentication
problem: downloading a public Hugging Face repository's `model.safetensors`
without a token is just as unusable for CGAI as downloading it with one.
There is no code path, present or planned, that reads transformer weights and
emits centroids.

A token-gated "teacher" pipeline (call a hosted Hugging Face model, train on
its output) was prototyped and then dropped. It added an external service
dependency, an API token requirement, and third-party licensing review for
every model, none of which serve the actual goal: a bigger, hand-owned
vocabulary and a more human-feeling baseline chatbot. That work is not
present in this repository.

## Decision: hand-authored, composable native corpora

Instead of importing anything, the baseline grows through more, and more
varied, hand-written corpora trained natively and combined with the existing
merge tooling:

```
Domain-scoped corpus files (examples/model_corpora/*.txt)
  -> trainNativeModel(text, config) per domain, same as any example model
  -> POST /api/v1/models/:name/merge combines them into one composed model
  -> baseline harness (persistence/api/src/evaluation) scores every model,
     including the merged one, against recall/challenge/unknown probes
```

This uses only capability that already exists (`cgai_model_merge`,
`createComposedArtifact`, the baseline suite); no new C, ABI, or HTTP surface
is required. What changes is content: more corpora, in more domains, so the
merged vocabulary and centroid coverage stop being smoke-test-sized.

### Domains added

- `conversational-patterns`: greetings, small talk, questions and answers,
  and polite requests, so the merged model has some grounding in ordinary
  turn-taking instead of only describing its own internals.
- `worldbuilding-vocabulary`: original terms for places, factions, characters,
  and systems of magic or technology, aimed at creative worldbuilding use
  rather than any existing fictional property.
- `general-vocabulary`: everyday nouns, verbs, and descriptive sentences
  outside both the tooling and worldbuilding domains, to broaden token
  coverage rather than deepen one topic.

Each is committed under `examples/model_corpora/`, trained by
`persistence/e2e/seed-models.ts` alongside the original three corpora, and
merged into a single `starter-chatbot` composed model so there is one
artifact that represents "the current best combined baseline" rather than
several disconnected small ones.

### What stays out of scope

- No transformer inference, embedded or hosted, anywhere in the pipeline.
- No scraping of third-party model weights or datasets; corpora are original
  and hand-written.
- No attempt to map attention/embedding weights onto centroids or token
  embeddings; CGAI's hashed embeddings are seed-derived, not learned.

### Next steps

Vocabulary breadth is still limited by how much original text is hand-written
and committed. Growing it further means adding more domain corpora the same
way, not a different mechanism. The baseline harness (`bun run baseline`)
is the way to tell whether a new corpus, or the merged superset, actually
improved recall/challenge scores rather than just adding more tokens with no
measurable effect.

