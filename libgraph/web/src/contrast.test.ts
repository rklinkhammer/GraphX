import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, test } from "vitest";

const styles = readFileSync(resolve(process.cwd(), "src/styles.css"), "utf8");

function cssVariable(name: string): string {
  const match = styles.match(new RegExp(`--${name}:\\s*(#[0-9a-fA-F]{6})`));
  expect(match, `CSS variable --${name} must be a six-digit hex color`).not.toBeNull();
  return match![1];
}

function relativeLuminance(hex: string): number {
  const channels = [1, 3, 5].map((offset) =>
    Number.parseInt(hex.slice(offset, offset + 2), 16) / 255,
  );
  const linear = channels.map((channel) =>
    channel <= 0.04045
      ? channel / 12.92
      : ((channel + 0.055) / 1.055) ** 2.4,
  );
  return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
}

function contrastRatio(first: string, second: string): number {
  const firstLuminance = relativeLuminance(first);
  const secondLuminance = relativeLuminance(second);
  const lighter = Math.max(firstLuminance, secondLuminance);
  const darker = Math.min(firstLuminance, secondLuminance);
  return (lighter + 0.05) / (darker + 0.05);
}

describe("independent WCAG non-text contrast oracle", () => {
  test.each([
    ["focus ring on white controls", "focus-ring-color", "#ffffff"],
    ["focus ring on dashboard", "focus-ring-color", "#f8f9fc"],
    ["focus ring on semantic groups", "focus-ring-color", "#f7f8fc"],
    ["focus ring on selected records", "focus-ring-color", "#fff0d9"],
    ["control boundary on white", "control-border-color", "#ffffff"],
    ["semantic boundary on white", "semantic-border-color", "#ffffff"],
    ["semantic boundary on dashboard", "semantic-border-color", "#f8f9fc"],
    ["semantic boundary on group fill", "semantic-border-color", "#f7f8fc"],
    ["skip-link boundary on dark fill", "skip-border-color", "#172033"],
    ["selected node boundary on white", "selected-boundary-color", "#ffffff"],
    ["selected group boundary on collapsed fill", "selected-boundary-color", "#f0eafa"],
    ["selected edge against canvas", "selected-boundary-color", "#f9fbff"],
    ["selected semantic record on selected fill", "selected-boundary-color", "#fff0d9"],
    ["minimap boundary on white", "minimap-border-color", "#ffffff"],
    ["warning boundary on warning fill", "warning-boundary-color", "#fff4f5"],
    ["notice boundary on notice fill", "notice-boundary-color", "#e7f6ed"],
  ])("keeps %s at or above 3:1", (_label, variable, background) => {
    expect(contrastRatio(cssVariable(variable), background)).toBeGreaterThanOrEqual(3);
  });

  test.each([
    ["warning text", "warning-text-color", "#fff4f5"],
    ["dirty and clean status text", "status-secondary-text-color", "#ffffff"],
    ["configured status", "#ffffff", "#315ab4"],
    ["initialized status", "#ffffff", "#08778a"],
    ["running status", "#ffffff", "#16723e"],
    ["stopped status", "#ffffff", "#8c3f12"],
    ["error and unavailable status", "#ffffff", "#8a2530"],
  ])("keeps %s normal text at or above 4.5:1", (_label, foreground, background) => {
    const resolvedForeground = foreground.startsWith("#")
      ? foreground
      : cssVariable(foreground);
    expect(contrastRatio(resolvedForeground, background)).toBeGreaterThanOrEqual(4.5);
  });

  test("routes focus, controls, and semantic boundaries through checked colors", () => {
    expect(styles).toMatch(/outline:\s*3px solid var\(--focus-ring-color\)/);
    expect(styles).toMatch(/outline:\s*4px solid var\(--focus-ring-color\)/);
    expect(styles).toMatch(/button[\s\S]*border:\s*1px solid var\(--control-border-color\)/);
    expect(styles).toMatch(/semantic-group-item[\s\S]*border:\s*1px solid var\(--semantic-border-color\)/);
    expect(styles).toMatch(/semantic-edge-table-region[\s\S]*border:\s*1px solid var\(--semantic-border-color\)/);
    expect(styles).toMatch(/graph-node-card\.selected[\s\S]*border-color:\s*var\(--selected-boundary-color\)/);
    expect(styles).toMatch(/graph-group-card\.selected[\s\S]*border-color:\s*var\(--selected-boundary-color\)/);
    expect(styles).toMatch(/bundle-edge\.selected path[\s\S]*stroke:\s*var\(--selected-boundary-color\)/);
    expect(styles).toMatch(/react-flow__minimap[\s\S]*border:\s*1px solid var\(--minimap-border-color\)/);
    expect(styles).toMatch(/semantic-warnings[\s\S]*border:\s*2px solid var\(--warning-boundary-color\)/);
    expect(styles).toMatch(/notice[\s\S]*border-left:\s*0\.3rem solid var\(--notice-boundary-color\)/);
    expect(styles).toMatch(/revision-summary[\s\S]*color:\s*var\(--status-secondary-text-color\)/);
  });
});
