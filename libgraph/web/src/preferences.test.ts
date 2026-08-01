import { webcrypto } from "node:crypto";
import { describe, expect, test, vi } from "vitest";

import { adaptGraphDocument } from "./adapter";
import { adaptPresentationGroups } from "./hierarchy";
import {
  canonicalGraphSignatureInput,
  decodePresentationPreferences,
  encodePresentationPreferences,
  graphSignature,
  PRESENTATION_PREFERENCE_KEY,
  PRESENTATION_PREFERENCE_MAX_BYTES,
  readPresentationPreferences,
  removePresentationPreferences,
  writePresentationPreferences,
  type PresentationPreferences,
} from "./preferences";

const document = {
  nodes: [
    { id: "b", type: "Sink", node_config: { mutable: 1 } },
    { id: "a", type: "Source", node_config: { mutable: 2 } },
  ],
  edges: [
    { source_node_id: "a", source_port: 0, target_node_id: "b", target_port_name: "0" },
  ],
  presentation: {
    groups: [
      {
        id: "g",
        label: "Group",
        members: ["a"],
        layout: "grid",
        collapsed_by_default: false,
      },
    ],
  },
};

function model() {
  const graph = adaptGraphDocument(document);
  return { graph, hierarchy: adaptPresentationGroups(graph) };
}

function record(signature = "sha256:abc"): PresentationPreferences {
  return {
    schema: 1,
    graph_signature: signature,
    mode: "grouped",
    collapsed_group_ids: ["g"],
    semantic_expanded_group_ids: ["g"],
    viewport: { x: -25, y: 50, zoom: 1.5 },
  };
}

