// Runs the extension's tests.
//
//   node ide/vscode/test/run.js [--filter <text>]
//
// Any Node.js will do. VS Code brings its own, so if none is installed:
//
//   ELECTRON_RUN_AS_NODE=1 /usr/share/code/code ide/vscode/test/run.js      (Linux)
//   $env:ELECTRON_RUN_AS_NODE=1; & "$env:LOCALAPPDATA\Programs\Microsoft VS Code\Code.exe" ...  (Windows)
//
// tests/python/test_vscode_extension.py finds one of those and runs this as part of the
// repository's own test suite.

"use strict";

const fs = require("fs");
const path = require("path");

const harness = require("./harness");

function parseArguments(argv)
{
    const options = { filter: "" };
    for (let i = 0; i < argv.length; ++i)
    {
        if (argv[i] === "--filter" || argv[i] === "-f")
        {
            options.filter = argv[++i] || "";
        }
    }
    return options;
}

async function main()
{
    const options = parseArguments(process.argv.slice(2));
    const files = fs.readdirSync(__dirname)
        .filter((name) => name.endsWith(".test.js"))
        .sort();
    for (const file of files)
    {
        require(path.join(__dirname, file));
    }
    const ok = await harness.runAll(options.filter);
    process.exit(ok ? 0 : 1);
}

main().catch((error) =>
{
    process.stderr.write(`${error && error.stack ? error.stack : error}\n`);
    process.exit(1);
});
