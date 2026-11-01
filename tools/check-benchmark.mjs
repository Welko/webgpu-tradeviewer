#!/usr/bin/env node
/**
 * Catches changes in memory and speed between commits, from the numbers
 * BenchmarkApp writes to benchmark.json, against the baseline in
 * tools/benchmark-baseline.csv.
 *
 * The baseline only changes by hand, through `accept`: each row is a change
 * someone meant, with the commit that made it and why. The last row for a
 * platform is its baseline. Memory is the same on every machine, so it's
 * compared exactly with the platform's last row: any change fails, smaller
 * too. Times depend on the machine, so they're compared with the last row on
 * the same GPU, within a margin: much slower fails, and so does much faster,
 * so a gain is locked in before it can quietly be lost again. Numbers marked
 * flaky are shown, never checked.
 *
 * Usage:
 *   node tools/check-benchmark.mjs check <benchmark.json> [--summary <summary.md>] [--run-id <id>] [--artifact-url <url>]
 *     compares, writes the tables as Markdown (for a CI job summary), and
 *     exits with 1 on any change past the margins; in CI, --run-id and
 *     --artifact-url give the commands that accept the run, with gh and
 *     without
 *   node tools/check-benchmark.mjs accept <run-id | benchmark.json> <description>
 *     appends the numbers as the new baseline: a CI run's, downloaded with
 *     the GitHub CLI (gh), so the times are CI's own; or a file's. The commit
 *     measured is the one the file names (CI's do), else the one checked out
 */
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const baselinePath = join(root, 'tools', 'benchmark-baseline.csv');

/** How far times may stray from the baseline before they count. */
const Thresholds = {
  /** Slower than this many times the baseline fails, and so does faster than its inverse. */
  ratio: 1.5,
  /**
   * Differences smaller than this fail nothing, whatever the ratio: 6% of a
   * 60 Hz frame. CI's shared runners make sub-millisecond times jump by half
   * a millisecond between runs, doubling some; less wouldn't show in the
   * frame rate anyway.
   */
  floorMs: 1,
};

/** The columns before the numbers in the baseline. */
const RowColumns = ['date', 'commit', 'platform', 'gpu', 'trades', 'description'];

function readJson(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

// CSV, as RFC 4180 has it: fields with commas, quotes or line breaks are quoted.

function toCsvField(value) {
  const text = String(value ?? '');
  return /[",\n\r]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = '';
  let quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') {
        field += '"';
        i++;
      } else if (c === '"') {
        quoted = false;
      } else {
        field += c;
      }
    } else if (c === '"') {
      quoted = true;
    } else if (c === ',') {
      row.push(field);
      field = '';
    } else if (c === '\n') {
      row.push(field);
      rows.push(row);
      row = [];
      field = '';
    } else if (c !== '\r') {
      field += c;
    }
  }
  if (field !== '' || row.length > 0) {
    row.push(field);
    rows.push(row);
  }
  return rows;
}

/** The baseline as { header, rows }, each row an object by column; empty if there's none yet. */
function readBaseline() {
  if (!existsSync(baselinePath)) {
    return { header: [], rows: [] };
  }
  const [header = [], ...lines] = parseCsv(readFileSync(baselinePath, 'utf8'));
  return { header, rows: lines.map((line) => Object.fromEntries(header.map((column, i) => [column, line[i] ?? '']))) };
}

function formatValue(value, unit) {
  return unit === 'bytes' ? `${Math.round(value).toLocaleString('en-US')} B` : `${value.toFixed(3)} ms`;
}

function formatChange(now, before, unit) {
  // Memory changes by a few bytes too, which a percentage rounds away.
  if (unit === 'bytes') {
    const difference = Math.round(now - before);
    return `${difference > 0 ? '+' : ''}${difference.toLocaleString('en-US')} B`;
  }
  if (before === 0) {
    return now === 0 ? '0%' : 'new';
  }
  const percent = ((now - before) / before) * 100;
  return `${percent > 0 ? '+' : ''}${percent.toFixed(1)}%`;
}

