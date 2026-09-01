import { edgeIdentity } from "./adapter";
import type { DisplayGraph, PortKey } from "./types";

export const COMMAND_HISTORY_LIMIT = 128;
export const COMMAND_POLL_MS = 500;
export const METRIC_POLL_MS = 1_000;
export const METRIC_STALE_MS = 3_000;
export const METRIC_RESPONSE_BYTES = 1_048_576;
export const METRIC_SCHEMA_BYTES = 262_144;
export const METRIC_VALUE_BYTES = 524_288;
export const GRAPH_EXPORT_BYTES = 16_777_216;
export const ANIMATED_EDGE_LIMIT = 256;

export class IgnoredMetricSnapshotError extends Error {}

export interface CommandDescriptor {
  name: string;
  asynchronous: boolean;
  arguments: Record<string, unknown>;
  description: string;
  supported: boolean;
  unsupportedReason?: string;
}

export interface OperationRecord {
  command: string;
  operation_id: string;
  status: string;
  state: string;
  coordinator_revision: number;
  configured_revision: number | null;
  active_revision: number | null;
  graph_generation: number;
  configuration_dirty: boolean;
  diagnostic?: string;
}

export function retainOperationHistory(
  current: readonly OperationRecord[],
  operation: OperationRecord,
): OperationRecord[] {
  return [
    operation,
    ...current.filter((entry) => entry.operation_id !== operation.operation_id),
  ].slice(0, COMMAND_HISTORY_LIMIT);
}

function scanJsonString(text: string, start: number): number {
  if (text[start] !== '"') throw new Error("JSON string expected");
  for (let index = start + 1; index < text.length; ++index) {
    const character = text[index];
    if (character === '"') return index + 1;
    if (character === "\\") {
      index += 1;
      if (index >= text.length) throw new Error("unterminated JSON escape");
      if (text[index] === "u") {
        if (!/^[0-9A-Fa-f]{4}$/.test(text.slice(index + 1, index + 5))) {
          throw new Error("invalid JSON unicode escape");
        }
        index += 4;
      }
    } else if (character.charCodeAt(0) < 0x20) {
      throw new Error("invalid JSON string control character");
    }
  }
  throw new Error("unterminated JSON string");
}

const skipJsonWhitespace = (text: string, start: number): number => {
  let index = start;
  while (index < text.length && /[\t\n\r ]/.test(text[index])) index += 1;
  return index;
};

function scanJsonValue(text: string, start: number): number {
  const first = skipJsonWhitespace(text, start);
  if (text[first] === '"') return scanJsonString(text, first);
  if (text[first] === "{" || text[first] === "[") {
    const stack = [text[first] === "{" ? "}" : "]"];
    for (let index = first + 1; index < text.length; ++index) {
      if (text[index] === '"') {
        index = scanJsonString(text, index) - 1;
      } else if (text[index] === "{" || text[index] === "[") {
        stack.push(text[index] === "{" ? "}" : "]");
      } else if (text[index] === "}" || text[index] === "]") {
        if (stack.pop() !== text[index]) throw new Error("mismatched JSON container");
        if (stack.length === 0) return index + 1;
      }
    }
    throw new Error("unterminated JSON container");
  }
  let end = first;
  while (end < text.length && !/[\t\n\r ,}\]]/.test(text[end])) end += 1;
  if (end === first) throw new Error("JSON value expected");
  const primitive = text.slice(first, end);
  if (primitive !== "true" && primitive !== "false" && primitive !== "null" &&
      !/^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(primitive)) {
    throw new Error("invalid JSON primitive");
  }
  return end;
}

export function extractTopLevelJsonMember(text: string, member: string): string {
  if (new TextEncoder().encode(text).byteLength > GRAPH_EXPORT_BYTES + 4096) {
    throw new Error("graph response exceeds the bounded browser limit");
  }
  let index = skipJsonWhitespace(text, 0);
  if (text[index++] !== "{") throw new Error("graph response envelope must be an object");
  let found: string | null = null;
  for (;;) {
    index = skipJsonWhitespace(text, index);
    if (text[index] === "}") {
      index += 1;
      if (skipJsonWhitespace(text, index) !== text.length) {
        throw new Error("trailing text after graph response envelope");
      }
      break;
    }
    const keyEnd = scanJsonString(text, index);
    const key = JSON.parse(text.slice(index, keyEnd)) as string;
    index = skipJsonWhitespace(text, keyEnd);
    if (text[index++] !== ":") throw new Error("graph response member separator is invalid");
    const valueStart = skipJsonWhitespace(text, index);
    const valueEnd = scanJsonValue(text, valueStart);
    if (key === member) {
      if (found !== null) throw new Error(`duplicate ${member} member in graph response`);
      found = text.slice(valueStart, valueEnd);
    }
    index = skipJsonWhitespace(text, valueEnd);
    if (text[index] === "}") {
      index += 1;
      if (skipJsonWhitespace(text, index) !== text.length) {
        throw new Error("trailing text after graph response envelope");
      }
      break;
    }
    if (text[index++] !== ",") throw new Error("graph response member delimiter is invalid");
  }
  if (found === null) throw new Error(`graph response is missing ${member}`);
  return found;
}

