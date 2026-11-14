// Thunder tasks: build, run, package and check, as entries in "Run Task".
//
// A task is the honest way to build in VS Code: it shows the real command line, streams the
// real output into a terminal (so `input()` can read what the user types) and hands the
// compiler's diagnostics to the Problems panel through the `$thunderc` problem matcher.

"use strict";

const path = require("path");
const vscode = require("vscode");

const { compileCommand, runCommand } = require("./toolchain");
const { outputFor, packageFor } = require("./project");

const TASK_TYPE = "thunder";

class TaskProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideTasks()
    {
        const tasks = [];
        for (const folder of (vscode.workspace.workspaceFolders || []))
        {
            const project = this.services.projectFor(folder.uri);
            const program = project.entry || this.services.activeProgram();
            if (!program)
            {
                continue;
            }
            for (const task of ["build", "run", "check", "package"])
            {
                const created = await this.createTask({ type: TASK_TYPE, task, file: program }, folder);
                if (created)
                {
                    tasks.push(created);
                }
            }
        }
        return tasks;
    }

    async resolveTask(task)
    {
        const definition = task.definition;
        if (!definition || definition.type !== TASK_TYPE)
        {
            return undefined;
        }
        const folder = task.scope && task.scope.uri ? task.scope : (vscode.workspace.workspaceFolders || [])[0];
        return this.createTask(definition, folder, task.name);
    }

    /** Builds the VS Code task that runs one Thunder command. */
    async createTask(definition, folder, name)
    {
        const toolchain = this.services.toolchain();
        if (!toolchain)
        {
            return undefined;
        }
        const file = definition.file
            ? this.services.resolveVariables(definition.file, folder)
            : (this.services.activeProgram() || (folder ? this.services.projectFor(folder.uri).entry : ""));
        if (!file)
        {
            return undefined;
        }
        const project = folder ? this.services.projectFor(folder.uri) : null;
        const configuration = this.services.configuration(vscode.Uri.file(file));
        const language = await this.services.languageCodeFor(file);
        const modulePaths = this.services.modulePathsFor(vscode.Uri.file(file));
        const output = outputFor(file, project);
        let command = [];
        switch (definition.task)
        {
        case "check":
            command = compileCommand(toolchain.thunderc, {
                source: file, language, modulePaths, noOutput: true,
            });
            break;
        case "package":
            command = compileCommand(toolchain.thunderc, {
                source: file,
                language,
                modulePaths,
                package: packageFor(file, project, configuration.get("packageOutputDirectory")),
                display: configuration.get("display"),
            });
            break;
        case "run":
            command = compileCommand(toolchain.thunderc, { source: file, language, modulePaths, output, quiet: true });
            break;
        default:
            command = compileCommand(toolchain.thunderc, { source: file, language, modulePaths, output });
            break;
        }

        const commands = [command];
        if (definition.task === "run")
        {
            commands.push(runCommand(toolchain.thundervm, {
                bytecode: output,
                arguments: argumentsFor(definition, project, configuration),
                maxSteps: configuration.get("maxSteps"),
                display: configuration.get("display"),
                nativePaths: this.services.nativePathsFor(vscode.Uri.file(file)),
            }));
        }
        // One terminal, the commands one after the other: compile, and run only if that worked.
        const shell = new vscode.ShellExecution(commandLine(commands, taskShellKind()));

        const task = new vscode.Task(
            definition,
            folder || vscode.TaskScope.Workspace,
            name || `${definition.task}: ${path.basename(file)}`,
            "thunder",
            shell,
            ["$thunderc"]);
        task.group = definition.task === "run" ? vscode.TaskGroup.Test : vscode.TaskGroup.Build;
        task.presentationOptions = {
            reveal: vscode.TaskRevealKind.Always,
            panel: vscode.TaskPanelKind.Dedicated,
            clear: true,
            echo: true,
        };
        return task;
    }
}

