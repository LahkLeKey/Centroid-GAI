# Centroid neural network

The working neural prototype learns token embeddings, an ordered context encoder,
centroid positions, and token distributions through backpropagation. It predicts
the next token from a fixed window, then feeds generated tokens back into that
window. The separate [chat service](chat-service.md) adds structural conversation
training and persistent question conditioning to this learning core.

The implementation uses a feed-forward encoder with soft centroid routing.
There is no transformer or attention layer in this architecture. The existing
HTTP model endpoints use the separate count-based engine; they do not serve
`.cgnn` models. Follow the [chatbot roadmap](chatbot-plan.md) for the remaining
work and [API contract](api-contract.md) for the available HTTP behavior.

## Run the neural experiment

Build the native CLI using [development instructions](development.md), then run
these commands from the repository root with a single-configuration build:

```sh
./build/dev/cgai neural-train examples/neural/train.txt build/dev/order.cgnn 60 0.015 examples/neural/heldout.txt
./build/dev/cgai neural-evaluate build/dev/order.cgnn examples/neural/heldout.txt
./build/dev/cgai neural-generate build/dev/order.cgnn "a b" 18 0 42
```

For a Visual Studio build, use `.\build\dev\Release\cgai.exe` as the executable.
The destination directory must already exist. Training creates a fresh model
and replaces the destination artifact when saving succeeds.

```text
neural-train <train.txt> <model.cgnn> [epochs] [learning-rate] [validation.txt]
neural-evaluate <model.cgnn> <heldout.txt>
neural-generate <model.cgnn> <prompt> [max-tokens] [temperature] [seed]
```

The example corpus repeats an artificial rule: `a b` precedes `left`, and `b a`
precedes `right`. The held-out file uses the same rules with a different starting
phase and length. It exercises learning, token order, evaluation, and artifact
loading; it is not a conversational or factual knowledge benchmark.

Training prints metrics before and after optimization for the training text and
optional validation text. Evaluation reports target count, mean cross-entropy
in natural units, perplexity, greedy accuracy, and unknown-token count. Each
sequence includes one final end-of-sequence (EOS) target. Compare held-out
results across runs using the same split and tokenization. A lower training loss
alone does not establish useful answer quality.

## Configuration

The CLI uses `cgai_neural_default_config()` and exposes only the positional
options shown above. Custom network shapes require the
[C API](../include/centroid_gai_neural.h).

| Setting | Default | Accepted range |
| --- | --- | --- |
| Embedding dimensions `D` | 8 | 1–64 |
| Hidden dimensions `H` | 16 | 1–128 |
| Centroid count `K` | 16 | 1–128 |
| Context window `W` | 4 tokens | 1–256 |
| Initialization/shuffle seed | 42 | `uint64_t` |
| Routing temperature | 1.0 | 0.01–100 |
| Training epochs | 20 | 1–10,000 |
| Adam learning rate | 0.01 | Greater than 0, at most 1 |
| Global gradient norm limit | 5.0 | Greater than 0, at most 1,000 |

Generation defaults to 40 tokens, sampling temperature 0.8, and the model seed.
Its temperature is separate from routing temperature: zero chooses the most
probable token; positive values rescale the final output distribution. Seed zero
uses the stored model seed. Generation stops at EOS or the token limit and
returns only the continuation. The CLI rejects a worst-case output allocation
above 64 MiB even if early EOS could produce a shorter response.

## What the network learns

For vocabulary size `V`, the five parameter groups are embeddings `E[V,D]`,
encoder weights `A[H,W*D]`, encoder bias `b[H]`, centroid coordinates `C[K,H]`,
and centroid-specific output logits `L[K,V]`.

For each target, collect the preceding `W` tokens in chronological order,
left-padding with beginning-of-sequence (BOS) tokens when needed:

```text
x = concat(E[context[0]], ..., E[context[W-1]])
h = tanh(A x + b)
g[k] = softmax_k(-sum_j((h[j] - C[k,j])^2) / routing_temperature)
q[k,v] = softmax_v(L[k,v])     # BOS is excluded from each output row
p[v] = sum_k(g[k] * q[k,v])
loss = -log(p[target])
```

The encoder preserves position because each slot occupies a different part of
`x`. Distance to each centroid determines its routing weight `g[k]`. Each
centroid contributes a normalized token distribution `q[k,v]`; the final
prediction mixes these probabilities. BOS is context-only. EOS is a learned
output that ends generation. Log-space calculations stabilize loss and gradients.