export function prepareGraphExportFromRaw(
  rawGraph: string,
  coordinatorRevision: number,
  contentIdentity: string,
): { encoded: string; filename: string } {
  const end = scanJsonValue(rawGraph, 0);
  if (skipJsonWhitespace(rawGraph, end) !== rawGraph.length) {
    throw new Error("raw authoritative graph token is invalid");
  }
  const metadata = JSON.stringify({
    artifact: "graphx.graph-export", version: 1,
    coordinator_revision: coordinatorRevision, content_identity: contentIdentity,
  }, null, 2);
  const encoded = `${metadata.slice(0, -2)},\n  "graph": ${rawGraph}\n}`;
  if (new TextEncoder().encode(encoded).byteLength > GRAPH_EXPORT_BYTES) {
    throw new Error("export exceeds the 16 MiB browser limit");
  }
  const identity = contentIdentity.replace(/[^A-Za-z0-9]/g, "").slice(0, 12) || "unknown";
  return { encoded, filename: `graphx-graph-r${coordinatorRevision}-${identity}.json`.slice(0, 96) };
}

export function mayAnimateEdge(
  hasExactActivity: boolean,
  reducedMotion: boolean,
  alreadyAnimated: number,
): boolean {
  return hasExactActivity && !reducedMotion && alreadyAnimated < ANIMATED_EDGE_LIMIT;
}

interface ApiEnvelope<T> {
  success: boolean;
  data?: T;
  message?: string;
}

async function readEnvelope<T>(response: Response): Promise<ApiEnvelope<T>> {
  const body = (await response.json()) as ApiEnvelope<T>;
  if (!response.ok || !body.success || body.data === undefined) {
    throw new Error(body.message ?? `HTTP ${response.status}`);
  }
  return body;
}

function argumentSchemaSupported(argumentsSchema: Record<string, unknown>): string | null {
  const rootKeys = new Set(["type", "properties", "required", "additionalProperties"]);
  if (Object.keys(argumentsSchema).some((key) => !rootKeys.has(key))) {
    return "argument schema contains an unsupported root keyword";
  }
  if (argumentsSchema.type !== undefined && argumentsSchema.type !== "object") {
    return "argument schema root type is unsupported";
  }
  if (argumentsSchema.additionalProperties !== undefined &&
      argumentsSchema.additionalProperties !== false) {
    return "argument schema permits undeclared properties";
  }
  const properties = argumentsSchema.properties;
  if (properties === undefined && Object.keys(argumentsSchema).length === 0) return null;
  if (properties === null || typeof properties !== "object" || Array.isArray(properties)) {
    return "argument schema does not contain bounded properties";
  }
  const entries = Object.entries(properties as Record<string, unknown>);
  if (entries.length > 32) return "argument schema exceeds 32 fields";
  const propertyNames = new Set(entries.map(([name]) => name));
  const required = argumentsSchema.required;
  if (required !== undefined && (!Array.isArray(required) || required.length > 32 ||
      required.some((name) => typeof name !== "string" || !propertyNames.has(name)) ||
      new Set(required).size !== required.length)) {
    return "argument schema required fields are invalid";
  }
  for (const [name, candidate] of entries) {
    if (new TextEncoder().encode(name).byteLength === 0 ||
        new TextEncoder().encode(name).byteLength > 128 || candidate === null ||
        typeof candidate !== "object" || Array.isArray(candidate)) {
      return `unsupported field ${name}`;
    }
    const field = candidate as Record<string, unknown>;
    const type = field.type;
    const commonKeys = new Set(["type", "description"]);
    const allowedKeys = type === "string"
      ? new Set([...commonKeys, "enum", "maxLength"])
      : type === "number" || type === "integer" || type === "unsigned"
        ? new Set([...commonKeys, "minimum", "maximum"])
        : commonKeys;
    if (Object.keys(field).some((key) => !allowedKeys.has(key)) ||
        (field.description !== undefined && !boundedString(field.description, 1_024))) {
      return `unsupported field ${name}`;
    }
    const semanticTokens = name
      .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
      .toLowerCase().split(/[^a-z0-9]+/).filter(Boolean);
    const forbiddenTokens = new Set([
      "command", "shell", "terminal", "executable", "environment", "env",
      "filesystem", "file", "path", "url", "uri",
    ]);
    if (semanticTokens.some((token) => forbiddenTokens.has(token))) {
      return `forbidden execution field ${name}`;
    }
    const supported = type === "boolean" || type === "integer" ||
      type === "unsigned" || type === "number" || type === "string";
    const stringEnumValid = field.enum === undefined ||
      (type === "string" && Array.isArray(field.enum) && field.enum.length > 0 &&
        field.enum.length <= 64 && new Set(field.enum).size === field.enum.length &&
        field.enum.every((value) => typeof value === "string" &&
          new TextEncoder().encode(value).byteLength <= 1_024) &&
        (field.maxLength === undefined ||
          (Number.isSafeInteger(field.maxLength) && Number(field.maxLength) > 0 &&
           Number(field.maxLength) <= 1_024 && field.enum.every((value) =>
             new TextEncoder().encode(String(value)).byteLength <= Number(field.maxLength)))));
    const boundedStringValid = type !== "string" || field.enum !== undefined ||
      (Number.isSafeInteger(field.maxLength) && Number(field.maxLength) > 0 &&
       Number(field.maxLength) <= 1_024);
    const boundedNumberValid = (type !== "number" && type !== "integer" && type !== "unsigned") ||
      (typeof field.minimum === "number" && Number.isFinite(field.minimum) &&
       typeof field.maximum === "number" && Number.isFinite(field.maximum) &&
       field.minimum <= field.maximum && (type !== "unsigned" || field.minimum >= 0));
    const safeIntegerBounds = (type !== "integer" && type !== "unsigned") ||
      (Number.isSafeInteger(field.minimum) && Number.isSafeInteger(field.maximum));
    if (!supported || !stringEnumValid || !boundedStringValid || !boundedNumberValid ||
        !safeIntegerBounds) {
      return `unsupported field ${name}`;
    }
  }
  return null;
}

