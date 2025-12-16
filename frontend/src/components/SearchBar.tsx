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

export default function SearchBar(props: Props) {
  const { query, k, loading, onChangeQuery, onChangeK, onSubmit } = props;

  return (
    <div className="d-flex gap-2 align-items-center flex-wrap">
      <input
        className="form-control"
        value={query}
        onChange={(e) => onChangeQuery(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === "Enter") onSubmit();
        }}
        placeholder="Search..."
        style={{ flex: "1 1 420px" }}
      />

      <div className="d-flex align-items-center gap-2">
        <span className="text-secondary small">Top K</span>
        <input
          className="form-control"
          type="number"
          min={1}
          max={200}
          value={k}
          onChange={(e) => onChangeK(Number(e.target.value))}
          style={{ width: 110 }}
        />
      </div>

      <button
        className={`btn ${loading ? "btn-secondary" : "btn-dark"}`}
        onClick={onSubmit}
        disabled={loading || query.trim().length === 0}
      >
        {loading ? "Searching..." : "Search"}
      </button>
    </div>
  );
}
