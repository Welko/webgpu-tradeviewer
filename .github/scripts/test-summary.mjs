// Prints a Markdown summary of the JUnit XML files under a directory: one
// row per file, then the details of each failure. CI appends it to the run's
// summary page.
//
//   node .github/scripts/test-summary.mjs <results dir>

import { readdirSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';

const MaxFailureLength = 4000;

// File paths in failures, relative to the checkout.
const workspace = process.env.GITHUB_WORKSPACE ? `${process.env.GITHUB_WORKSPACE}/` : null;

/** Every .xml file under `dir`, sorted. */
function xmlFiles(dir) {
  return readdirSync(dir, { recursive: true })
    .filter((path) => path.endsWith('.xml'))
    .sort()
    .map((path) => join(dir, path));
}

function decode(text) {
  return text
    .replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, '$1')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&amp;/g, '&');
}

function escapeHtml(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function attribute(attributes, name) {
  const match = attributes.match(new RegExp(`\\b${name}="([^"]*)"`));
  return match ? decode(match[1]) : '';
}

/** The test cases in a JUnit XML document, with their failures' text. */
function testCases(xml) {
  const cases = [];
  for (const [, attributes, , body = ''] of xml.matchAll(/<testcase\b([^>]*?)(\/>|>([\s\S]*?)<\/testcase>)/g)) {
    const failures = [...body.matchAll(/<(failure|error)\b([^>]*?)(?:\/>|>([\s\S]*?)<\/\1>)/g)].map(
      ([, , failureAttributes, text = '']) => decode(text).trim() || attribute(failureAttributes, 'message'),
    );
    cases.push({
      name: attribute(attributes, 'name'),
      seconds: Number(attribute(attributes, 'time')) || 0,
      failures,
      skipped: /<skipped\b/.test(body),
    });
  }
  return cases;
}

const dir = process.argv[2];
if (!dir) {
  console.error('Usage: test-summary.mjs <results dir>');
  process.exit(2);
}

let files = [];
try {
  files = xmlFiles(dir);
} catch {
  // No results at all: the builds failed before any test ran.
}

const rows = [];
const failures = [];
const totals = { passed: 0, failed: 0, skipped: 0 };
for (const file of files) {
  const suite = relative(dir, file).replace(/\.xml$/, '');
  const cases = testCases(readFileSync(file, 'utf8'));
  const failed = cases.filter((testCase) => testCase.failures.length > 0);
  const skipped = cases.filter((testCase) => testCase.skipped && testCase.failures.length === 0);
  const passed = cases.length - failed.length - skipped.length;
  const seconds = cases.reduce((sum, testCase) => sum + testCase.seconds, 0);

  totals.passed += passed;
  totals.failed += failed.length;
  totals.skipped += skipped.length;
  rows.push(`| ${failed.length ? '❌' : '✅'} | ${suite} | ${passed} | ${failed.length} | ${skipped.length} | ${seconds.toFixed(2)} s |`);
  for (const testCase of failed) {
    const text = testCase.failures.join('\n\n');
    failures.push({ suite, name: testCase.name, text: workspace ? text.replaceAll(workspace, '') : text });
  }
}

const lines = ['## Tests', ''];
if (rows.length === 0) {
  lines.push('No test results: the builds failed before any test ran.');
} else {
  const status = totals.failed ? `❌ ${totals.failed} failed` : '✅ All passed';
  lines.push(`${status}: ${totals.passed} passed, ${totals.failed} failed, ${totals.skipped} skipped.`, '');
  lines.push('| | Tests | Passed | Failed | Skipped | Time |', '|---|---|--:|--:|--:|--:|', ...rows);
}
if (failures.length) {
  lines.push('', '### Failures');
  for (const { suite, name, text } of failures) {
    const shown = text.length > MaxFailureLength ? `${text.slice(0, MaxFailureLength)}\n…` : text;
    lines.push('', `<details><summary>${escapeHtml(`${suite}: ${name}`)}</summary>`, '', '```', shown, '```', '', '</details>');
  }
}
console.log(lines.join('\n'));