export async function discoverCommands(signal?: AbortSignal): Promise<CommandDescriptor[]> {
  const response = await fetch("/api/v1/execution/commands", { signal });
  const envelope = await readEnvelope<unknown[]>(response);
  if (!Array.isArray(envelope.data) || envelope.data.length > 32) {
    throw new Error("command discovery response is invalid or over-bound");
  }
  const discovered = envelope.data.map((candidate) => {
    if (candidate === null || typeof candidate !== "object" || Array.isArray(candidate)) {
      throw new Error("command descriptor is invalid");
    }
    const value = candidate as Record<string, unknown>;
    const descriptorKeys = new Set(["name", "asynchronous", "arguments", "description"]);
    const metadataValid = Object.keys(value).length === descriptorKeys.size &&
      Object.keys(value).every((key) => descriptorKeys.has(key)) &&
      boundedString(value.name, 128, false) && typeof value.asynchronous === "boolean" &&
      boundedString(value.description, 1_024) && value.arguments !== null &&
      typeof value.arguments === "object" && !Array.isArray(value.arguments);
    const name = typeof value.name === "string" ? value.name : "";
    const argumentsSchema = metadataValid && value.arguments !== null &&
      typeof value.arguments === "object" && !Array.isArray(value.arguments)
      ? value.arguments as Record<string, unknown> : {};
    const unsupportedReason = metadataValid
      ? argumentSchemaSupported(argumentsSchema)
      : "command descriptor metadata is invalid";
    return {
      name,
      asynchronous: metadataValid && value.asynchronous === true,
      arguments: argumentsSchema,
      description: metadataValid ? value.description as string : "",
      supported: metadataValid && unsupportedReason === null,
      ...(unsupportedReason ? { unsupportedReason } : {}),
    };
  });
  if (new Set(discovered.map((command) => command.name)).size !== discovered.length) {
    throw new Error("command discovery contains duplicate names");
  }
  return discovered;
}

const operationStatuses = new Set(["accepted", "running", "completed", "failed", "cancelled"]);
const executionStates = new Set([
  "CONFIGURED", "INITIALIZED", "RUNNING", "STOPPING", "STOPPED", "ERROR",
]);

function parseOperationRecord(candidate: unknown): OperationRecord {
  if (candidate === null || typeof candidate !== "object" || Array.isArray(candidate)) {
    throw new Error("operation response is invalid");
  }
  const value = candidate as Record<string, unknown>;
  const optionalRevision = (revision: unknown) => revision === null || safeUnsigned(revision);
  if (!boundedString(value.command, 128, false) ||
      !boundedString(value.operation_id, 128, false) ||
      !operationStatuses.has(String(value.status)) ||
      !executionStates.has(String(value.state)) ||
      !safeUnsigned(value.coordinator_revision) ||
      !optionalRevision(value.configured_revision) ||
      !optionalRevision(value.active_revision) ||
      !safeUnsigned(value.graph_generation) ||
      typeof value.configuration_dirty !== "boolean") {
    throw new Error("operation response contract is invalid");
  }
  return value as unknown as OperationRecord;
}

export function validateCommandArguments(
  descriptor: CommandDescriptor,
  argumentsValue: Record<string, unknown>,
): string | null {
  if (!descriptor.supported || Object.keys(argumentsValue).length > 32) {
    return "command argument set is unsupported or over-bound";
  }
  const properties = descriptor.arguments.properties;
  const definitions = properties && typeof properties === "object" && !Array.isArray(properties)
    ? properties as Record<string, Record<string, unknown>> : {};
  const required = Array.isArray(descriptor.arguments.required)
    ? descriptor.arguments.required.filter((name): name is string => typeof name === "string") : [];
  for (const name of required) {
    if (!(name in argumentsValue)) return `required field ${name} is missing`;
  }
  for (const [name, value] of Object.entries(argumentsValue)) {
    const schema = definitions[name];
    if (!schema) return `field ${name} was not declared`;
    if (schema.type === "boolean" && typeof value !== "boolean") return `field ${name} must be boolean`;
    if ((schema.type === "integer" || schema.type === "unsigned") &&
        !(typeof value === "number" && Number.isSafeInteger(value))) return `field ${name} must be an integer`;
    if (schema.type === "number" && !(typeof value === "number" && Number.isFinite(value))) {
      return `field ${name} must be finite`;
    }
    if ((schema.type === "integer" || schema.type === "unsigned" || schema.type === "number") &&
        (value as number) < Number(schema.minimum)) return `field ${name} is below its minimum`;
    if ((schema.type === "integer" || schema.type === "unsigned" || schema.type === "number") &&
        (value as number) > Number(schema.maximum)) return `field ${name} exceeds its maximum`;
    if (schema.type === "unsigned" && (value as number) < 0) return `field ${name} must be unsigned`;
    if (schema.type === "string") {
      if (typeof value !== "string" ||
          new TextEncoder().encode(value).byteLength > Number(schema.maxLength ?? 1_024)) {
        return `field ${name} exceeds its string contract`;
      }
      if (Array.isArray(schema.enum) && !schema.enum.includes(value)) return `field ${name} is not an allowed value`;
    }
  }
  return null;
}

