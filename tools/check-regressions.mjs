#!/usr/bin/env node
/**
 * Catches regressions in memory and speed between commits, from the numbers
 * the benchmarks write to benchmark.json, against tools/regression-baseline.csv:
 * BenchmarkApp natively, web/scripts/benchmark.mjs in Chromium. Each platform
 * ("native", "web") has rows of its own.
 *
 * The baseline only changes by hand, through `accept`: each row is a change
 * someone meant, with the commit that made it and why. Memory is the same on
 * every machine, so it's compared exactly with the platform's last row: any
 * change fails, smaller too. Times depend on the machine, and CI's runners
 * differ by up to twice in speed, so the baseline keeps no times: CI builds
 * the commit that `baseline-commit` names, benchmarks both commits on the same
 * machine, and compares them. Much slower fails, and so does much faster, so
 * a gain is locked in before it can quietly be lost again. Numbers marked
 * flaky are shown, never checked.
 *
 * Usage:
 *   node tools/check-regressions.mjs check <benchmark.json> [--baseline <benchmark.json>] [--summary <summary.md>]
 *                                          [--run-id <id>] [--artifact-url <url>]
 *     compares, writes the tables as Markdown (for a CI job summary), and
 *     exits with 1 on any change past the margins. --baseline is the
 *     baseline's commit measured on the same machine; without it, times
 *     aren't compared. In CI, --run-id and --artifact-url give the commands
 *     that accept the run, with gh and without
 *   node tools/check-regressions.mjs median <out.json> <benchmark.json>...
 *     writes each number's median over several runs of one commit
 *   node tools/check-regressions.mjs baseline-commit <platform>
 *     prints the commit to compare times with: the platform's last row's,
 *     skipping rows measured with uncommitted changes, which can't be built
 *   node tools/check-regressions.mjs accept <run-id | benchmark.json> <description> [--platform <platform>]
 *     appends the memory as the new baseline, and the commit as the one to
 *     compare times with: a CI run's, downloaded with the GitHub CLI (gh)
 *     from its benchmark-<platform> artifact (native by default), or a
 *     file's. The commit measured is the one the file names (CI's do), else
 *     the one checked out
 */
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const baselinePath = join(root, 'tools', 'regression-baseline.csv');

/** How far times may stray from the baseline's commit before they count. */
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

/** The columns before the memory in the baseline. */
const RowColumns = ['date', 'commit', 'platform', 'description'];

/** A commit that can be built again: not one with uncommitted changes, which is marked "-dirty". */
const BuildableCommit = /^[0-9a-f]{7,40}$/;

