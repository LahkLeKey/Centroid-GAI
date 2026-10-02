/** Inert evidence for worker lifecycle tests; no external provider or factuality assertions. */
import type { ChatSendRequest } from '../../../shared/chat.ts';
import { ChatService } from './service.ts';
import type { ChatStore } from './store.ts';
import type { ChatWorkers } from './workers.ts';
import { ResearchService } from './research.ts';
import { MemoryService } from './memory.ts';

export class NeuralFixtureService extends ChatService {
    constructor(store: ChatStore, workers: ChatWorkers, owner: string, research?: ResearchService) {
        const evidence = research ?? new ResearchService(new MemoryService(store, owner));
        if (!research) evidence.answer = async input => ({
            content: 'Fixture source excerpt.', memoryIds: [],
            sources: [{ id: 'fixture-source', path: 'fixture.txt', excerpt: `${input.content} has fixture evidence.` }],
            research: { status: 'none', mode: 'sources', reason: 'inert lifecycle fixture', provider: null,
                queries: 0, fetched: 0, elapsedMs: 0 },
        });
        super(store, workers, owner, evidence);
    }
    override send(id: string, input: ChatSendRequest) {
        return super.send(id, { ...input, answerMode: input.answerMode ?? 'neural' });
    }
}

export const fixtureValidation = {
    version: 1 as const, cases: [
        { id: 'heldout-answer', messages: [{ role: 'user' as const, content: 'Which evidence is available?' }],
            expected: 'answer' as const, acceptedAnswers: ['A reviewed passage is available.'],
            evidence: [{ id: 'heldout-source', excerpt: 'A reviewed passage is available.' }] },
        { id: 'heldout-absent', messages: [{ role: 'user' as const, content: 'Which unsupported fact is available?' }],
            expected: 'abstain' as const, acceptedAnswers: ['I do not have evidence.'] },
    ],
};