export async function submitCommand(
  command: string,
  argumentsValue: Record<string, unknown> = {},
  signal?: AbortSignal,
): Promise<{ operation: OperationRecord; location: string | null; message: string }> {
  if (!boundedString(command, 128, false) || Object.keys(argumentsValue).length > 32) {
    throw new Error("command request is invalid or over-bound");
  }
  const response = await fetch(
    `/api/v1/execution/commands/${encodeURIComponent(command)}`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(argumentsValue),
      signal,
    },
  );
  const envelope = await readEnvelope<OperationRecord>(response);
  const operation = parseOperationRecord(envelope.data);
  const location = response.headers.get("Location");
  const terminal = isTerminalOperation(operation.status);
  if ((response.status !== 200 && response.status !== 202) ||
      operation.command !== command ||
      (response.status === 202 && (terminal || location === null)) ||
      (response.status === 200 && !terminal) ||
      (location !== null && location !==
        `/api/v1/execution/operations/${encodeURIComponent(operation.operation_id)}`)) {
    throw new Error("command response status, operation, or Location is inconsistent");
  }
  return {
    operation,
    location,
    message: envelope.message ?? `Command ${command} completed`,
  };
}

export function isTerminalOperation(status: string): boolean {
  return status === "completed" || status === "failed" || status === "cancelled";
}

export async function pollOperation(
  location: string,
  signal: AbortSignal,
  expectedCommand?: string,
): Promise<OperationRecord> {
  if (!/^\/api\/v1\/execution\/operations\/[A-Za-z0-9._-]{1,128}$/.test(location)) {
    throw new Error("server returned an invalid operation location");
  }
  while (!signal.aborted) {
    await new Promise<void>((resolve, reject) => {
      const onAbort = () => {
        window.clearTimeout(timer);
        signal.removeEventListener("abort", onAbort);
        reject(new DOMException("Polling aborted", "AbortError"));
      };
      const timer = window.setTimeout(() => {
        signal.removeEventListener("abort", onAbort);
        resolve();
      }, COMMAND_POLL_MS);
      signal.addEventListener("abort", onAbort, { once: true });
      if (signal.aborted) onAbort();
    });
    const response = await fetch(location, { signal });
    if (response.status === 404) throw new Error("operation expired or is unknown");
    const envelope = await readEnvelope<OperationRecord>(response);
    const operation = parseOperationRecord(envelope.data);
    const expectedOperationId = decodeURIComponent(location.split("/").at(-1) ?? "");
    if (operation.operation_id !== expectedOperationId ||
        (expectedCommand !== undefined && operation.command !== expectedCommand)) {
      throw new Error("operation polling response is not correlated to its command");
    }
    if (isTerminalOperation(operation.status)) return operation;
  }
  throw new DOMException("Polling aborted", "AbortError");
}

export interface MetricTarget {
  kind: "node" | "edge";
  node_id?: string;
  source_node_id?: string;
  source_port?: { kind: "index" | "name"; value: number | string };
  target_node_id?: string;
  target_port?: { kind: "index" | "name"; value: number | string };
}

export interface MetricValue {
  target: MetricTarget;
  graph_generation: number;
  metric_id: string;
  scalar_type: string;
  scalar_encoding: "native" | "decimal_string";
  unit: string;
  semantics: string;
  aggregation: string;
  availability_rule: string;
  availability: "available" | "unavailable";
  reason: string;
  value: boolean | number | string | null;
  sample_time: string | null;
  counter_epoch?: string | null;
  counter_epoch_encoding?: "decimal_string";
  rate?: number | null;
  rate_reason?: string;
  identity: string;
}

export interface MetricsSnapshot {
  schema_version: 1;
  graph_generation: number;
  active_revision: number | null;
  snapshot_sequence: number;
  snapshot_time: string;
  availability: { state: "available" | "unavailable" | "stale"; reason: string };
  schemas: MetricSchema[];
  values: MetricValue[];
  diagnostics: {
    rejected: number;
    dropped_queue_full: number;
    rejection_categories: {
      schema_contract: number;
      sample_contract: number;
      authority_mismatch: number;
      subscriber_failure: number;
      internal: number;
    };
  };
}

export interface MetricSchema {
  target: MetricTarget;
  graph_generation: number;
  metric_id: string;
  scalar_type: "boolean" | "integer" | "unsigned" | "number" | "string";
  scalar_encoding: "native" | "decimal_string";
  unit: string;
  semantics: "gauge" | "monotonic_counter" | "state";
  aggregation: "sum" | "min" | "max" | "average" | "rate" | "none";
  availability_rule: string;
  identity: string;
}

const encodeMetricIdentityPart = (value: string): string =>
  `${new TextEncoder().encode(value).byteLength}:${value}`;
const compareUtf8 = (left: string, right: string): number => {
  const encoder = new TextEncoder();
  const leftBytes = encoder.encode(left);
  const rightBytes = encoder.encode(right);
  const shared = Math.min(leftBytes.length, rightBytes.length);
  for (let index = 0; index < shared; ++index) {
    if (leftBytes[index] !== rightBytes[index]) {
      return leftBytes[index] - rightBytes[index];
    }
  }
  return leftBytes.length - rightBytes.length;
};
const hasOnlyKeys = (value: Record<string, unknown>, allowed: readonly string[]): boolean =>
  Object.keys(value).every((key) => allowed.includes(key));

