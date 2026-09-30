import type { IncomingMessage, ServerResponse } from 'node:http';
import type { ChatCreateRequest, ChatSendRequest, ChatTrainRequest } from '../../../shared/chat.ts';
import { ChatService, ChatServiceError, chatErrorStatus, chatText } from './service.ts';

interface HttpHelpers {
    readJson<T>(request: IncomingMessage): Promise<T>;
    sendJson(response: ServerResponse, status: number, value: unknown): void;
}
/** Chat routes use one configured local owner. Remote deployments must set a bearer token. */
export async function chatRoute(service: ChatService, request: IncomingMessage, response: ServerResponse,
    segments: readonly string[], helpers: HttpHelpers): Promise<boolean> {
    const resource = segments[0];
    if (!['chat-models', 'chat-jobs', 'conversations', 'memory'].includes(resource ?? '')) return false;
    try {
        const token = process.env.CGAI_CHAT_API_TOKEN;
        if (token && request.headers.authorization !== `Bearer ${token}`) throw new ChatServiceError('unauthorized', 401);
        if (!token && !['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(request.socket.remoteAddress ?? ''))
            throw new ChatServiceError('remote chat requires CGAI_CHAT_API_TOKEN', 403);
        const method = request.method ?? 'GET';
        let value: unknown;
        let status = 200;
        const id = segments[1];
        if (resource === 'memory') {
            const memory = service.research?.memory;
            if (!memory) throw new ChatServiceError('memory unavailable', 503);
            if (segments.length === 1 && method === 'GET') value = await memory.read();
            else if (segments.length === 1 && method === 'POST') {
                const input = await helpers.readJson<{ content: string; conversationId?: string }>(request);
                if (input.conversationId) await service.conversation(input.conversationId);
                value = await memory.remember(input.content, input.conversationId ?? null); status = 201;
            } else if (id === 'settings' && segments.length === 2 && method === 'PATCH') {
                const input = await helpers.readJson<{ enabled: boolean; retentionDays: number }>(request);
                value = await memory.settings(input.enabled, input.retentionDays);
            } else if (id && segments.length === 2 && method === 'PATCH') {
                const input = await helpers.readJson<{ content?: string; disputed?: boolean }>(request);
                if (input.disputed === true) { await memory.dispute(id); value = { disputed: true }; }
                else value = await memory.remember(chatText(input.content, 'content', 4096), null, id);
            } else if (segments.length <= 2 && method === 'DELETE') { await memory.forget(id); value = { deleted: true }; }
            else throw new ChatServiceError('memory route not found', 404);
        } else if (resource === 'chat-models' && segments.length === 1 && method === 'GET') value = await service.store.listModels();
        else if (resource === 'chat-models' && id === 'train' && segments.length === 2 && method === 'POST') {
            value = await service.train(await helpers.readJson<ChatTrainRequest>(request)); status = 202;
        } else if (resource === 'chat-jobs' && id && segments.length === 2 && method === 'GET') {
            value = await service.store.getJob(id);
            if (!value) throw new ChatServiceError('training job not found', 404);
        } else if (resource === 'conversations' && segments.length === 1 && method === 'POST') {
            const input = await helpers.readJson<ChatCreateRequest>(request);
            if (!input || typeof input !== 'object' || Array.isArray(input)) throw new ChatServiceError('conversation request must be an object');
            value = await service.create(input.modelName, input.title); status = 201;
        } else if (resource === 'conversations' && id && segments.length === 2 && method === 'GET') value = await service.conversation(id);
        else if (resource === 'conversations' && id && segments.length === 2 && method === 'DELETE') {
            const input = await helpers.readJson<{ revision: number; forgetMemory?: boolean }>(request);
            await service.remove(id, input.revision, input.forgetMemory === true); value = { deleted: true };
        } else if (resource === 'conversations' && id && segments.length === 3 && segments[2] === 'messages' && method === 'POST')
            value = await service.send(id, await helpers.readJson<ChatSendRequest>(request));
        else if (resource === 'conversations' && id && segments.length === 3 && segments[2] === 'cancel' && method === 'POST') {
            const input = await helpers.readJson<{ requestId: string }>(request);
            value = await service.cancel(id, chatText(input.requestId, 'requestId', 200));
        } else throw new ChatServiceError('chat route not found', 404);
        helpers.sendJson(response, status, value);
    } catch (error) {
        helpers.sendJson(response, error instanceof SyntaxError || error instanceof TypeError ? 400 :
            error instanceof RangeError ? 413 : chatErrorStatus(error), { error: error instanceof Error ? error.message : 'chat service failed' });
    }
    return true;
}
