// The Thunder project model.
//
// A folder is a Thunder project when it contains a `thunder.json`. Everything in that file is
// optional: it only says which program is the entry point, which language it is written in and
// where its modules and native libraries live, so that Run, Debug and Package work from any
// file in the project and a workspace with several folders can hold several programs.
//
// Nothing here touches the VS Code API or the file system (except through the `read` function
// handed to `loadProject`), so the whole model is unit-tested.

"use strict";

const path = require("path");

const PROJECT_FILE = "thunder.json";

/** The settings a project file may set, with the values a plain folder of .th files gets. */
function defaults()
{
    return {
        name: "",
        entry: "",
        language: "auto",
        modulePaths: [],
        nativePaths: [],
        display: "",
        arguments: [],
        output: "",
        package: "",
        maxSteps: 0,
    };
}

/**
 * Reads a project description.
 *
 * `text` is the contents of thunder.json (or null when the folder has none) and `folder` the
 * folder it belongs to. Relative paths inside it are resolved against that folder. Unknown
 * keys are kept, so a newer project file does not lose anything here, and a broken file is
 * reported rather than thrown: an editor must stay usable.
 */
function parseProject(text, folder)
{
    const project = Object.assign(defaults(), { folder, file: text === null ? "" : path.join(folder, PROJECT_FILE) });
    if (text === null || text === undefined)
    {
        return { project, error: null };
    }
    let data = null;
    try
    {
        data = JSON.parse(stripComments(text));
    }
    catch (error)
    {
        return { project, error: `${PROJECT_FILE}: ${error.message}` };
    }
    if (data === null || typeof data !== "object" || Array.isArray(data))
    {
        return { project, error: `${PROJECT_FILE}: expected an object` };
    }
    for (const key of Object.keys(data))
    {
        project[key] = data[key];
    }
    project.folder = folder;
    project.file = path.join(folder, PROJECT_FILE);
    project.modulePaths = resolveAll(project.modulePaths, folder);
    project.nativePaths = resolveAll(project.nativePaths, folder);
    project.arguments = (project.arguments || []).map(String);
    if (project.entry)
    {
        project.entry = path.resolve(folder, project.entry);
    }
    if (project.output)
    {
        project.output = path.resolve(folder, project.output);
    }
    if (project.package)
    {
        project.package = path.resolve(folder, project.package);
    }
    return { project, error: null };
}

/** Allows // comments, which VS Code's own JSON editors write. */
function stripComments(text)
{
    let out = "";
    let inString = false;
    let escaped = false;
    for (let i = 0; i < text.length; ++i)
    {
        const character = text[i];
        if (inString)
        {
            out += character;
            if (escaped)
            {
                escaped = false;
            }
            else if (character === "\\")
            {
                escaped = true;
            }
            else if (character === "\"")
            {
                inString = false;
            }
            continue;
        }
        if (character === "\"")
        {
            inString = true;
            out += character;
            continue;
        }
        if (character === "/" && text[i + 1] === "/")
        {
            while (i < text.length && text[i] !== "\n")
            {
                ++i;
            }
            out += "\n";
            continue;
        }
        if (character === "/" && text[i + 1] === "*")
        {
            i += 2;
            while (i < text.length && !(text[i] === "*" && text[i + 1] === "/"))
            {
                ++i;
            }
            ++i;
            continue;
        }
        out += character;
    }
    return out;
}

function resolveAll(values, folder)
{
    return (values || []).map((value) => path.resolve(folder, String(value)));
}

/**
 * Which program a command should work on.
 *
 * The active editor wins when it holds a Thunder file - that is what the user is looking at -
 * then the project's entry point, then a program called main.th. `active` and `entry` are
 * absolute paths or null; `programs` are every .th file of the project.
 */
function programFor(active, project, programs)
{
    if (active)
    {
        return active;
    }
    if (project && project.entry)
    {
        return project.entry;
    }
    const candidates = programs || [];
    const named = candidates.find((file) => path.basename(file) === "main.th")
        || candidates.find((file) => /(^|[\\/])(main|program|app)\.th$/i.test(file));
    if (named)
    {
        return named;
    }
    return candidates.length === 1 ? candidates[0] : null;
}

/** Where the bytecode of a program goes: the project's `output`, else beside the source. */
function outputFor(source, project)
{
    if (project && project.output)
    {
        // A directory ending in a separator (or an existing directory) takes the source's name.
        if (project.output.endsWith(path.sep) || !path.extname(project.output))
        {
            return path.join(project.output, path.basename(source, path.extname(source)) + ".thc");
        }
        return project.output;
    }
    return path.join(path.dirname(source), path.basename(source, path.extname(source)) + ".thc");
}

/** Where `Package as an application` writes the executable. */
function packageFor(source, project, configuredDirectory)
{
    const base = path.basename(source, path.extname(source));
    if (project && project.package)
    {
        return project.package;
    }
    if (configuredDirectory)
    {
        return path.resolve(project ? project.folder : path.dirname(source), configuredDirectory, base);
    }
    return path.join(path.dirname(source), base);
}

/**
 * The directories imported modules are looked for in, in search order: the directory of the
 * program itself (which the compiler also searches), the project's module paths and the
 * directories from the settings.
 */
function modulePathsFor(source, project, configured)
{
    const directories = [path.dirname(source)];
    for (const directory of (project ? project.modulePaths : []))
    {
        directories.push(directory);
    }
    for (const directory of (configured || []))
    {
        directories.push(path.isAbsolute(directory)
            ? directory
            : path.resolve(project ? project.folder : path.dirname(source), directory));
    }
    return directories.filter((directory, index) => directories.indexOf(directory) === index);
}

/** The contents of a new project file. */
function projectTemplate(name, language, entry)
{
    return {
        $schema: "./.vscode/thunder-project.schema.json",
        name,
        entry: entry || "main.th",
        language: language || "en",
        modulePaths: ["lib"],
        nativePaths: [],
        arguments: [],
    };
}

module.exports = {
    PROJECT_FILE,
    defaults,
    modulePathsFor,
    outputFor,
    packageFor,
    parseProject,
    programFor,
    projectTemplate,
    stripComments,
};
