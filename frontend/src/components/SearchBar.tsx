// src/components/SearchBar.tsx
import React from "react";
import { Search } from "lucide-react";

type Props = {
  query: string;
  k: number;
  loading: boolean;
  onChangeQuery: (q: string) => void;
  onChangeK: (k: number) => void;
  onSubmit: () => void;
};

export default function SearchBar({
  query,
  k,
  loading,
  onChangeQuery,
  onChangeK,
  onSubmit,
}: Props) {
  return (
    <div>
      <div className="d-flex gap-2 align-items-center flex-wrap">
        {/* wrapper must be relative */}
        <div style={{ position: "relative", flex: "1 1 420px" }}>
          {/* icon inside input */}
          <Search
            size={22}
            className="text-secondary"
            style={{
              position: "absolute",
              left: 18,
              top: "50%",
              transform: "translateY(-50%)",
              pointerEvents: "none",
            }}
          />

          <input
            className="form-control form-control-lg"
            style={{
              padding: "0.9rem 1.3rem 0.9rem 3.2rem", 
              borderRadius: "20px",
            }}
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
      </div>

      {loading ? (
        <div className="small text-secondary mt-2">Searching…</div>
      ) : null}
    </div>
  );
}
