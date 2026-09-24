interface MetricBarProps {
    label: string;
    value: number;
    max: number;
    display?: string;
    color?: string;
}

/** Horizontal proportion bar used to compare a numeric model metric against the cohort max. */
export function MetricBar({ label, value, max, display, color = 'bg-indigo-500' }: MetricBarProps) {
    const pct = max > 0 ? Math.min(100, Math.round((value / max) * 100)) : 0;
    return (
        <div>
            <div className="mb-1 flex items-baseline justify-between text-xs">
                <span className="text-slate-400">{label}</span>
                <span className="font-mono text-slate-200">{display ?? value.toLocaleString()}</span>
            </div>
            <div className="h-1.5 w-full overflow-hidden rounded-full bg-slate-800">
                <div className={`h-full rounded-full ${color}`} style={{ width: `${pct}%` }} />
            </div>
        </div>
    );
}
