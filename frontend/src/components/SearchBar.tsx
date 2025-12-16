// src/components/SearchBar.tsx
import React from "react";

type Props = {
  query: string;
  k: number;
  loading: boolean;
  onChangeQuery: (q: string) => void;
  onChangeK: (k: number) => void;
  onSubmit: () => void;
};

export default function SearchBar({ query, k, loading, onChangeQuery, onChangeK, onSubmit }: Props) {
  return (
    <div>
      <div className="d-flex gap-2 align-items-center flex-wrap">
        <div className="input-group" style={{ flex: "1 1 420px" }}>
        <input
          className="form-control form-control-lg"
          value={query}
          onChange={(e) => onChangeQuery(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && query.trim()) {
              onSubmit();
            }
          }}
          placeholder="Search documents..."
        />
        </div>

        <div className="d-flex align-items-center gap-2">
          <span className="text-secondary small">Results</span>
          <input
            className="form-control"
            type="number"
            min={1}
            max={200}
            value={k}
            onChange={(e) => onChangeK(Number(e.target.value))}
            style={{ width: 110 }}
            aria-label="Top K"
          />
        </div>
      </div>

      {loading ? <div className="small text-secondary mt-2">Searching…</div> : null}
    </div>
  );
}
