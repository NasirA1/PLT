// The test harness itself: the helper that keeps made-up paths absolute on every platform.
//
// This exists because of a real failure. The tests used to build fixed paths with
// `path.join(path.sep, "work", "game")`. On Linux that is `/work/game` - absolute, and
// `path.resolve` leaves it alone. On Windows it is `\work\game`, which names the *current
// drive's* root, so the moment the code under test called `path.resolve` it became
// `C:\work\game` and seven tests failed with "expected '\\work\\game\\...' to equal
// 'C:\\work\\game\\...'" - on Windows only.

"use strict";

const path = require("path");

const { absolute, assert, suite, test } = require("./harness");

suite("harness: paths in test fixtures", () =>
{
    test("a fixture path is absolute and resolving it changes nothing", () =>
    {
        const folder = absolute("work", "game");
        assert.ok(path.isAbsolute(folder));
        assert.strictEqual(path.resolve(folder), folder, "the code under test resolves paths; this must be a no-op");
        assert.strictEqual(path.resolve(folder, "main.th"), path.join(folder, "main.th"));
    });

    test("the old way of writing one has no drive on Windows, which is what broke the tests", () =>
    {
        // Node's platform-specific path modules show both behaviours from either platform.
        const posix = path.posix.join(path.posix.sep, "work", "game");
        assert.strictEqual(path.posix.resolve(posix), posix, "on Linux the old fixtures were fine");

        const windows = path.win32.join(path.win32.sep, "work", "game");
        assert.strictEqual(windows, "\\work\\game");
        // isAbsolute says yes, so checking that would not have caught it...
        assert.ok(path.win32.isAbsolute(windows));
        // ... but the path names the *current drive's* root, so resolving it against anything
        // on a drive re-roots it there - and the expected value no longer matches.
        assert.strictEqual(path.win32.resolve("C:\\project", windows), "C:\\work\\game");
        assert.notStrictEqual(path.win32.resolve("C:\\project", windows), windows);
    });

    test("absolute() builds a path the platform's own rules call absolute", () =>
    {
        assert.ok(path.isAbsolute(absolute("one")));
        assert.ok(path.isAbsolute(absolute("one", "two", "three.th")));
        assert.strictEqual(path.basename(absolute("one", "two", "three.th")), "three.th");
        if (process.platform === "win32")
        {
            assert.match(absolute("one"), /^[A-Za-z]:\\one$/);
        }
        else
        {
            assert.strictEqual(absolute("one"), "/one");
        }
    });
});