/** Where a baseline row came from, for the line above its table. */
function describeRow(row) {
  const repository = process.env.GITHUB_REPOSITORY;
  const commit = repository && /^[0-9a-f]+$/.test(row.commit) ? `[${row.commit}](https://github.com/${repository}/commit/${row.commit})` : `\`${row.commit}\``;
  return `${commit} (${row.date.slice(0, 10)}): ${row.description}`;
}

/** Whether a measurement differs too much from the baseline's, and how. */
function judge(metric, before) {
  if (before === undefined) {
    return { verdict: '❌ not in the baseline', failed: true };
  }
  if (metric.unit === 'bytes') {
    if (metric.value === before) {
      return { verdict: '✅', failed: false };
    }
    return { verdict: metric.value > before ? '❌ more' : '❌ less', failed: true };
  }
  if (Math.abs(metric.value - before) > Thresholds.floorMs) {
    if (metric.value > before * Thresholds.ratio) {
      return { verdict: '❌ slower', failed: true };
    }
    if (metric.value < before / Thresholds.ratio) {
      return { verdict: '❌ faster', failed: true };
    }
  }
  return { verdict: '✅', failed: false };
}

/** One table's rows against a baseline row (or none), and whether any failed. */
function compare(metrics, row) {
  const lines = [];
  let failed = false;
  for (const metric of metrics) {
    const parsed = row ? Number.parseFloat(row[metric.key]) : Number.NaN;
    const before = Number.isFinite(parsed) ? parsed : undefined;
    const judged = row ? judge(metric, before) : { verdict: '', failed: false };
    // Flaky numbers are shown like the rest, never judged.
    const verdict = metric.flaky ? 'recorded only' : judged.verdict;
    failed ||= !metric.flaky && judged.failed;
    const shown = before === undefined ? '–' : formatValue(before, metric.unit);
    const change = before === undefined ? '–' : formatChange(metric.value, before, metric.unit);
    lines.push(`| ${metric.label} | ${shown} | ${formatValue(metric.value, metric.unit)} | ${change} | ${verdict} |`);
  }
  return { lines, failed };
}

const TableHeader = '| | Baseline | Now | Change | |\n|---|--:|--:|--:|---|';
const FlakyNote =
  '_Recorded only_: these numbers vary too much from run to run to fail a build on, so they\'re shown for reference and never checked.';

/** A table's Markdown: what it's against, its rows, and a note on any flaky ones. */
function section(title, against, metrics, compared) {
  const parts = [title, against, '', TableHeader, ...compared.lines];
  if (metrics.some((m) => m.flaky)) {
    parts.push('', FlakyNote);
  }
  return parts;
}

/** The commands that make these numbers the baseline: from CI's artifact, downloaded by hand or with gh, or from the file. */
function describeAccepting(resultsPath, runId, artifactUrl) {
  const command = (source) => `node tools/check-benchmark.mjs accept ${source} "<what changed and why>"`;
  const parts = ['', '### These numbers changed', 'If that\'s meant, make them the baseline, saying why, and commit `tools/benchmark-baseline.csv`.'];
  if (artifactUrl) {
    parts.push(
      '',
      `Download [this run's \`benchmark\` artifact](${artifactUrl}), then:`,
      '',
      '```bash',
      'unzip -o ~/Downloads/benchmark.zip -d ~/Downloads/benchmark',
      command('~/Downloads/benchmark/benchmark.json'),
      '```',
    );
  }
  if (runId) {
    parts.push('', `${artifactUrl ? 'Or, with' : 'With'} the GitHub CLI (\`gh\`) signed in, which downloads it:`, '', '```bash', command(runId), '```');
  }
  if (!artifactUrl && !runId) {
    parts.push('', '```bash', command(resultsPath), '```');
  }
  return parts;
}

