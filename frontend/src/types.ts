// src/types.ts
export type SearchResult = {
  docId: number;
  score: number;
  title: string;
  segment: string;
  cord_uid: string;
  json_relpath: string;
  url?: string;
  publish_time?: string;
};

export type SearchResponse = {
  query: string;
  k?: number;
  segments?: number;
  search_time_ms?: number;
  total_time_ms?: number;
  found?: number;
  results: SearchResult[];
};