Training updates all five parameter groups with clipped Adam, one target at a
time. It shuffles target indices while keeping each target's original preceding
tokens intact. Repeated tokens accumulate embedding gradients from every context
position. Each training call creates fresh optimizer moments; continuing after
save/load does not reproduce uninterrupted optimizer training.

The parameter count is `P = V*D + H*W*D + H + K*H + K*V`. Weights occupy `8*P`
bytes on supported double-precision builds. Training additionally allocates
three `P`-double arrays for gradients and Adam moments, plus temporary buffers.
Output computation visits all centroid/vocabulary pairs. There is no GPU path,
minibatching, or sparse optimizer, so larger vocabulary and shape settings can
be expensive even within the accepted limits.

## Vocabulary and context boundaries

Vocabulary creation uses training text only and reserves BOS, EOS, and unknown
(UNK) IDs. Later training, evaluation, and generation keep that vocabulary fixed;
unseen words map to UNK. A large unknown-token count means the model cannot
distinguish many original spellings. Supplying validation text to the CLI does
not add its words to the vocabulary or update weights from it.

The shared tokenizer lowercases through C character conversion, groups word
bytes and apostrophes, and separates punctuation. It preserves ordinary UTF-8
bytes in the default C locale but does not perform full Unicode case folding,
Unicode word segmentation, or subword tokenization. Generation cannot preserve
original capitalization or arbitrary whitespace.

A training file is one continuous sequence. Newlines do not introduce dialogue
boundaries or reset context, and EOS is appended once at the end. During
generation, the prompt enters the same rolling window as generated tokens. Once
enough output has been emitted, prompt tokens leave that window. The implemented
model has no separate persistent prompt, roles, or conversation state.

## C ownership and model files

Use `cgai_neural_create`, `cgai_neural_train`, `cgai_neural_evaluate`, and
`cgai_neural_generate`, then release the owned handle with `cgai_neural_destroy`.
Generation writes to a caller-owned bounded output buffer. Failures return
`NULL` or `CGAI_STATUS_ERROR`; retrieve details with `cgai_last_error()`.
Training requires exclusive access and retains updates already made if a later
step fails. Immutable models support concurrent evaluation, generation, and
saving while callers retain the model and use separate result buffers.

`cgai_neural_save` and `cgai_neural_load` use the `.cgnn` neural artifact with
`CGAINN1` magic and version 1. It contains shape, seed, routing temperature,
vocabulary, and weights. Numeric fields use native representation: use trusted
artifacts on the same architecture. Loading validates shapes, vocabulary,
finite parameters, and exact file length. Saving replaces the destination
directly and is not atomic. Optimizer state is not stored. This format is
independent of the baseline `.cgai` format and its merge operations.

Limits enforced by the neural implementation include 8,192 vocabulary entries
including controls, two million scalar parameters, one million text tokens per
operation before final EOS, 16 MiB per input text, 1 MiB per token spelling,
and 64 MiB per artifact. Generation accepts 0–1,000,000 output tokens and a
sampling temperature of 0–100. Accepted dimensions must also fit the total
parameter bound; the maximum of every dimension cannot always be used together.

## From continuation to chatbot

The [chat header](../include/centroid_gai_chat.h) now has implementations for
persistent prompt slots, rolling answer slots, structural roles, independent
dialogue examples, assistant-only training and the bounded `.cgchat` codec.
One Adam optimizer spans every example and epoch in a training call; another
call starts fresh moments. No new mathematical parameter groups were introduced:
the existing encoder consumes the two separately padded windows.

Chat training holds BOS padding embeddings fixed at zero for newly created models.
Centroid output rows start with different token preferences, then learn all output
logits through Adam. This avoids the padding saturation and expert collapse seen
with the prototype's initialization on the two-dialogue demo. Both supplied demo
answers now generate exactly with EOS; this is a training correctness check.
The held-out six-case dialogue fixture still scores 0/6 exact answers.

Chat generation also stops persistent repetition and reports `repetition` separately
from EOS and the token limit. These chat policies leave the standalone continuation
prototype's initialization and generation behavior unchanged.

The [chat service](chat-service.md) connects these artifacts to workers, HTTP
and PostgreSQL. Source mode returns original excerpts independently from generated
wording. Compiled knowledge does not automatically become neural weights.
The [roadmap](chatbot-plan.md) retains unmet quality and release acceptance gates.

The native neural tests cover analytical gradients against finite differences,
causal context, ordered tokens, learned parameter updates, frozen vocabulary,
determinism, held-out learning, and artifact validation. CLI tests cover training,
evaluation, generation, and argument errors. Run the neural checks with
`ctest --test-dir build/dev -C Release -R centroid_gai_neural --output-on-failure`;
use [development instructions](development.md) for the full check suite.
