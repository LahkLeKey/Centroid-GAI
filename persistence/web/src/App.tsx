import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  ApiError,
  deleteModel,
  getHealth,
  listModels,
  type HealthResult,
  type ModelMetadata,
} from './api/client';
import { Sidebar } from './components/Sidebar';
import { ChatPlayground } from './components/ChatPlayground';
import { ModelInspector, type InspectorView } from './components/ModelInspector';
import { NewModelModal } from './components/NewModelModal';
import { hasLabelDraft, useModelLabels, type LabelDraft } from './modelLabels';

type Tab = 'chat' | 'inspector';

function App() {
  const [models, setModels] = useState<ModelMetadata[]>([]);
  const [loading, setLoading] = useState(true);
  const [selectedName, setSelectedName] = useState<string | null>(null);
  const [health, setHealth] = useState<HealthResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [tab, setTab] = useState<Tab>('chat');
  const [inspectorView, setInspectorView] = useState<InspectorView>('overview');
  const [modalOpen, setModalOpen] = useState(false);
  const { labels, saveLabels, storageError } = useModelLabels();
  const [labelDrafts, setLabelDrafts] = useState<Record<string, LabelDraft>>({});
  const unsavedModels = models.filter((model) => hasLabelDraft(labelDrafts[model.id]));
  const labelSuggestions = [...new Set(models.flatMap((model) => labels[model.id] ?? []))].sort();

  const refresh = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const data = await listModels();
      setModels(data);
      setSelectedName((current) =>
        current && data.some((model) => model.name === current) ? current : (data[0]?.name ?? null),
      );
    } catch (cause) {
      setError(cause instanceof ApiError ? cause.message : 'Unable to reach the persistence API.');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
    getHealth()
      .then(setHealth)
      .catch(() => setHealth(null));
  }, [refresh]);

  useEffect(() => {
    if (!unsavedModels.length) return;
    const warnBeforeLeaving = (event: BeforeUnloadEvent) => {
      event.preventDefault();
      event.returnValue = '';
    };
    window.addEventListener('beforeunload', warnBeforeLeaving);
    return () => window.removeEventListener('beforeunload', warnBeforeLeaving);
  }, [unsavedModels.length]);

  async function handleDelete(name: string) {
    await deleteModel(name);
    await refresh();
  }

  const selectedModel = useMemo(
    () => models.find((model) => model.name === selectedName) ?? null,
    [models, selectedName],
  );
  const nextUnlabeled = models.find((model) => model.id !== selectedModel?.id && !labels[model.id]?.length);

  return (
    <div className="flex h-dvh flex-col overflow-hidden bg-slate-950 text-slate-200">
      <header className="flex shrink-0 items-center justify-between border-b border-slate-800 px-5 py-3">
        <div className="flex items-center gap-2">
          <div className="flex h-7 w-7 items-center justify-center rounded-md bg-indigo-600 font-mono text-xs font-bold text-white">
            C
          </div>
          <div>
            <h1 className="text-sm font-semibold text-slate-100">Centroid-GAI</h1>
            <p className="text-[11px] text-slate-500">Persistence &amp; model playground</p>
          </div>
        </div>
        <div className="flex items-center gap-2 text-xs">
          <span className={`h-2 w-2 rounded-full ${health?.status === 'ok' ? 'bg-emerald-500' : 'bg-rose-500'}`} />
          <span className="text-slate-500">
            {health?.status === 'ok' ? `${health.database} · native ok` : 'API unreachable'}
          </span>
        </div>
      </header>

      <div className="flex min-h-0 flex-1 flex-col md:flex-row">
        <Sidebar
          models={models}
          loading={loading}
          selectedName={selectedName}
          onSelect={(name) => {
            setSelectedName(name);
          }}
          onDelete={handleDelete}
          onRefresh={refresh}
          onNewModel={() => setModalOpen(true)}
          labels={labels}
          onEditLabels={(name) => { setSelectedName(name); setTab('inspector'); setInspectorView('overview'); }}
        />

        <main className="flex min-h-0 min-w-0 flex-1 flex-col p-4">
          {error && (
            <div className="mb-4 rounded-md border border-rose-900/50 bg-rose-950/40 px-4 py-2 text-sm text-rose-300">
              {error}
            </div>
          )}
          {storageError && <p role="alert" className="mb-3 rounded-md border border-amber-500/30 bg-amber-500/10 p-3 text-xs text-amber-200">{storageError}</p>}

          <div className="mb-4 flex shrink-0 gap-1">
            {(['chat', 'inspector'] as const).map((value) => (
              <button
                key={value}
                type="button"
                onClick={() => setTab(value)}
                aria-pressed={tab === value}
                className={`rounded-md px-3 py-1.5 text-sm font-medium ${tab === value ? 'bg-slate-800 text-slate-100' : 'text-slate-500 hover:text-slate-300'
                  }`}
              >
                {value === 'chat' ? 'Chat' : 'Inspector'}
              </button>
            ))}
          </div>

          {unsavedModels.length > 0 && (
            <div className="mb-3 flex max-h-24 shrink-0 flex-wrap items-center gap-2 overflow-y-auto rounded-md border border-amber-500/20 bg-amber-500/5 p-2 text-xs text-amber-200">
              <span>Unsaved label drafts:</span>
              {unsavedModels.map((model) => <button key={model.id} type="button" onClick={() => { setSelectedName(model.name); setTab('inspector'); setInspectorView('overview'); }}
                className="max-w-full rounded border border-amber-400/30 px-2 py-1 break-all hover:bg-amber-500/10">Resume {model.name}</button>)}
            </div>
          )}

          <div className="min-h-0 flex-1">
            {tab === 'chat' ? (
              <ChatPlayground key={selectedName} modelName={selectedName} />
            ) : (
              <ModelInspector model={selectedModel} cohort={models} onSelect={setSelectedName}
                onRefresh={refresh} onRun={() => setTab('chat')}
                view={inspectorView} onViewChange={setInspectorView}
                onCreated={(name) => void refresh().then(() => setSelectedName(name))}
                labels={selectedModel ? labels[selectedModel.id] ?? [] : []} labelSuggestions={labelSuggestions}
                allLabels={labels}
                labelDraft={selectedModel ? labelDrafts[selectedModel.id] : undefined}
                onLabelDraftChange={(draft) => {
                  if (!selectedModel) return;
                  setLabelDrafts((current) => {
                    const next = { ...current };
                    if (draft) next[selectedModel.id] = draft;
                    else delete next[selectedModel.id];
                    return next;
                  });
                }}
                onNextUnlabeled={nextUnlabeled ? () => setSelectedName(nextUnlabeled.name) : undefined}
                onSaveLabels={(values) => selectedModel ? saveLabels(selectedModel.id, values, labelDrafts[selectedModel.id]?.baseline ?? labels[selectedModel.id] ?? []) : 'Select a model first.'} />
            )}
          </div>
        </main>
      </div>

      <NewModelModal
        open={modalOpen}
        onClose={() => setModalOpen(false)}
        onCreated={(name) => void refresh().then(() => setSelectedName(name))}
      />
    </div>
  );
}

export default App;