function targetIdentity(target: unknown, graph: DisplayGraph): {
  target: MetricTarget;
  identity: string;
  wireKey: string;
} {
  if (target === null || typeof target !== "object" || Array.isArray(target)) {
    throw new Error("metric target is invalid");
  }
  const candidate = target as Record<string, unknown>;
  if (candidate.kind !== "node" && candidate.kind !== "edge") {
    throw new Error("metric target kind is invalid");
  }
  const boundedNodeId = (value: unknown): value is string =>
    boundedString(value, 256, false);
  if (candidate.kind === "node") {
    if (!hasOnlyKeys(candidate, ["kind", "node_id"])) {
      throw new Error("metric node target contains undeclared fields");
    }
    if (!boundedNodeId(candidate.node_id) ||
        !graph.nodes.some((node) => node.id === candidate.node_id)) {
      throw new Error("metric node target is not authoritative");
    }
    const parsed: MetricTarget = { kind: "node", node_id: candidate.node_id };
    return {
      target: parsed,
      identity: `node:${candidate.node_id}`,
      wireKey: `node|${encodeMetricIdentityPart(candidate.node_id)}`,
    };
  }
  const port = (value: unknown): PortKey | null => {
    if (value === null || typeof value !== "object" || Array.isArray(value)) return null;
    const parsed = value as Record<string, unknown>;
    if (!hasOnlyKeys(parsed, ["kind", "value"])) return null;
    if (parsed.kind === "index" && typeof parsed.value === "number" &&
        Number.isSafeInteger(parsed.value) && parsed.value >= 0) {
      return { kind: "index", value: parsed.value };
    }
    if (parsed.kind === "name" && boundedString(parsed.value, 128, false)) {
      return { kind: "name", value: parsed.value };
    }
    return null;
  };
  const source = port(candidate.source_port);
  const destination = port(candidate.target_port);
  if (!hasOnlyKeys(candidate, ["kind", "source_node_id", "source_port",
    "target_node_id", "target_port"])) {
    throw new Error("metric edge target contains undeclared fields");
  }
  if (!boundedNodeId(candidate.source_node_id) ||
      !boundedNodeId(candidate.target_node_id) || !source || !destination) {
    throw new Error("metric edge target tuple is invalid");
  }
  const id = edgeIdentity(candidate.source_node_id, source, candidate.target_node_id, destination);
  if (!graph.edges.some((edge) => edge.id === id)) {
    throw new Error("metric edge target is not authoritative");
  }
  return {
    target: {
      kind: "edge",
      source_node_id: candidate.source_node_id,
      source_port: source,
      target_node_id: candidate.target_node_id,
      target_port: destination,
    },
    identity: `edge:${id}`,
    wireKey: `edge|${encodeMetricIdentityPart(candidate.source_node_id)}|${
      source.kind === "index" ? `index:${source.value}` :
        `name:${encodeMetricIdentityPart(source.value)}`
    }|${encodeMetricIdentityPart(candidate.target_node_id)}|${
      destination.kind === "index" ? `index:${destination.value}` :
        `name:${encodeMetricIdentityPart(destination.value)}`
    }`,
  };
}

const scalarTypes = new Set(["boolean", "integer", "unsigned", "number", "string"]);
const semantics = new Set(["gauge", "monotonic_counter", "state"]);
const aggregations = new Set(["sum", "min", "max", "average", "rate", "none"]);
const boundedString = (value: unknown, maximum: number, allowEmpty = true): value is string =>
  typeof value === "string" && new TextEncoder().encode(value).byteLength <= maximum &&
  (allowEmpty || value.length > 0);
const safeUnsigned = (value: unknown): value is number =>
  typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
// The metrics wire contract uses the server's canonical UTC rendering:
// YYYY-MM-DDTHH:mm:ss.sssZ. Keeping exactly millisecond precision makes the
// accepted grammar deterministic across browsers instead of inheriting each
// Date.parse implementation's permissive ISO-8601 extensions.
const validTimestamp = (value: unknown): value is string => {
  if (typeof value !== "string") return false;
  const match = value.match(
    /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})\.(\d{3})Z$/,
  );
  if (!match) return false;
  const [, yearText, monthText, dayText, hourText, minuteText, secondText] = match;
  const year = Number(yearText);
  const month = Number(monthText);
  const day = Number(dayText);
  const hour = Number(hourText);
  const minute = Number(minuteText);
  const second = Number(secondText);
  const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const days = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  if (month < 1 || month > 12 || day < 1 || day > days[month - 1] ||
      hour > 23 || minute > 59 || second > 59) return false;
  const parsed = Date.parse(value);
  return Number.isFinite(parsed) && parsed <= Date.now() + 1_000;
};

function descriptorFrom(candidate: unknown, graph: DisplayGraph, generation: number): MetricSchema {
  if (candidate === null || typeof candidate !== "object" || Array.isArray(candidate)) {
    throw new Error("metric schema descriptor is invalid");
  }
  const value = candidate as Record<string, unknown>;
  if (!hasOnlyKeys(value, ["target", "graph_generation", "metric_id", "scalar_type",
    "scalar_encoding", "unit", "semantics", "aggregation", "availability_rule"])) {
    throw new Error("metric schema descriptor contains undeclared fields");
  }
  const parsedTarget = targetIdentity(value.target, graph);
  const expectedEncoding = value.scalar_type === "integer" ||
      value.scalar_type === "unsigned" ? "decimal_string" : "native";
  if (value.graph_generation !== generation || !boundedString(value.metric_id, 128, false) ||
      !scalarTypes.has(String(value.scalar_type)) || !boundedString(value.unit, 32) ||
      value.scalar_encoding !== expectedEncoding ||
      !semantics.has(String(value.semantics)) || !aggregations.has(String(value.aggregation)) ||
      !boundedString(value.availability_rule, 256, false)) {
    throw new Error("metric schema descriptor contract is invalid");
  }
  return {
    target: parsedTarget.target,
    graph_generation: generation,
    metric_id: value.metric_id,
    scalar_type: value.scalar_type as MetricSchema["scalar_type"],
    scalar_encoding: value.scalar_encoding as MetricSchema["scalar_encoding"],
    unit: value.unit,
    semantics: value.semantics as MetricSchema["semantics"],
    aggregation: value.aggregation as MetricSchema["aggregation"],
    availability_rule: value.availability_rule,
    identity: parsedTarget.identity,
  };
}

