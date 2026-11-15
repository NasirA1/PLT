// What the debugger says to the user, in the language of the program being debugged.
//
// A Pashto program is debugged in Pashto: the Debug Console and the Thunder Output view carry
// the program's own output, so the debugger's remarks between its lines - the exit code, a
// request for input - are written in the same language, as the Thunder IDE writes them.

"use strict";

const PASHTO_DIGITS = ["۰", "۱", "۲", "۳", "۴", "۵", "۶", "۷", "۸", "۹"];

const MESSAGES = {
    en: {
        waitsForInput: "The program waits for a line of input: type it in the Thunder Output view or the Debug Console.",
        endedUnexpectedly: "The virtual machine ended unexpectedly.",
        stoppedByDebugger: "Program stopped by the debugger.",
        exited: (code) => `Program exited with code ${code}.`,
        inputTitle: "The program asks for input",
        inputPrompt: (name) => `${name} is waiting for a line of input`,
        inputPlaceholder: "Type the line and press Enter (Escape: the end of the input)",
        runtimeError: "runtime error",
    },
    ps: {
        waitsForInput: "پروګرام د ننوت یوې کرښې ته انتظار باسي: د «تندر وتنه» پاڼه کې یا د ډیبګ کنسول کې یې ولیکئ.",
        endedUnexpectedly: "مجازي ماشین ناڅاپه پای ته ورسېد.",
        stoppedByDebugger: "پروګرام د ډیبګر له خوا ودرول شو.",
        exited: (code) => `پروګرام پای ته ورسېد (د وتلو کوډ ${pashtoDigits(code)}).`,
        inputTitle: "پروګرام ننوت غواړي",
        inputPrompt: (name) => `${name} د ننوت یوې کرښې ته انتظار باسي`,
        inputPlaceholder: "کرښه ولیکئ او Enter کېکاږئ (Escape: د ننوت پای)",
        runtimeError: "د چلېدو تېروتنه",
    },
};

function pashtoDigits(value)
{
    return String(value).replace(/[0-9]/g, (digit) => PASHTO_DIGITS[Number(digit)]);
}

/** The messages for a language code; English for one that has none. */
function messagesFor(code)
{
    return MESSAGES[code] || MESSAGES.en;
}

module.exports = { messagesFor, pashtoDigits };
