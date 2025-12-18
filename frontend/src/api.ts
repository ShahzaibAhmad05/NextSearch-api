// src/api.ts
import type { SearchResponse, SuggestResponse } from "./types";

// Backend defaults to http://127.0.0.1:8080 (see api_server.cpp)
// You can override with VITE_API_BASE, e.g. http://localhost:8080
const BASE = (import.meta.env.VITE_API_BASE as string | undefined) ?? "http://localhost:8080";

export async function search(query: string, k: number): Promise<SearchResponse> {
  // C++ backend expects: GET /search?q=<query>&k=<k>
  const url = new URL(`${BASE}/search`);
  url.searchParams.set("q", query);
  url.searchParams.set("k", String(k));

  const res = await fetch(url.toString(), {
    method: "GET",
    headers: { Accept: "application/json" },
  });

  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Search failed (${res.status}): ${text}`);
  }

  return (await res.json()) as SearchResponse;
}

export async function suggest(query: string, k: number, signal?: AbortSignal): Promise<SuggestResponse> {
  // C++ backend expects: GET /suggest?q=<query>&k=<k>
  const url = new URL(`${BASE}/suggest`);
  url.searchParams.set("q", query);
  url.searchParams.set("k", String(k));

  const res = await fetch(url.toString(), {
    method: "GET",
    headers: { Accept: "application/json" },
    signal,
  });

  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Suggest failed (${res.status}): ${text}`);
  }

  return (await res.json()) as SuggestResponse;
}

export async function addDocument(payload: {
  cord_root: string;
  json_relpath: string;
  cord_uid: string;
  title: string;
}): Promise<any> {
  const res = await fetch(`${BASE}/add_document`, {
    method: "POST",
    headers: { "Content-Type": "application/json", Accept: "application/json" },
    body: JSON.stringify(payload),
  });

  if (!res.ok) throw new Error(await res.text());
  return res.json();
}