function typedScalar(value: unknown, scalarType: MetricSchema["scalar_type"],
                     encoding: MetricSchema["scalar_encoding"]): boolean {
  const signedDecimal = (candidate: unknown): candidate is string => {
    if (typeof candidate !== "string" || !/^(?:0|-?[1-9][0-9]*)$/.test(candidate)) return false;
    const parsed = BigInt(candidate);
    return parsed >= -(1n << 63n) && parsed <= (1n << 63n) - 1n;
  };
  const unsignedDecimal = (candidate: unknown): candidate is string => {
    if (typeof candidate !== "string" || !/^(?:0|[1-9][0-9]*)$/.test(candidate)) return false;
    const parsed = BigInt(candidate);
    return parsed <= (1n << 64n) - 1n;
  };
  if (scalarType === "integer") return encoding === "decimal_string" && signedDecimal(value);
  if (scalarType === "unsigned") return encoding === "decimal_string" && unsignedDecimal(value);
  if (encoding !== "native") return false;
  if (scalarType === "boolean") return typeof value === "boolean";
  if (scalarType === "number") return typeof value === "number" && Number.isFinite(value);
  return boundedString(value, 1_024);
}

export function validateMetricTargetSchemaBounds(
  schemas: readonly Pick<MetricSchema, "target">[],
  graph: DisplayGraph,
): void {
  const metricCountByTarget = new Map<string, number>();
  for (const schema of schemas) {
    const wireKey = targetIdentity(schema.target, graph).wireKey;
    metricCountByTarget.set(wireKey, (metricCountByTarget.get(wireKey) ?? 0) + 1);
    if (metricCountByTarget.size > 2_048) {
      throw new Error("metric target schema bounds are invalid");
    }
  }
}

