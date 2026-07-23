import { readdir, readFile } from 'node:fs/promises';
import { extname, join } from 'node:path';

async function visit(directory) {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) await visit(path);
    else if (['.ts', '.tsx', '.css', '.html'].includes(extname(path))) {
      const text = await readFile(path, 'utf8');
      if (text.includes('\t') || text.split('\n').some((line) => /\s+$/.test(line))) {
        throw new Error(`format violation: ${path}`);
      }
    }
  }
}

await visit('src');