describe("bounded local presentation preferences", () => {
  test("canonicalizes structural identities and excludes mutable configuration", async () => {
    const first = model();
    expect(canonicalGraphSignatureInput(first.graph, first.hierarchy)).toBe(
      "nodes:2|n1:a|n1:b|edges:1|e29:edge|1:a|index:0|1:b|name:1:0|groups:1|g1:g",
    );
    const signature = await graphSignature(
      first.graph,
      first.hierarchy,
      webcrypto as unknown as Crypto,
    );
    const changed = structuredClone(document);
    changed.nodes[0].node_config.mutable = 999;
    const changedGraph = adaptGraphDocument(changed);
    expect(
      await graphSignature(
        changedGraph,
        adaptPresentationGroups(changedGraph),
        webcrypto as unknown as Crypto,
      ),
    ).toBe(signature);
    expect(signature).toMatch(/^sha256:[0-9a-f]{64}$/);

    const structurallyChanged = structuredClone(document);
    structurallyChanged.nodes.push({
      id: "structural-addition",
      type: "Sink",
      node_config: { mutable: 1 },
    });
    const structurallyChangedGraph = adaptGraphDocument(structurallyChanged);
    await expect(
      graphSignature(
        structurallyChangedGraph,
        adaptPresentationGroups(structurallyChangedGraph),
        webcrypto as unknown as Crypto,
      ),
    ).resolves.not.toBe(signature);
  });

  test("decodes only the exact schema, signature, known unique groups, and finite ranges", () => {
    const valid = record();
    expect(
      decodePresentationPreferences(encodePresentationPreferences(valid), "sha256:abc", new Set(["g"])),
    ).toEqual(valid);
    const invalidCases: unknown[] = [
      { ...valid, schema: 2 },
      { ...valid, graph_signature: "sha256:stale" },
      { ...valid, mode: "other" },
      { ...valid, collapsed_group_ids: ["unknown"] },
      { ...valid, collapsed_group_ids: ["g", "g"] },
      { ...valid, semantic_expanded_group_ids: Array(257).fill("g") },
      { ...valid, viewport: { x: Number.NaN, y: 0, zoom: 1 } },
      { ...valid, viewport: { x: 10_000_001, y: 0, zoom: 1 } },
      { ...valid, viewport: { x: 0, y: 0, zoom: 0.09 } },
      { ...valid, viewport: { x: 0, y: 0, zoom: 4.01 } },
      { ...valid, unexpected: true },
    ];
    for (const invalid of invalidCases) {
      expect(
        decodePresentationPreferences(JSON.stringify(invalid), "sha256:abc", new Set(["g"])),
      ).toBeNull();
    }
    expect(
      decodePresentationPreferences("x".repeat(PRESENTATION_PREFERENCE_MAX_BYTES + 1), "sha256:abc", new Set()),
    ).toBeNull();

    const boundaryIds = Array.from({ length: 256 }, (_, index) => `g-${index}`);
    const boundaryRecord: PresentationPreferences = {
      ...record(),
      collapsed_group_ids: boundaryIds,
      semantic_expanded_group_ids: boundaryIds,
      viewport: { x: -10_000_000, y: 10_000_000, zoom: 4 },
    };
    expect(
      decodePresentationPreferences(
        JSON.stringify(boundaryRecord),
        "sha256:abc",
        new Set(boundaryIds),
      ),
    ).toEqual(boundaryRecord);
    for (const key of [
      "collapsed_group_ids",
      "semantic_expanded_group_ids",
    ] as const) {
      expect(
        decodePresentationPreferences(
          JSON.stringify({
            ...boundaryRecord,
            [key]: [...boundaryIds, "g-256"],
          }),
          "sha256:abc",
          new Set([...boundaryIds, "g-256"]),
        ),
      ).toBeNull();
    }
    for (const viewport of [
      { x: 10_000_000, y: -10_000_000, zoom: 0.1 },
      { x: -10_000_000, y: 10_000_000, zoom: 4 },
    ]) {
      expect(
        decodePresentationPreferences(
          JSON.stringify({ ...valid, viewport }),
          "sha256:abc",
          new Set(["g"]),
        ),
      ).not.toBeNull();
    }
  });

  test("handles absent, malformed, stale, unavailable, quota failure, write, and reset without throwing", () => {
    expect(
      readPresentationPreferences({ getItem: () => null }, "sha256:abc", new Set(["g"])),
    ).toEqual({ status: "absent", value: null, message: null });
    for (const serialized of ["{", JSON.stringify(record("sha256:stale"))]) {
      expect(
        readPresentationPreferences({ getItem: () => serialized }, "sha256:abc", new Set(["g"])).status,
      ).toBe("fallback");
    }
    expect(
      readPresentationPreferences(
        { getItem: () => { throw new Error("unavailable"); } },
        "sha256:abc",
        new Set(["g"]),
      ),
    ).toEqual({
      status: "fallback",
      value: null,
      message: "View preferences are unavailable; deterministic defaults are active.",
      disablePersistence: true,
    });

    const setItem = vi.fn();
    expect(writePresentationPreferences({ setItem }, record())).toBeNull();
    expect(setItem).toHaveBeenCalledWith(PRESENTATION_PREFERENCE_KEY, JSON.stringify(record()));
    expect(
      writePresentationPreferences(
        { setItem: () => { throw new DOMException("quota", "QuotaExceededError"); } },
        record(),
      ),
    ).toMatch(/could not be saved/);
    const removeItem = vi.fn();
    expect(removePresentationPreferences({ removeItem })).toBeNull();
    expect(removeItem).toHaveBeenCalledWith(PRESENTATION_PREFERENCE_KEY);
    expect(
      removePresentationPreferences({ removeItem: () => { throw new Error("blocked"); } }),
    ).toMatch(/could not be removed/);
  });

  test("fails visibly when Web Crypto is unavailable", async () => {
    const { graph, hierarchy } = model();
    await expect(graphSignature(graph, hierarchy, {} as Crypto)).rejects.toThrow(
      "Web Crypto SHA-256 is unavailable",
    );
  });
});
