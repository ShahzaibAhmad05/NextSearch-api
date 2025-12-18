// src/components/SearchBar.tsx
import React, { useEffect, useMemo, useRef, useState } from "react";
import { Search } from "lucide-react";
import { suggest as apiSuggest } from "../api";

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
  const [suggestions, setSuggestions] = useState<string[]>([]);
  const [open, setOpen] = useState(false);
  const [activeIdx, setActiveIdx] = useState<number>(-1);

  const abortRef = useRef<AbortController | null>(null);
  const blurTimerRef = useRef<number | null>(null);

  const trimmed = useMemo(() => query.trim(), [query]);

  useEffect(() => {
    // Only suggest when there is a meaningful prefix
    if (trimmed.length < 2) {
      abortRef.current?.abort();
      setSuggestions([]);
      setOpen(false);
      setActiveIdx(-1);
      return;
    }

    const t = window.setTimeout(async () => {
      abortRef.current?.abort();
      const controller = new AbortController();
      abortRef.current = controller;

      try {
        const res = await apiSuggest(query, 5, controller.signal);
        const s = Array.isArray(res.suggestions) ? res.suggestions.slice(0, 5) : [];
        setSuggestions(s);
        setOpen(s.length > 0);
        setActiveIdx(-1);
      } catch (e: any) {
        // Ignore aborts; suppress other errors quietly (no UI changes requested)
        if (e?.name === "AbortError") return;
        setSuggestions([]);
        setOpen(false);
        setActiveIdx(-1);
      }
    }, 180);

    return () => window.clearTimeout(t);
  }, [query, trimmed]);

  useEffect(() => {
    return () => {
      abortRef.current?.abort();
      if (blurTimerRef.current != null) window.clearTimeout(blurTimerRef.current);
    };
  }, []);

  function pickSuggestion(value: string) {
    onChangeQuery(value);
    setOpen(false);
    setActiveIdx(-1);
    // Trigger search immediately on pick
    onSubmit();
  }

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
              padding: "0.7rem 1.3rem 0.7rem 3.2rem",
              borderRadius: "20px",
            }}
            value={query}
            onChange={(e) => onChangeQuery(e.target.value)}
            onFocus={() => {
              if (suggestions.length > 0) setOpen(true);
            }}
            onBlur={() => {
              // Delay closing so click events on suggestions can fire.
              if (blurTimerRef.current != null) window.clearTimeout(blurTimerRef.current);
              blurTimerRef.current = window.setTimeout(() => setOpen(false), 120);
            }}
            onKeyDown={(e) => {
              if (e.key === "ArrowDown") {
                if (suggestions.length === 0) return;
                e.preventDefault();
                setOpen(true);
                setActiveIdx((prev) => {
                  const next = prev + 1;
                  return next >= suggestions.length ? 0 : next;
                });
                return;
              }

              if (e.key === "ArrowUp") {
                if (suggestions.length === 0) return;
                e.preventDefault();
                setOpen(true);
                setActiveIdx((prev) => {
                  const next = prev - 1;
                  return next < 0 ? suggestions.length - 1 : next;
                });
                return;
              }

              if (e.key === "Escape") {
                if (open) {
                  e.preventDefault();
                  setOpen(false);
                  setActiveIdx(-1);
                }
                return;
              }

              if (e.key === "Enter") {
                if (open && activeIdx >= 0 && activeIdx < suggestions.length) {
                  e.preventDefault();
                  pickSuggestion(suggestions[activeIdx]);
                  return;
                }
                if (query.trim()) {
                  onSubmit();
                }
              }
            }}
            placeholder="Search documents..."
          />

          {open && suggestions.length > 0 ? (
            <div
              style={{
                position: "absolute",
                left: 0,
                right: 0,
                top: "calc(100% + 8px)",
                background: "#fff",
                border: "1px solid #e6e6e6",
                borderRadius: "14px",
                boxShadow: "0 10px 30px rgba(0,0,0,0.08)",
                overflow: "hidden",
                zIndex: 20,
              }}
            >
              {suggestions.map((s, idx) => (
                <div
                  key={s + idx}
                  className="small"
                  style={{
                    padding: "10px 14px",
                    cursor: "pointer",
                    background: idx === activeIdx ? "#f2f4f7" : "#fff",
                  }}
                  onMouseDown={(ev) => {
                    // Prevent input blur before we pick
                    ev.preventDefault();
                  }}
                  onClick={() => pickSuggestion(s)}
                  onMouseEnter={() => setActiveIdx(idx)}
                >
                  {s}
                </div>
              ))}
            </div>
          ) : null}
        </div>
      </div>

      {loading ? <div className="small text-secondary mt-2">Searching…</div> : null}
    </div>
  );
}