export function parseMetricsSnapshot(
  document: unknown,
  graph: DisplayGraph,
  expectedGeneration: number | null,
  previous: { generation: number; sequence: number } | null = null,
): MetricsSnapshot {
  if (document === null || typeof document !== "object" || Array.isArray(document)) {
    throw new Error("metric response must be an object");
  }
  const envelope = document as { success?: unknown; data?: unknown };
  if (envelope.success !== true || envelope.data === null ||
      typeof envelope.data !== "object" || Array.isArray(envelope.data)) {
    throw new Error("metric response envelope is invalid");
  }
  const data = envelope.data as Record<string, unknown>;
  if (data.schema_version !== 1 || !safeUnsigned(data.graph_generation) ||
      !safeUnsigned(data.snapshot_sequence) || !Array.isArray(data.values) ||
      data.values.length > 4096 || !Array.isArray(data.schemas) || data.schemas.length > 4096) {
    throw new Error("metric snapshot schema or bounds are invalid");
  }
  const encoder = new TextEncoder();
  if (encoder.encode(JSON.stringify(data.schemas)).byteLength > METRIC_SCHEMA_BYTES ||
      encoder.encode(JSON.stringify(data.values)).byteLength > METRIC_VALUE_BYTES) {
    throw new Error("metric snapshot split payload bounds are invalid");
  }
  const generation = data.graph_generation as number;
  if (expectedGeneration !== null && generation !== expectedGeneration) {
    throw new IgnoredMetricSnapshotError(
      `stale metric generation ${generation}; expected ${expectedGeneration}`,
    );
  }
  if (previous && (generation < previous.generation ||
      (generation === previous.generation && data.snapshot_sequence < previous.sequence))) {
    throw new IgnoredMetricSnapshotError("out-of-order metric snapshot");
  }
  if (data.active_revision !== null && !safeUnsigned(data.active_revision)) {
    throw new Error("metric active revision is invalid");
  }
  if (!validTimestamp(data.snapshot_time) || data.availability === null ||
      typeof data.availability !== "object" || Array.isArray(data.availability) ||
      data.diagnostics === null || typeof data.diagnostics !== "object" ||
      Array.isArray(data.diagnostics)) {
    throw new Error("metric snapshot metadata is invalid");
  }
  const availability = data.availability as Record<string, unknown>;
  if (!new Set(["available", "unavailable", "stale"]).has(String(availability.state)) ||
      !boundedString(availability.reason, 256) ||
      (availability.state !== "available" && availability.reason === "")) {
    throw new Error("metric availability is invalid");
  }
  const diagnostics = data.diagnostics as Record<string, unknown>;
  const categoryKeys = ["schema_contract", "sample_contract", "authority_mismatch",
    "subscriber_failure", "internal"];
  const categories = diagnostics.rejection_categories;
  if (!hasOnlyKeys(diagnostics,
    ["rejected", "dropped_queue_full", "rejection_categories"]) ||
      !safeUnsigned(diagnostics.rejected) ||
      !safeUnsigned(diagnostics.dropped_queue_full) || categories === null ||
      typeof categories !== "object" || Array.isArray(categories) ||
      !hasOnlyKeys(categories as Record<string, unknown>, categoryKeys) ||
      categoryKeys.some((key) =>
        !safeUnsigned((categories as Record<string, unknown>)[key]))) {
    throw new Error("metric diagnostics are invalid");
  }
  const schemas = data.schemas.map((candidate) => descriptorFrom(candidate, graph, generation));
  validateMetricTargetSchemaBounds(schemas, graph);
  const schemaByKey = new Map<string, MetricSchema>();
  let previousSchemaKey = "";
  for (const schema of schemas) {
    const parsedTarget = targetIdentity(schema.target, graph);
    const key = `${parsedTarget.wireKey}|metric|${encodeMetricIdentityPart(schema.metric_id)}`;
    if (schemaByKey.has(key)) throw new Error("duplicate metric schema identity");
    if (previousSchemaKey && compareUtf8(key, previousSchemaKey) < 0) {
      throw new Error("metric schemas are not deterministically ordered");
    }
    schemaByKey.set(key, schema);
    previousSchemaKey = key;
  }
  const values: MetricValue[] = [];
  const valueKeys = new Set<string>();
  let previousValueKey = "";
  for (const candidate of data.values) {
    if (candidate === null || typeof candidate !== "object" || Array.isArray(candidate)) {
      throw new Error("metric value is invalid");
    }
    const value = candidate as Record<string, unknown>;
    const allowedValueKeys = ["target", "graph_generation", "metric_id", "scalar_type",
      "scalar_encoding", "unit", "semantics", "aggregation", "availability_rule",
      "availability", "reason", "value", "sample_time",
      ...(value.semantics === "monotonic_counter"
        ? ["counter_epoch", "counter_epoch_encoding", "rate", "rate_reason"] : [])];
    if (!hasOnlyKeys(value, allowedValueKeys)) {
      throw new Error("metric value contains undeclared fields");
    }
    const parsedTarget = targetIdentity(value.target, graph);
    if (value.graph_generation !== generation || !boundedString(value.metric_id, 128, false)) {
      throw new Error("metric value identity or generation is invalid");
    }
    const key = `${parsedTarget.wireKey}|metric|${encodeMetricIdentityPart(value.metric_id)}`;
    const schema = schemaByKey.get(key);
    if (valueKeys.has(key)) throw new Error("duplicate metric value identity");
    if (previousValueKey && compareUtf8(key, previousValueKey) < 0) {
      throw new Error("metric values are not deterministically ordered");
    }
    valueKeys.add(key);
    previousValueKey = key;
    if (!schema || value.scalar_type !== schema.scalar_type ||
        value.scalar_encoding !== schema.scalar_encoding || value.unit !== schema.unit ||
        value.semantics !== schema.semantics || value.aggregation !== schema.aggregation ||
        value.availability_rule !== schema.availability_rule ||
        (value.availability !== "available" && value.availability !== "unavailable") ||
        !boundedString(value.reason, 256)) {
      throw new Error("metric value does not match its schema");
    }
    const available = value.availability === "available";
    if (availability.state !== "available" && available) {
      throw new Error("stale or unavailable snapshot contains an available value");
    }
    const unavailableTimeValid = availability.state === "available"
      ? value.sample_time === null || validTimestamp(value.sample_time)
      : value.sample_time === null;
    if ((available && (!typedScalar(value.value, schema.scalar_type, schema.scalar_encoding) ||
                       !validTimestamp(value.sample_time))) ||
        (!available && (value.value !== null || !unavailableTimeValid ||
                        !boundedString(value.reason, 256, false)))) {
      throw new Error("metric typed value or availability is invalid");
    }
    let counterEpoch: string | null | undefined;
    let rate: number | null | undefined;
    let rateReason: string | undefined;
    if (schema.semantics === "monotonic_counter") {
      const epochValid = value.counter_epoch === null
        ? !available
        : typeof value.counter_epoch === "string" &&
          /^(?:0|[1-9][0-9]*)$/.test(value.counter_epoch) &&
          BigInt(value.counter_epoch) <= (1n << 64n) - 1n;
      const rateValid = value.rate === null ||
        (typeof value.rate === "number" && Number.isFinite(value.rate) && value.rate >= 0);
      const rateReasonValid = boundedString(value.rate_reason, 256);
      const rateStateConsistent = available
        ? (value.rate === null ? value.rate_reason !== "" : value.rate_reason === "")
        : value.rate === null;
      if (value.counter_epoch_encoding !== "decimal_string" || !epochValid ||
          !rateValid || !rateReasonValid || !rateStateConsistent ||
          (value.rate !== null && schema.aggregation !== "rate")) {
        throw new Error("counter metric state is invalid");
      }
      counterEpoch = value.counter_epoch as string | null;
      rate = value.rate as number | null;
      rateReason = value.rate_reason as string;
    }
    values.push({
      ...schema,
      availability: value.availability,
      reason: value.reason,
      value: value.value as MetricValue["value"],
      sample_time: value.sample_time as string | null,
      ...(counterEpoch === undefined ? {} : {
        counter_epoch: counterEpoch,
        counter_epoch_encoding: "decimal_string" as const,
        rate,
        rate_reason: rateReason,
      }),
    });
  }
  if (valueKeys.size !== schemaByKey.size ||
      [...schemaByKey.keys()].some((key) => !valueKeys.has(key))) {
    throw new Error("metric schemas and values are not one-to-one");
  }
  return {
    schema_version: 1,
    graph_generation: generation,
    active_revision: typeof data.active_revision === "number" ? data.active_revision : null,
    snapshot_sequence: data.snapshot_sequence as number,
    snapshot_time: data.snapshot_time as string,
    availability: availability as MetricsSnapshot["availability"],
    schemas,
    values,
    diagnostics: diagnostics as unknown as MetricsSnapshot["diagnostics"],
  };
}