function readJson(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
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

/** The row whose commit times are compared with: the platform's last one that can be built again. */
function getBaselineRow(rows, platform) {
  return rows.filter((row) => row.platform === platform && BuildableCommit.test(row.commit)).at(-1);
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
  const commit = repository && BuildableCommit.test(row.commit) ? `[${row.commit}](https://github.com/${repository}/commit/${row.commit})` : `\`${row.commit}\``;
  return `${commit} (${row.date.slice(0, 10)}): ${row.description}`;
}

/** Whether a measurement differs too much from the one before, and how. */
function judge(metric, before) {
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

/**
 * One table's rows, each against its number before (`getBefore`, undefined if
 * there's none, which is judged `missing`), and whether any failed.
 */
function compare(metrics, getBefore, missing) {
  const lines = [];
  let failed = false;
  for (const metric of metrics) {
    const parsed = Number.parseFloat(getBefore(metric));
    const before = Number.isFinite(parsed) ? parsed : undefined;
    const judged = before === undefined ? missing : judge(metric, before);
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

/** "the median of 3 runs", or nothing for a single run. */
function describeRuns(results) {
  return results.runs > 1 ? `, the median of ${results.runs} runs` : '';
}

/** CI's artifact holding a platform's benchmark.json. */
function getArtifactName(platform) {
  return `benchmark-${platform}`;
}

/** The commands that make these numbers the baseline: from CI's artifact, downloaded by hand or with gh, or from the file. */
function describeAccepting(resultsPath, platform, runId, artifactUrl) {
  const command = (source, extra = '') => `node tools/check-regressions.mjs accept ${source} "<what changed and why>"${extra}`;
  const artifact = getArtifactName(platform);
  const parts = ['', '### These numbers changed', 'If that\'s meant, make them the baseline, saying why, and commit `tools/regression-baseline.csv`.'];
  if (artifactUrl) {
    parts.push(
      '',
      `Download [this run's \`${artifact}\` artifact](${artifactUrl}), then:`,
      '',
      '```bash',
      `unzip -o ~/Downloads/${artifact}.zip -d ~/Downloads/${artifact}`,
      command(`~/Downloads/${artifact}/benchmark.json`),
      '```',
    );
  }
  if (runId) {
    const withGh = command(runId, ` --platform ${platform}`);
    parts.push('', `${artifactUrl ? 'Or, with' : 'With'} the GitHub CLI (\`gh\`) signed in, which downloads it:`, '', '```bash', withGh, '```');
  }
  if (!artifactUrl && !runId) {
    parts.push('', '```bash', command(resultsPath), '```');
  }
  return parts;
}

function check(resultsPath, baselineRunPath, summaryPath, runId, artifactUrl) {
  const results = readJson(resultsPath);
  const baselineRun = baselineRunPath ? readJson(baselineRunPath) : undefined;
  const { rows } = readBaseline();
  const memoryRow = rows.filter((row) => row.platform === results.platform).at(-1);
  const baselineRow = getBaselineRow(rows, results.platform);

  const memoryMetrics = results.metrics.filter((m) => m.unit === 'bytes');
  const timeMetrics = results.metrics.filter((m) => m.unit === 'ms');
  // No baseline fails too: a check that quietly stops checking is worse than none.
  const memory = compare(memoryMetrics, (m) => memoryRow?.[m.key], { verdict: '❌ not in the baseline', failed: true });
  // The baseline's commit may not measure a number yet: nothing to compare it with.
  const baselineTimes = new Map((baselineRun?.metrics ?? []).map((m) => [m.key, m.value]));
  const times = compare(timeMetrics, (m) => baselineTimes.get(m.key), { verdict: baselineRun ? 'new' : '', failed: false });

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
      `### Times on ${results.gpu}${results.cpu ? `, ${results.cpu}` : ''}, ${results.trades.toLocaleString('en-US')} trades`,
      baselineRun
        ? `Against the baseline's commit, ${baselineRow ? describeRow(baselineRow) : 'as given'}, measured on this same machine in this run` +
            `${describeRuns(baselineRun)}; now${describeRuns(results) || ', one run'}. ` +
            `More than ${Thresholds.ratio}× slower or faster (and ${Thresholds.floorMs} ms) fails.`
        : 'Not compared: no measurement of the baseline\'s commit on this machine (`--baseline`).',
      timeMetrics,
      times,
    ),
  ];
  const failed = memory.failed || times.failed;
  if (failed) {
    parts.push(...describeAccepting(resultsPath, results.platform, runId, artifactUrl));
  }
  const summary = `${parts.join('\n')}\n`;
  if (summaryPath) {
    writeFileSync(summaryPath, summary);
  }
  console.log(summary);
  return failed ? 1 : 0;
}

/** Several runs of one commit as one: the first's details, and each number's median. */
function writeMedians(outPath, runPaths) {
  const runs = runPaths.map(readJson);
  const metrics = runs[0].metrics.map((metric) => ({
    ...metric,
    value: median(runs.map((run) => run.metrics.find((m) => m.key === metric.key)?.value).filter(Number.isFinite)),
  }));
  writeFileSync(outPath, `${JSON.stringify({ ...runs[0], runs: runs.length, metrics }, null, 2)}\n`);
  return 0;
}

function git(...args) {
  return execFileSync('git', args, { cwd: root, encoding: 'utf8' }).trim();
}

/** A CI run's benchmark.json for `platform`, downloaded with gh. */
function fetchRun(runId, platform) {
  const directory = mkdtempSync(join(tmpdir(), 'benchmark-'));
  try {
    try {
      execFileSync('gh', ['run', 'download', runId, '--name', getArtifactName(platform), '--dir', directory], { cwd: root, stdio: 'inherit' });
    } catch (error) {
      if (error.code === 'ENOENT') {
        console.error(`Downloading run ${runId} needs the GitHub CLI (gh), which isn't installed. Download its ${getArtifactName(platform)} artifact from the run's page and pass its benchmark.json instead.`);
        process.exit(1);
      }
      throw error;
    }
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

function accept(source, description, platform) {
  // A run id is all digits; anything else is a file, which must be there.
  if (!existsSync(source) && !/^\d+$/.test(source)) {
    console.error(`No such file: ${source}`);
    return 1;
  }
  // A run id is all digits; anything else is a file, which must exist.
  if (!/^\d+$/.test(source) && !existsSync(source)) {
    console.error(`No such file: ${source}`);
    return 1;
  }
  const results = /^\d+$/.test(source) ? fetchRun(source, platform) : readJson(source);
  const commit = results.commit?.slice(0, 7) ?? getLocalCommit();
  const memory = results.metrics.filter((m) => m.unit === 'bytes');
  const baseline = readBaseline();
  // The row's columns first, then the memory; new numbers get columns at the
  // end, and earlier rows leave them empty.
  const header = [...RowColumns, ...baseline.header.filter((column) => !RowColumns.includes(column))];
  for (const metric of memory) {
    if (!header.includes(metric.key)) {
      header.push(metric.key);
    }
  }
  const row = {
    date: new Date().toISOString().replace(/\.\d+Z$/, 'Z'),
    commit,
    platform: results.platform,
    description,
    ...Object.fromEntries(memory.map((m) => [m.key, m.value])),
  };
  const lines = [header, ...baseline.rows.map((old) => header.map((column) => old[column])), header.map((column) => row[column])];
  writeFileSync(baselinePath, lines.map((line) => line.map(toCsvField).join(',')).join('\n') + '\n');
  console.log(`The baseline for ${results.platform} is now ${commit}. Commit ${baselinePath}.`);
  return 0;
}

function option(args, name) {
  const at = args.indexOf(name);
  return at >= 0 ? args[at + 1] : undefined;
}

const [command, ...args] = process.argv.slice(2);
if (command === 'check' && args[0]) {
  process.exit(
    check(args[0], option(args, '--baseline'), option(args, '--summary'), option(args, '--run-id'), option(args, '--artifact-url')),
  );
} else if (command === 'median' && args.length >= 2) {
  process.exit(writeMedians(args[0], args.slice(1)));
} else if (command === 'baseline-commit' && args[0]) {
  const row = getBaselineRow(readBaseline().rows, args[0]);
  if (!row) {
    console.error(`The baseline has no row for ${args[0]} with a commit to build.`);
    process.exit(1);
  }
  console.log(row.commit);
  process.exit(0);
} else if (command === 'accept' && args[0] && args[1]?.trim()) {
  process.exit(accept(args[0], args[1].trim(), option(args, '--platform') ?? 'native'));
} else {
  console.error(readFileSync(fileURLToPath(import.meta.url), 'utf8').match(/Usage:[\s\S]*?(?=\n \*\/)/)[0].replace(/\n \*/g, '\n'));
  process.exit(2);
}
