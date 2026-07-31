import { describe, expect, test, vi } from "vitest";

import { adaptGraphDocument } from "./adapter";
import { layoutDisplayGraph } from "./layout";
import cyclic from "./test/fixtures/cyclic.json";
import disconnected from "./test/fixtures/disconnected.json";
import splitMerge from "./test/fixtures/split_merge.json";

describe("deterministic layered layout", () => {
  test("is stable for repeated conversion of the same split/merge document", async () => {
    const model = adaptGraphDocument(splitMerge);
    const first = await layoutDisplayGraph(model);
    const second = await layoutDisplayGraph(model);
    expect(first.diagnostic).toBeNull();
    expect(first.nodes.map(({ id, position }) => ({ id, position }))).toEqual(
      second.nodes.map(({ id, position }) => ({ id, position })),
    );
    expect(first.edges.map(({ id, sourceHandle, targetHandle }) => ({
      id,
      sourceHandle,
      targetHandle,
    }))).toEqual(
      second.edges.map(({ id, sourceHandle, targetHandle }) => ({
        id,
        sourceHandle,
        targetHandle,
      })),
    );
    expect(first.edges[0].ariaLabel).toMatch(
      /^Edge from .+ (index|name) .+ to .+ (index|name) .+\. Press Enter or Space to select\.$/,
    );
  });

  test("lays out disconnected and cyclic graphs without deleting entities", async () => {
    const islands = await layoutDisplayGraph(adaptGraphDocument(disconnected));
    expect(islands.nodes).toHaveLength(3);
    expect(islands.edges).toHaveLength(0);

    const cycle = await layoutDisplayGraph(adaptGraphDocument(cyclic));
    expect(cycle.nodes).toHaveLength(2);
    expect(cycle.edges).toHaveLength(2);
  });

  test("uses a deterministic grid and retains every entity when ELK fails", async () => {
    const model = adaptGraphDocument(splitMerge);
    const forcedFailure = vi.fn(async () => {
      throw new Error("independently forced layout failure");
    });
    const result = await layoutDisplayGraph(model, forcedFailure);
    expect(forcedFailure).toHaveBeenCalledTimes(1);
    expect(result.nodes).toHaveLength(model.nodes.length);
    expect(result.edges).toHaveLength(model.edges.length);
    expect(result.diagnostic).toContain(
      "independently forced layout failure",
    );
    expect(result.nodes.map(({ position }) => position)).toEqual([
      { x: 0, y: 0 },
      { x: 340, y: 0 },
      { x: 680, y: 0 },
      { x: 0, y: 220 },
      { x: 340, y: 220 },
      { x: 680, y: 220 },
    ]);
  });
});
