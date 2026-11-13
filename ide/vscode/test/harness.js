// A test harness with no dependencies: `suite`, `test`, `assert`, and the hook that makes
// `require("vscode")` return the stand-in.
//
// The extension has no build step and no node_modules on purpose - a student should be able to
// use it with nothing but VS Code installed - so its tests bring their own runner.

"use strict";

const Module = require("module");
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const EXTENSION = path.resolve(__dirname, "..");
const ROOT = path.resolve(EXTENSION, "..", "..");

// `require("vscode")` inside the extension's sources: hand back the stand-in.
const original = Module._load;
Module._load = function load(request, parent, isMain)
{
    if (request === "vscode")
    {
        return require("./fake-vscode");
    }
    return original.call(this, request, parent, isMain);
};

const suites = [];
let current = null;

function suite(name, body)
{
    current = { name, tests: [], before: null, after: null };
    suites.push(current);
    body();
    current = null;
}

function test(name, body)
{
    if (!current)
    {
        throw new Error(`test "${name}" is outside a suite`);
    }
    current.tests.push({ name, body });
}

function before(body)
{
    current.before = body;
}

/** Skips the rest of a suite when something it needs is missing (the toolchain, usually). */
class Skip extends Error {}

function skip(reason)
{
    throw new Skip(reason);
}

async function runAll(filter)
{
    let passed = 0;
    let failed = 0;
    let skipped = 0;
    const failures = [];
    for (const entry of suites)
    {
        if (filter && !entry.name.toLowerCase().includes(filter.toLowerCase()))
        {
            continue;
        }
        process.stdout.write(`\n${entry.name}\n`);
        let context = {};
        if (entry.before)
        {
            try
            {
                context = (await entry.before()) || {};
            }
            catch (error)
            {
                if (error instanceof Skip)
                {
                    process.stdout.write(`  - skipped: ${error.message}\n`);
                    skipped += entry.tests.length;
                    continue;
                }
                throw error;
            }
        }
        for (const item of entry.tests)
        {
            try
            {
                await item.body(context);
                ++passed;
                process.stdout.write(`  ok  ${item.name}\n`);
            }
            catch (error)
            {
                if (error instanceof Skip)
                {
                    ++skipped;
                    process.stdout.write(`  -   ${item.name} (skipped: ${error.message})\n`);
                    continue;
                }
                ++failed;
                failures.push({ suite: entry.name, test: item.name, error });
                process.stdout.write(`  FAIL ${item.name}\n`);
            }
        }
    }
    process.stdout.write(`\n${passed} passed, ${failed} failed, ${skipped} skipped\n`);
    for (const failure of failures)
    {
        process.stdout.write(`\n${failure.suite} > ${failure.test}\n`);
        process.stdout.write(`${failure.error && failure.error.stack ? failure.error.stack : failure.error}\n`);
    }
    return failed === 0;
}

/**
 * An absolute path for a made-up file, on this platform.
 *
 * `path.join(path.sep, "work", "main.th")` is NOT absolute on Windows: `\work\main.th` names
 * the current drive's root, and the moment the code under test calls `path.resolve` it becomes
 * `C:\work\main.th`, so a test that compares the two fails on Windows and passes on Linux.
 * Tests therefore build their fixed paths here, where `path.resolve` has already been applied.
 */
function absolute(...parts)
{
    return path.resolve(path.sep, ...parts);
}

/** Where thunderc and thundervm are, or null (the same search the extension does). */
function toolchainDirectory()
{
    const { findToolchain } = require(path.join(EXTENSION, "src", "toolchain"));
    const found = findToolchain({
        environment: process.env.THUNDER_BIN || "",
        folders: [ROOT],
    });
    return found.directory;
}

function requireToolchain()
{
    const directory = toolchainDirectory();
    if (!directory)
    {
        skip("thunderc and thundervm have not been built (set THUNDER_BIN)");
    }
    return directory;
}

/** A scratch directory that is emptied when the process ends. */
function temporaryDirectory(name)
{
    const directory = fs.mkdtempSync(path.join(require("os").tmpdir(), `thunder-vscode-test-${name}-`));
    process.on("exit", () =>
    {
        try
        {
            fs.rmSync(directory, { recursive: true, force: true });
        }
        catch (error)
        {
            // nothing to clean up
        }
    });
    return directory;
}

module.exports = {
    EXTENSION,
    ROOT,
    Skip,
    absolute,
    assert,
    before,
    requireToolchain,
    runAll,
    skip,
    suite,
    suites,
    temporaryDirectory,
    test,
    toolchainDirectory,
};
