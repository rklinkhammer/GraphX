import { compareCodeUnits } from "./identity";
import type { DisplayGraph, DisplayHierarchy } from "./types";

export const PRESENTATION_PREFERENCE_KEY = "graphx.dashboard.presentation";
export const PRESENTATION_PREFERENCE_SCHEMA = 1;
export const PRESENTATION_PREFERENCE_MAX_BYTES = 65_536;
export const PRESENTATION_PREFERENCE_MAX_GROUP_IDS = 256;

export interface PresentationViewport {
  x: number;
  y: number;
  zoom: number;
}

export interface PresentationPreferences {
  schema: 1;
  graph_signature: string;
  mode: "grouped" | "raw";
  collapsed_group_ids: string[];
  semantic_expanded_group_ids: string[];
  viewport: PresentationViewport;
}

export type PreferenceReadResult =
  | { status: "absent"; value: null; message: null }
  | { status: "valid"; value: PresentationPreferences; message: null }
  | {
      status: "fallback";
      value: null;
      message: string;
      disablePersistence: boolean;
    };

const encodeString = (value: string): string => `${value.length}:${value}`;

export function canonicalGraphSignatureInput(
  graph: DisplayGraph,
  hierarchy: DisplayHierarchy,
): string {
  const nodes = graph.nodes.map((node) => node.id).sort(compareCodeUnits);
  const edges = graph.edges.map((edge) => edge.id).sort(compareCodeUnits);
  const groups =
    hierarchy.status === "valid"
      ? hierarchy.groups.map((group) => group.id).sort(compareCodeUnits)
      : [];
  return [
    `nodes:${nodes.length}`,
    ...nodes.map((id) => `n${encodeString(id)}`),
    `edges:${edges.length}`,
    ...edges.map((id) => `e${encodeString(id)}`),
    `groups:${groups.length}`,
    ...groups.map((id) => `g${encodeString(id)}`),
  ].join("|");
}

export async function graphSignature(
  graph: DisplayGraph,
  hierarchy: DisplayHierarchy,
  cryptoProvider: Crypto | undefined = globalThis.crypto,
): Promise<string> {
  if (!cryptoProvider?.subtle) {
    throw new Error("Web Crypto SHA-256 is unavailable");
  }
  const bytes = new TextEncoder().encode(
    canonicalGraphSignatureInput(graph, hierarchy),
  );
  const digest = await cryptoProvider.subtle.digest("SHA-256", bytes);
  return `sha256:${[...new Uint8Array(digest)]
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join("")}`;
}

function exactKeys(value: Record<string, unknown>, expected: string[]): boolean {
  const actual = Object.keys(value).sort(compareCodeUnits);
  return actual.length === expected.length && actual.every((key, index) => key === expected[index]);
}

function validUniqueGroupIds(
  candidate: unknown,
  knownGroupIds: ReadonlySet<string>,
): candidate is string[] {
  return (
    Array.isArray(candidate) &&
    candidate.length <= PRESENTATION_PREFERENCE_MAX_GROUP_IDS &&
    candidate.every((id) => typeof id === "string" && knownGroupIds.has(id)) &&
    new Set(candidate).size === candidate.length
  );
}

function validViewport(candidate: unknown): candidate is PresentationViewport {
  if (candidate === null || typeof candidate !== "object" || Array.isArray(candidate)) {
    return false;
  }
  const viewport = candidate as Record<string, unknown>;
  return (
    exactKeys(viewport, ["x", "y", "zoom"]) &&
    typeof viewport.x === "number" &&
    Number.isFinite(viewport.x) &&
    viewport.x >= -10_000_000 &&
    viewport.x <= 10_000_000 &&
    typeof viewport.y === "number" &&
    Number.isFinite(viewport.y) &&
    viewport.y >= -10_000_000 &&
    viewport.y <= 10_000_000 &&
    typeof viewport.zoom === "number" &&
    Number.isFinite(viewport.zoom) &&
    viewport.zoom >= 0.1 &&
    viewport.zoom <= 4
  );
}

export function decodePresentationPreferences(
  serialized: string,
  expectedSignature: string,
  knownGroupIds: ReadonlySet<string>,
): PresentationPreferences | null {
  if (new TextEncoder().encode(serialized).byteLength > PRESENTATION_PREFERENCE_MAX_BYTES) {
    return null;
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(serialized) as unknown;
  } catch {
    return null;
  }
  if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
    return null;
  }
  const value = parsed as Record<string, unknown>;
  if (
    !exactKeys(value, [
      "collapsed_group_ids",
      "graph_signature",
      "mode",
      "schema",
      "semantic_expanded_group_ids",
      "viewport",
    ]) ||
    value.schema !== PRESENTATION_PREFERENCE_SCHEMA ||
    value.graph_signature !== expectedSignature ||
    (value.mode !== "grouped" && value.mode !== "raw") ||
    !validUniqueGroupIds(value.collapsed_group_ids, knownGroupIds) ||
    !validUniqueGroupIds(value.semantic_expanded_group_ids, knownGroupIds) ||
    !validViewport(value.viewport)
  ) {
    return null;
  }
  return value as unknown as PresentationPreferences;
}

export function readPresentationPreferences(
  storage: Pick<Storage, "getItem">,
  expectedSignature: string,
  knownGroupIds: ReadonlySet<string>,
): PreferenceReadResult {
  let serialized: string | null;
  try {
    serialized = storage.getItem(PRESENTATION_PREFERENCE_KEY);
  } catch {
    return {
      status: "fallback",
      value: null,
      message: "View preferences are unavailable; deterministic defaults are active.",
      disablePersistence: true,
    };
  }
  if (serialized === null) {
    return { status: "absent", value: null, message: null };
  }
  const value = decodePresentationPreferences(
    serialized,
    expectedSignature,
    knownGroupIds,
  );
  return value
    ? { status: "valid", value, message: null }
    : {
        status: "fallback",
        value: null,
        message: "Saved view preferences were invalid or for another graph; deterministic defaults are active.",
        disablePersistence: false,
      };
}

export function encodePresentationPreferences(
  value: PresentationPreferences,
): string {
  return JSON.stringify(value);
}

export function writePresentationPreferences(
  storage: Pick<Storage, "setItem">,
  value: PresentationPreferences,
): string | null {
  const serialized = encodePresentationPreferences(value);
  if (new TextEncoder().encode(serialized).byteLength > PRESENTATION_PREFERENCE_MAX_BYTES) {
    return "View preferences exceed the local size bound; deterministic defaults remain active.";
  }
  try {
    storage.setItem(PRESENTATION_PREFERENCE_KEY, serialized);
    return null;
  } catch {
    return "View preferences could not be saved locally; graph inspection is unaffected.";
  }
}

export function removePresentationPreferences(
  storage: Pick<Storage, "removeItem">,
): string | null {
  try {
    storage.removeItem(PRESENTATION_PREFERENCE_KEY);
    return null;
  } catch {
    return "Saved view preferences could not be removed; deterministic defaults are active for this page.";
  }
}