/** The arguments the program gets: the task's, else the project's, else the setting's. */
function argumentsFor(definition, project, configuration)
{
    if (definition.arguments && definition.arguments.length)
    {
        return definition.arguments;
    }
    if (project && project.arguments && project.arguments.length)
    {
        return project.arguments;
    }
    return configuration.get("programArguments") || [];
}

// ---------------------------------------------------------------------- the shell of a task
//
// A task's command line runs in whatever shell VS Code gives tasks - which is NOT simply
// "PowerShell on Windows": a Windows user whose default profile is Git Bash gets Git Bash,
// where PowerShell's `if ($?) { ... }` is a syntax error. So the shell is read, not assumed:
// the automation profile if one is set, else the default shell (vscode.env.shell), and the
// command line is written in that shell's own language.

const POSIX_SHELLS = new Set(["bash", "sh", "zsh", "fish", "dash", "ksh", "ash", "wsl"]);

/** "posix", "powershell" or "cmd", from a shell executable's path. */
function shellKind(shellPath, platform)
{
    // Both separators by hand: a Windows path must be recognised when read on any platform.
    const parts = String(shellPath || "").split(/[\\/]/);
    const name = parts[parts.length - 1].toLowerCase().replace(/\.exe$/, "");
    if (name.includes("pwsh") || name.includes("powershell"))
    {
        return "powershell";
    }
    if (name === "cmd")
    {
        return "cmd";
    }
    if (POSIX_SHELLS.has(name))
    {
        return "posix";
    }
    // An unknown or unset shell: VS Code's own default is PowerShell on Windows, a POSIX
    // shell everywhere else.
    return (platform || process.platform) === "win32" ? "powershell" : "posix";
}

/** The kind of shell VS Code will start for a task on this machine. */
function taskShellKind()
{
    const platformKey = process.platform === "win32" ? "windows" : (process.platform === "darwin" ? "osx" : "linux");
    const automation = vscode.workspace.getConfiguration("terminal.integrated").get(`automationProfile.${platformKey}`);
    const fromProfile = automation && automation.path;
    return shellKind(fromProfile || (vscode.env && vscode.env.shell) || "", process.platform);
}

/**
 * Several commands as one line for the given shell, the next one running only if the previous
 * one succeeded: `&&` for POSIX shells and cmd, `; if ($?) { ... }` for Windows PowerShell
 * (5.1 has no `&&`; `$?` works in every PowerShell).
 */
function commandLine(commands, kind)
{
    const shell = kind === "win32" ? "powershell" : (kind || "posix"); // "win32" was the old spelling
    const lines = commands.map((command) => command.map((argument) => quoteArgument(argument, shell)).join(" "));
    if (lines.length === 1)
    {
        return lines[0];
    }
    if (shell === "powershell")
    {
        return lines.slice(0, -1).map((line) => `${line}; if ($?) { `).join("")
            + lines[lines.length - 1]
            + " }".repeat(lines.length - 1);
    }
    return lines.join(" && ");
}

function quoteArgument(value, kind)
{
    const text = String(value);
    const shell = kind === true || kind === "win32" ? "powershell" : (kind || "posix");
    // A backslash is a Windows path separator and a POSIX escape: Git Bash on Windows gets
    // C:\Users\... quoted, or it would eat the backslashes.
    if (text !== "" && !/[\s\\"'`$&|;(){}<>^%]/.test(text))
    {
        return text;
    }
    if (shell === "powershell")
    {
        // PowerShell: single quotes take everything literally, and '' is a single quote.
        return `'${text.replace(/'/g, "''")}'`;
    }
    if (shell === "cmd")
    {
        // cmd: double quotes keep spaces together; a quote inside is doubled.
        return `"${text.replace(/"/g, '""')}"`;
    }
    return `'${text.replace(/'/g, "'\\''")}'`;
}

module.exports = { TASK_TYPE, TaskProvider, argumentsFor, commandLine, quoteArgument, shellKind, taskShellKind };