export async function fetchMetricsSnapshot(
  graph: DisplayGraph,
  expectedGeneration: number | null,
  signal: AbortSignal,
  previous: { generation: number; sequence: number } | null = null,
): Promise<MetricsSnapshot> {
  const response = await fetch("/api/v1/metrics", { signal });
  const text = await response.text();
  if (new TextEncoder().encode(text).byteLength > METRIC_RESPONSE_BYTES) {
    throw new Error("metric response exceeds 1 MiB");
  }
  if (!response.ok) throw new Error(`metric snapshot HTTP ${response.status}`);
  validateMetricsRawSplitBounds(text);
  return parseMetricsSnapshot(JSON.parse(text) as unknown, graph, expectedGeneration, previous);
}

export function validateMetricsRawSplitBounds(text: string): void {
  const rawData = extractTopLevelJsonMember(text, "data");
  const rawSchemas = extractTopLevelJsonMember(rawData, "schemas");
  const rawValues = extractTopLevelJsonMember(rawData, "values");
  if (new TextEncoder().encode(rawSchemas).byteLength > METRIC_SCHEMA_BYTES ||
      new TextEncoder().encode(rawValues).byteLength > METRIC_VALUE_BYTES) {
    throw new Error("metric snapshot split raw payload bounds are invalid");
  }
}

export function metricText(value: MetricValue): string {
  if (value.availability !== "available") {
    return `${value.metric_id}: unavailable (${value.reason || "unspecified"})`;
  }
  const rendered = value.value === null ? "unavailable" : String(value.value);
  return `${value.metric_id}: ${rendered}${value.unit ? ` ${value.unit}` : ""}; available; sampled ${value.sample_time}`;
}

export function isExactAvailableActivity(value: MetricValue): boolean {
  if (value.identity.startsWith("edge:") && value.metric_id === "activity" &&
      value.availability === "available" && value.semantics === "gauge") {
    return value.value === true ||
      (typeof value.value === "number" && Number.isFinite(value.value) && value.value > 0);
  }
  return false;
}

export function aggregateMetricText(values: MetricValue[], explicitMemberCount?: number): string[] {
  const buckets = new Map<string, MetricValue[]>();
  for (const value of values) {
    const key = value.metric_id;
    const bucket = buckets.get(key) ?? [];
    bucket.push(value);
    buckets.set(key, bucket);
  }
  return [...buckets.values()].map((bucket) => {
    const first = bucket[0];
    const available = bucket.filter((value) => value.availability === "available");
    const memberCount = Math.max(bucket.length, explicitMemberCount ?? bucket.length);
    const prefix = `${first.metric_id}: ${available.length}/${memberCount} members available`;
    const unavailable = (reason: string) =>
      `${prefix}; unavailable (${reason.slice(0, 256)})`;
    const compatible = bucket.every((value) =>
      value.scalar_type === first.scalar_type && value.unit === first.unit &&
      value.scalar_encoding === first.scalar_encoding &&
      value.semantics === first.semantics && value.aggregation === first.aggregation &&
      value.availability_rule === first.availability_rule &&
      value.graph_generation === first.graph_generation &&
      (first.semantics !== "monotonic_counter" || value.counter_epoch === first.counter_epoch));
    if (!compatible) return unavailable("incompatible member metrics");
    if (bucket.length !== memberCount) return unavailable("members have no matching metric");
    if (first.aggregation === "none") {
      return unavailable("aggregation is declared none");
    }
    if (available.length !== memberCount) {
      return unavailable("one or more member metrics are unavailable");
    }
    if (first.aggregation === "rate" && first.semantics !== "monotonic_counter") {
      return unavailable("rate aggregation requires monotonic counters");
    }
    if ((first.scalar_type === "boolean" || first.scalar_type === "string") &&
        first.aggregation !== "none") {
      return unavailable(`${first.aggregation} aggregation does not support ${first.scalar_type} values`);
    }
    if ((first.scalar_type === "integer" || first.scalar_type === "unsigned") &&
        first.aggregation !== "rate") {
      const integers = available.map((value) => BigInt(String(value.value)));
      const exact = first.aggregation === "sum"
        ? integers.reduce((sum, value) => sum + value, 0n)
        : first.aggregation === "min"
          ? integers.reduce((result, value) => value < result ? value : result)
          : first.aggregation === "max"
            ? integers.reduce((result, value) => value > result ? value : result)
            : null;
      if (exact !== null) return `${prefix}; ${first.aggregation} ${exact}${first.unit ? ` ${first.unit}` : ""}`;
      const sum = integers.reduce((result, value) => result + value, 0n);
      const divisor = BigInt(integers.length);
      const average = sum % divisor === 0n ? String(sum / divisor) : `${sum}/${divisor}`;
      return `${prefix}; average ${average}${first.unit ? ` ${first.unit}` : ""}`;
    }
    const numbers = available.map((value) => first.aggregation === "rate"
      ? value.rate : typeof value.value === "number" ? value.value : null);
    if (numbers.some((value) => value === null || value === undefined)) {
      return unavailable(first.aggregation === "rate"
        ? "one or more counter rates are unavailable"
        : "one or more values cannot be aggregated");
    }
    const typed = numbers as number[];
    const aggregate = first.aggregation === "sum" || first.aggregation === "rate"
      ? typed.reduce((sum, value) => sum + value, 0)
      : first.aggregation === "min" ? Math.min(...typed)
        : first.aggregation === "max" ? Math.max(...typed)
          : typed.reduce((sum, value) => sum + value / typed.length, 0);
    if (!Number.isFinite(aggregate)) {
      return unavailable("aggregate result is not finite");
    }
    return `${prefix}; ${first.aggregation} ${aggregate}${first.unit ? ` ${first.unit}` : ""}`;
  }).slice(0, 64);
}