function check(resultsPath, summaryPath, runId, artifactUrl) {
  const results = readJson(resultsPath);
  const { rows } = readBaseline();
  const platform = rows.filter((row) => row.platform === results.platform);
  const memoryRow = platform.at(-1);
  const timesRow = platform.filter((row) => row.gpu === results.gpu).at(-1);

  const memoryMetrics = results.metrics.filter((m) => m.unit === 'bytes');
  const timeMetrics = results.metrics.filter((m) => m.unit === 'ms');
  const memory = compare(memoryMetrics, memoryRow);
  const times = compare(timeMetrics, timesRow);
  // No baseline at all fails too, unless nothing in it could be checked: a
  // check that quietly stops checking is worse than none.
  const memoryFailed = memory.failed || (!memoryRow && memoryMetrics.some((m) => !m.flaky));
  const timesFailed = times.failed || (!timesRow && timeMetrics.some((m) => !m.flaky));

  const parts = [
    ...section(
      `### Memory, the synthetic day (${results.platform})`,
      memoryRow
        ? `Exactly against the baseline from ${describeRow(memoryRow)}`
        : `❌ The baseline has nothing for ${results.platform} yet.`,
      memoryMetrics,
      memory,
    ),
    '',
    ...section(
      `### Times on ${results.gpu}, ${results.trades.toLocaleString('en-US')} trades`,
      timesRow
        ? `Against the baseline from ${describeRow(timesRow)}. ` +
            `More than ${Thresholds.ratio}× slower or faster (and ${Thresholds.floorMs} ms) fails.`
        : `❌ The baseline has no times on this GPU yet.`,
      timeMetrics,
      times,
    ),
  ];
  if (memoryFailed || timesFailed) {
    parts.push(...describeAccepting(resultsPath, runId, artifactUrl));
  }
  const summary = `${parts.join('\n')}\n`;
  if (summaryPath) {
    writeFileSync(summaryPath, summary);
  }
  console.log(summary);
  return memoryFailed || timesFailed ? 1 : 0;
}

function git(...args) {
  return execFileSync('git', args, { cwd: root, encoding: 'utf8' }).trim();
}

/** A CI run's benchmark.json, downloaded with gh. */
function fetchRun(runId) {
  const directory = mkdtempSync(join(tmpdir(), 'benchmark-'));
  try {
    execFileSync('gh', ['run', 'download', runId, '--name', 'benchmark', '--dir', directory], { cwd: root, stdio: 'inherit' });
    return readJson(join(directory, 'benchmark.json'));
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}

/** The commit checked out, marked if it has changes on top: what a local run measured. */
function getLocalCommit() {
  const dirty = git('status', '--porcelain', '--untracked-files=no') !== '';
  return `${git('rev-parse', '--short=7', 'HEAD')}${dirty ? '-dirty' : ''}`;
}

function accept(source, description) {
  const results = existsSync(source) ? readJson(source) : fetchRun(source);
  const commit = results.commit?.slice(0, 7) ?? getLocalCommit();
  const baseline = readBaseline();
  // New numbers get columns at the end; earlier rows leave them empty.
  const header = baseline.header.length > 0 ? [...baseline.header] : [...RowColumns];
  for (const metric of results.metrics) {
    if (!header.includes(metric.key)) {
      header.push(metric.key);
    }
  }
  const row = {
    date: new Date().toISOString().replace(/\.\d+Z$/, 'Z'),
    commit,
    platform: results.platform,
    gpu: results.gpu,
    trades: results.trades,
    description,
    ...Object.fromEntries(results.metrics.map((m) => [m.key, m.unit === 'ms' ? m.value.toFixed(4) : m.value])),
  };
  const lines = [header, ...baseline.rows.map((old) => header.map((column) => old[column])), header.map((column) => row[column])];
  writeFileSync(baselinePath, lines.map((line) => line.map(toCsvField).join(',')).join('\n') + '\n');
  console.log(`The baseline for ${results.platform} on ${results.gpu} is now ${commit}'s. Commit ${baselinePath}.`);
  return 0;
}

function option(args, name) {
  const at = args.indexOf(name);
  return at >= 0 ? args[at + 1] : undefined;
}

const [command, ...args] = process.argv.slice(2);
if (command === 'check' && args[0]) {
  process.exit(check(args[0], option(args, '--summary'), option(args, '--run-id'), option(args, '--artifact-url')));
} else if (command === 'accept' && args[0] && args[1]?.trim()) {
  process.exit(accept(args[0], args[1].trim()));
} else {
  console.error(readFileSync(fileURLToPath(import.meta.url), 'utf8').match(/Usage:[\s\S]*?(?=\n \*\/)/)[0].replace(/\n \*/g, '\n'));
  process.exit(2);
}
