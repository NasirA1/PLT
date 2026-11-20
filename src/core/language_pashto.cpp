// The Pashto (پښتو) language pack.
//
// Everything a Pashto-speaking student sees from the compiler and the virtual machine is defined
// here: keywords, type names, true/false, built-in function names, punctuation, digits, quotes,
// how values are printed and the text of every error message. Keeping it in one file makes the
// translation easy to review and correct.
//
// NOTE FOR REVIEWERS: these translations were drafted for the first release and should be checked
// by a native Pashto speaker familiar with computing terminology. Alternative spellings (for
// example with Persian/Arabic keyboard variants of a letter) can be added with add_keyword.

#include "thunder/language_pack.hpp"

namespace thunder
{
    void add_pashto_reference(LanguagePack& pack); // language_pashto_reference.cpp

    LanguagePack make_pashto()
    {
        LanguagePack pack;
        pack.code = "ps";
        pack.display_name = "Pashto";
        pack.native_name = "پښتو";
        pack.right_to_left = true;

        // ------------------------------------------------------------ keywords (preferred first)
        pack.add_keyword(TokenKind::KwVar, "متغیر");
        pack.add_keyword(TokenKind::KwConst, "ثابت");
        pack.add_keyword(TokenKind::KwFunc, "دنده");
        pack.add_keyword(TokenKind::KwStruct, "جوړښت");
        pack.add_keyword(TokenKind::KwEnum, "ګڼنه"); // "enumeration"; گڼنه (Persian gaf) is accepted via fold_letters
        pack.add_keyword(TokenKind::KwIf, "که");
        pack.add_keyword(TokenKind::KwElse, "کنه");
        pack.add_keyword(TokenKind::KwWhile, "ترڅو");
        pack.add_keyword(TokenKind::KwFor, "تکرار"); // "repetition"; تكرار (Arabic kaf) is accepted via fold_letters
        pack.add_keyword(TokenKind::KwIn, "په"); // for x in a: "تکرار x په a"
        pack.add_keyword(TokenKind::KwBreak, "ودرېږه");
        pack.add_keyword(TokenKind::KwBreak, "ودریږه"); // written with ی instead of ې
        pack.add_keyword(TokenKind::KwContinue, "دوام");
        pack.add_keyword(TokenKind::KwPass, "تېر");
        pack.add_keyword(TokenKind::KwAnd, "او");
        pack.add_keyword(TokenKind::KwOr, "یا");
        pack.add_keyword(TokenKind::KwNot, "نه");
        pack.add_keyword(TokenKind::KwImport, "راوړه");
        pack.add_keyword(TokenKind::KwNative, "بهرنی"); // "foreign": the C++ native interface
        pack.add_keyword(TokenKind::KwModule, "ټولګی"); // "module"; ټولگى, ټولګى (keyboard variants) are folded to it
        // "with": Pashto puts it after the thing it goes with - "لاستی سره = ..." (with the
        // handle), "ساکټ سره" (with the socket) - never "سره ساکټ".
        pack.add_keyword(TokenKind::KwWith, "سره");
        pack.with_after_resource = true;
        pack.add_keyword(TokenKind::KwInterface, "برسېر"); // "interface"; برسير (Arabic yeh) is folded to it
        // "mixin" ("joining in"): written with the Pashto gaf ګ; گډون with the Persian/Dari gaf
        // reads as the same keyword (fold_letters unifies the two gafs).
        pack.add_keyword(TokenKind::KwMixin, "ګډون");
        pack.add_keyword(TokenKind::KwReturn, "راګرځه"); // راگرځه (Persian gaf) is accepted via fold_letters
        pack.add_keyword(TokenKind::KwTrue, "رښتیا"); // رښتيا (Arabic ي) is accepted via fold_letters
        pack.add_keyword(TokenKind::KwFalse, "درواغ");
        pack.add_keyword(TokenKind::KwFalse, "دروغ"); // an earlier spelling, still accepted
        pack.add_keyword(TokenKind::KwNothing, "هېڅ"); // the empty struct reference
        pack.add_keyword(TokenKind::KwInt, "عدد");
        pack.add_keyword(TokenKind::KwFloat, "اعشاري");
        pack.add_keyword(TokenKind::KwBool, "منطقي");
        pack.add_keyword(TokenKind::KwString, "متن");
        pack.add_keyword(TokenKind::KwByte, "بایټ"); // بايټ (Arabic yeh) is accepted via fold_letters

        // ------------------------------------------------------------ annotations (@test, @mock)
        // "@ازموینه" (test) is preferred - the spelling of the testing module; "@آزموینه" with the
        // alef madda is accepted too (an earlier preferred spelling). ي/ی and the two gafs are
        // folded as everywhere.
        pack.add_annotation("test", "ازموینه");
        pack.add_annotation("test", "آزموینه");
        // "mock": پېښه ګر, two words like the multi-word built-ins (the lexer joins them); the
        // Dari/Persian gaf (پېښه گر) folds to the same word, and پېښه_ګر is accepted too.
        pack.add_annotation("mock", "پېښه ګر");
        pack.add_annotation("mock", "پېښه_ګر");

        // ------------------------------------------------------------ built-in functions
        // Multi-word names are written with a space between the words, as in ordinary Pashto;
        // the lexer joins them into one token.
        pack.add_builtin(BuiltinId::Print, "چاپ");
        pack.add_builtin(BuiltinId::Println, "کرښه چاپ");
        pack.add_builtin(BuiltinId::Len, "اوږدوالی");
        pack.add_builtin(BuiltinId::Str, "متن کړه");
        pack.add_builtin(BuiltinId::ToInt, "عدد کړه");
        pack.add_builtin(BuiltinId::ToFloat, "اعشاري کړه");
        pack.add_builtin(BuiltinId::Format, "بڼه کړه");
        pack.add_builtin(BuiltinId::Hex, "شپاړسیز کړه");
        pack.add_builtin(BuiltinId::Bin, "دوه ییز کړه");
        // bytes
        pack.add_builtin(BuiltinId::ToByte, "بایټ کړه");
        pack.add_builtin(BuiltinId::StringToBytes, "له متنه");
        pack.add_builtin(BuiltinId::BytesToString, "متن کړه");
        pack.add_builtin(BuiltinId::BytesToInt, "عدد کړه");
        pack.add_builtin(BuiltinId::IntToBytes, "له عدده");
        pack.add_builtin(BuiltinId::ReadBytes, "بایټونه ولوله");
        pack.add_builtin(BuiltinId::WriteBytes, "بایټونه ولیکه");
        pack.add_builtin(BuiltinId::AppendBytes, "بایټونه ورزیات کړه");
        pack.add_builtin(BuiltinId::Array, "لړۍ");
        pack.add_builtin(BuiltinId::Inspect, "کتنه");
        pack.add_builtin(BuiltinId::Dump, "وښایه");
        pack.add_builtin(BuiltinId::Copy, "کاپي");
        pack.add_builtin(BuiltinId::Has, "لري");
        pack.add_builtin(BuiltinId::Remove, "لیرې کړه");
        pack.add_builtin(BuiltinId::Remove, "لرې کړه"); // the earlier spelling, still accepted
        pack.add_builtin(BuiltinId::Keys, "کیلۍ");
        pack.add_builtin(BuiltinId::Values, "ارزښتونه");
        // input
        pack.add_builtin(BuiltinId::Input, "ولوله");
        pack.add_builtin(BuiltinId::EndOfInput, "د ننوت پای");
        // text
        pack.add_builtin(BuiltinId::Substring, "برخه");
        pack.add_builtin(BuiltinId::Trim, "پاک کړه");
        pack.add_builtin(BuiltinId::ToUpper, "لوی توري کړه");
        pack.add_builtin(BuiltinId::ToLower, "کوچنی توري کړه");
        pack.add_builtin(BuiltinId::ToLower, "واړه توري کړه"); // the earlier name, still accepted
        pack.add_builtin(BuiltinId::Split, "وېشه");
        pack.add_builtin(BuiltinId::Join, "یو ځای کړه");
        pack.add_builtin(BuiltinId::Replace, "بدل کړه");
        pack.add_builtin(BuiltinId::StartsWith, "پیلېږي په");
        pack.add_builtin(BuiltinId::EndsWith, "پایېږي په");
        pack.add_builtin(BuiltinId::Contains, "شامل دی");
        pack.add_builtin(BuiltinId::IndexOf, "ځای یې");
        pack.add_builtin(BuiltinId::Repeat, "تکرار کړه");
        pack.add_builtin(BuiltinId::Reverse, "سرچپه کړه");
        pack.add_builtin(BuiltinId::CharCode, "د توري کوډ");
        pack.add_builtin(BuiltinId::FromCharCode, "د کوډ توری");
        pack.add_builtin(BuiltinId::IsInt, "عدد دی");
        // tuples (pairs)
        pack.add_builtin(BuiltinId::First, "لومړی");
        pack.add_builtin(BuiltinId::Second, "دوهم");
        pack.add_builtin(BuiltinId::Swap, "ځای بدلول");
        pack.add_builtin(BuiltinId::Zip, "جوړې کړه");
        pack.add_builtin(BuiltinId::Enumerate, "شمېره ورکړه");
        pack.add_builtin(BuiltinId::Items, "توکي");
        // lists and maps
        pack.add_builtin(BuiltinId::Push, "ورزیات کړه");
        pack.add_builtin(BuiltinId::Pop, "وروستی واخله");
        pack.add_builtin(BuiltinId::Insert, "ننباسه");
        pack.add_builtin(BuiltinId::RemoveAt, "له ځایه لیرې کړه");
        pack.add_builtin(BuiltinId::RemoveAt, "له ځایه لرې کړه"); // the earlier spelling, still accepted
        pack.add_builtin(BuiltinId::Slice, "ټوټه");
        pack.add_builtin(BuiltinId::Concat, "سره نښلوه");
        pack.add_builtin(BuiltinId::Sort, "ترتیب کړه");
        pack.add_builtin(BuiltinId::Range, "واټن");
        pack.add_builtin(BuiltinId::Sum, "مجموعه");
        pack.add_builtin(BuiltinId::Map, "پر هر یو");
        pack.add_builtin(BuiltinId::Filter, "چاڼ کړه");
        pack.add_builtin(BuiltinId::Reduce, "راټول کړه");
        pack.add_builtin(BuiltinId::Get, "ترلاسه کړه");
        // module os
        pack.add_builtin(BuiltinId::Args, "دلیلونه");
        pack.add_builtin(BuiltinId::GetEnv, "د چاپېریال ارزښت");
        pack.add_builtin(BuiltinId::Run, "وچلوه");
        pack.add_builtin(BuiltinId::RunOutput, "د چلولو پایله");
        pack.add_builtin(BuiltinId::ReadFile, "ولوله");
        pack.add_builtin(BuiltinId::WriteFile, "ولیکه");
        pack.add_builtin(BuiltinId::AppendFile, "ورزیات کړه");
        pack.add_builtin(BuiltinId::FileExists, "شته");
        pack.add_builtin(BuiltinId::RemoveFile, "ړنګ کړه");
        pack.add_builtin(BuiltinId::ListDir, "د پوښۍ توکي");
        pack.add_builtin(BuiltinId::CurrentDir, "اوسنۍ پوښۍ");
        pack.add_builtin(BuiltinId::Time, "وخت");
        pack.add_builtin(BuiltinId::ClockMs, "ساعت");
        pack.add_builtin(BuiltinId::Sleep, "انتظار");
        pack.add_builtin(BuiltinId::DateTime, "نېټه او وخت");
        pack.add_builtin(BuiltinId::Platform, "عامل سیسټم");
        pack.add_builtin(BuiltinId::Exit, "پای ته ورسوه");

        // numbers and maths
        pack.add_builtin(BuiltinId::IsNumber, "شمېره ده");
        pack.add_builtin(BuiltinId::Abs, "مطلق");
        pack.add_builtin(BuiltinId::Min, "کمینه");
        pack.add_builtin(BuiltinId::Max, "اعظمه");
        pack.add_builtin(BuiltinId::Round, "نیژدې کړه");
        pack.add_builtin(BuiltinId::Round, "نږدې کړه"); // the earlier spelling, still accepted
        pack.add_builtin(BuiltinId::Floor, "کښته کړه");
        pack.add_builtin(BuiltinId::Floor, "ښکته کړه"); // the earlier spelling, still accepted
        pack.add_builtin(BuiltinId::Ceil, "پورته کړه");
        pack.add_builtin(BuiltinId::Sqrt, "جذر");
        pack.add_builtin(BuiltinId::Pow, "توان");
        pack.add_builtin(BuiltinId::Pow, "توان کړه"); // the earlier name, still accepted
        pack.add_builtin(BuiltinId::Exp, "نمايي");
        pack.add_builtin(BuiltinId::Log, "لوګارتم");
        pack.add_builtin(BuiltinId::Sin, "جیب");
        pack.add_builtin(BuiltinId::Cos, "تتمیم جیب");
        pack.add_builtin(BuiltinId::Tan, "ظل");
        pack.add_builtin(BuiltinId::Pi, "پای");
        pack.add_builtin(BuiltinId::Random, "اعشاري");
        // random numbers
        pack.add_builtin(BuiltinId::RandomInt, "عدد");
        pack.add_builtin(BuiltinId::SeedRandom, "بنسټ");
        // module graphics
        pack.add_builtin(BuiltinId::Canvas, "ټاټ");
        pack.add_builtin(BuiltinId::Clear, "ټاټ پاک کړه");
        pack.add_builtin(BuiltinId::Pixel, "ټکی");
        pack.add_builtin(BuiltinId::Line, "لیکه");
        pack.add_builtin(BuiltinId::Rect, "مستطیل");
        pack.add_builtin(BuiltinId::RectOutline, "د مستطیل څنډه");
        pack.add_builtin(BuiltinId::Circle, "دایره");
        pack.add_builtin(BuiltinId::CircleOutline, "د دایرې څنډه");
        pack.add_builtin(BuiltinId::Text, "لیکنه");
        pack.add_builtin(BuiltinId::TextSize, "د لیکنې اندازه");
        pack.add_builtin(BuiltinId::LineWidth, "د لیکې پلنوالی");
        pack.add_builtin(BuiltinId::Rgb, "رنګ");
        pack.add_builtin(BuiltinId::Frame, "چوکاټ");
        pack.add_builtin(BuiltinId::Show, "ښکاره کړه");
        pack.add_builtin(BuiltinId::FrameRate, "د چوکاټ چټکتیا");
        pack.add_builtin(BuiltinId::KeyPressed, "کښېکښل شوې تڼۍ");
        pack.add_builtin(BuiltinId::KeyDown, "تڼۍ کښېکښل شوې ده");
        pack.add_builtin(BuiltinId::MouseX, "موږک افقي");
        pack.add_builtin(BuiltinId::MouseX, "د موږک افقي ځای"); // the earlier name, still accepted
        pack.add_builtin(BuiltinId::MouseY, "موږک عمودي");
        pack.add_builtin(BuiltinId::MouseY, "د موږک عمودي ځای"); // the earlier name, still accepted
        pack.add_builtin(BuiltinId::MouseDown, "موږک کښېکښل شوی");
        // reflection
        pack.add_builtin(BuiltinId::NameOf, "د دندې نوم");
        // module task: the concurrency kernel (کارونه)
        pack.add_builtin(BuiltinId::TaskSpawn, "پیل کړه");
        pack.add_builtin(BuiltinId::TaskJoin, "راته ودرېږه");
        pack.add_builtin(BuiltinId::TaskDone, "پای ته رسېدلی");
        pack.add_builtin(BuiltinId::TaskSleep, "بیده شه");
        pack.add_builtin(BuiltinId::TaskYield, "لار ورکړه");
        pack.add_builtin(BuiltinId::TaskCurrent, "اوسنی");
        pack.add_builtin(BuiltinId::TaskName, "نوم");
        pack.add_builtin(BuiltinId::TaskCount, "شمېر");
        pack.add_builtin(BuiltinId::TaskMutex, "نوی قلف");
        pack.add_builtin(BuiltinId::TaskLock, "قلف کړه");
        pack.add_builtin(BuiltinId::TaskTryLock, "هڅه وکړه");
        pack.add_builtin(BuiltinId::TaskUnlock, "خلاص کړه");
        pack.add_builtin(BuiltinId::TaskCondition, "نوی حالت");
        pack.add_builtin(BuiltinId::TaskWait, "انتظار وباسه");
        pack.add_builtin(BuiltinId::TaskSignal, "خبر");
        pack.add_builtin(BuiltinId::TaskBroadcast, "ټولو ته خبر");
        // stopping the program on purpose
        pack.add_builtin(BuiltinId::Fail, "ناکام شه");
        // the text module's own forms of the general sequence functions and conversions
        pack.add_builtin(BuiltinId::StringLen, "اوږدوالی");
        pack.add_builtin(BuiltinId::StringContains, "شامل دی");
        pack.add_builtin(BuiltinId::StringIndexOf, "ځای یې");
        pack.add_builtin(BuiltinId::StringReverse, "سرچپه کړه");
        pack.add_builtin(BuiltinId::StringSlice, "ټوټه");
        pack.add_builtin(BuiltinId::StringToInt, "عدد کړه");
        pack.add_builtin(BuiltinId::StringToFloat, "اعشاري کړه");

        // ------------------------------------------------------------ key names (key_pressed, key_down)
        // Letters and digits keep their names ("a", "7"); the other keys are named in Pashto.
        pack.main_name = "اصلي"; // the function that runs after the top-level statements (main)
        pack.key_names = {{"left", "کیڼ"}, {"right", "ښي"}, {"up", "پورته"}, {"down", "کښته"}, {"down", "ښکته"}, {"space", "تشه"},
            {"enter", "ننوتل"}, {"escape", "وتل"}, {"backspace", "شاته"}, {"tab", "ټب"}};

        // ------------------------------------------------------------ built-in modules
        // The English names (os, math, ...) are accepted too, in every language.
        pack.add_module(BuiltinModule::Os, "سیسټم");
        pack.add_module(BuiltinModule::Graphics, "ګرافیک");
        pack.add_module(BuiltinModule::Math, "ریاضي");
        pack.add_module(BuiltinModule::String, "متن"); // spelled like the type, as "string" is in English
        pack.add_module(BuiltinModule::Random, "تصادفي");
        pack.add_module(BuiltinModule::Bytes, "بایټونه");
        pack.add_module(BuiltinModule::File, "فایل");
        // "the works": the concurrency kernel. The object that stands for one task is the
        // struct کار of the SDK's concurrency module, so the two names never collide.
        pack.add_module(BuiltinModule::Task, "کارونه");

        // ------------------------------------------------------------ built-in enums
        // The members are the Pashto colour names graphics has always accepted as strings.
        pack.add_enum(BuiltinEnumId::Color, "رنګونه");
        for (const auto& [member, name] : std::vector<std::pair<std::uint16_t, const char*>>{{0, "سور"}, {1, "زرغون"}, {2, "شین"},
                 {3, "سپین"}, {4, "تور"}, {5, "ژېړ"}, {6, "نارنجي"}, {7, "ګلابي"}, {8, "خړ"}, {9, "نسواري"}, {10, "بنفش"},
                 {11, "اسماني"}, {12, "طلايي"}})
        {
            pack.add_enum_member(BuiltinEnumId::Color, member, name);
        }
        pack.add_enum(BuiltinEnumId::Platform, "پلیټفارم");
        for (const auto& [member, name] : std::vector<std::pair<std::uint16_t, const char*>>{{0, "لینکس"}, {1, "وینډوز"},
                 {2, "مېک"}, {3, "ناپېژندلی"}})
        {
            pack.add_enum_member(BuiltinEnumId::Platform, member, name);
        }

        // ------------------------------------------------------------ punctuation, digits, quotes
        // Pashto programs separate with the Arabic comma '،' (the translator writes it; ',' is
        // accepted too) and use the ASCII quotes of the English form («» are accepted as well).
        pack.add_punctuation(TokenKind::Comma, U'،', true);      // ، Arabic comma
        pack.add_punctuation(TokenKind::Percent, U'٪', false);   // ٪ Arabic percent sign
        pack.add_punctuation(TokenKind::Star, U'×', false);      // × multiplication sign
        pack.add_punctuation(TokenKind::Slash, U'÷', false);     // ÷ division sign
        pack.decimal_point = U'\u066B'; // ٫ Arabic decimal separator ("." is accepted too)
        pack.digits = {U'۰', U'۱', U'۲', U'۳', U'۴', U'۵', U'۶', U'۷', U'۸',
            U'۹'}; // ۰ ۱ ۲ ۳ ۴ ۵ ۶ ۷ ۸ ۹
        // "متن", «متن» and the curly double quotes some keyboard layouts produce: “متن”, ”متن”.
        pack.quotes = {{U'"', U'"'}, {U'«', U'»'}, {U'“', U'”'}, {U'”', U'”'}, {U'„', U'“'}};

        pack.display.digits = {"۰", "۱", "۲", "۳", "۴", "۵", "۶", "۷", "۸", "۹"};
        pack.display.true_text = "رښتیا";
        pack.display.false_text = "درواغ";
        pack.display.separator = "، ";
        pack.display.open_quote = "«";
        pack.display.close_quote = "»";
        pack.display.function_word = "دنده";
        pack.display.nothing_text = "هېڅ";
        pack.display.decimal_point = "\u066B"; // ٫

        // ------------------------------------------------------------ words used in messages
        pack.phase_names = {"لغوي", "نحوي", "معنايي", "د اجرا"};
        pack.error_word = "تېروتنه";
        pack.warning_word = "خبرداری";
        pack.terms = {
            {"end of file", "د فایل پای"},
            {"after the handle", "له نوم وروسته"},
            {"self", "ځان"}, // the hidden receiver of a method, shown as "(ځان)" in the debugger
            {"close", "وتړه"},       // the function a module closes its resources with (with ... سره)
            {"resource", "سرچینه"},  // the hidden variable of "with resource" 
            {"at the start of the line", "د کرښې په پیل کې"},
            {"variable", "متغیر"},
            {"function", "دنده"},
            {"type", "ډول"},
            {"module", "ټولګی"},
            {"sequence", "لړ"},   // the hidden locals of a "for ... in" loop
            {"enum_values", "ارزښتونه"}, // Color.values and Color.count, in Pashto (the English words work too)
            {"enum_count", "شمېر"},
            {"count", "شمېر"}, // the byte count of bytes_to_int / int_to_bytes, in R022
            {"position", "ځای"},
            {"the end of the line", "د کرښې پای"},
            {"identifier", "نوم"},
            {"number", "عدد"},
            {"string", "متن"},
            {"the target", "موخه"},
            {"the array element", "د لړۍ توکی"},
            {"the map entry", "د نقشې توکی"},
            {"keys", "کیلۍ"},
            {"values", "ارزښتونه"},
            {"in {} (line {})", "په {} کې (کرښه {})"},
            {"a new line or ','", "نوې کرښه یا '،'"},
            {"a function type", "د دندې ډول"},
            {"a string", "یو متن"},
            // tests (@ازموینه): the words of messages, and the names in the SDK's testing.th
            // that the compiler writes calls to
            {"suite", "ډله"},
            {"result", "پایله"}, // the hidden local a mock's method returns // the hidden variable holding a test suite's instance
            {"the function", "دنده"},
            {"this statement", "دا امر"},
            {"a function", "یوه دنده"},
            {"a method", "د جوړښت دنده"},
            {"struct", "جوړښت"},
            {"no arguments", "هېڅ دلیل نه"},
            {"a name that is not empty", "داسې نوم چې تش نه وي"},
            {"testing.test", "ازموینه"},
            {"testing.run", "وچلوه"},
            {"testing.finish", "پای_ته_ورسوه"},
            {"testing.Mock", "پېښه_ګر"},
            {"testing._record", "_ثبت_کړه"},
            {"annotation.test", "یوه ازموینه نښه کوي: یوه دنده چې پارامټر نه لري، یا ټوله یوه ډله - یو جوړښت (هره عامه دنده یې چې پارامټر نه لري یوه ازموینه ده، پر یوې نوې بېلګې چلېږي) یا یو ټولګی. د خوښې تشریح یې نوموي: @ازموینه(«جمع سمه ده»). یوازې د ازموینې فایلونه ازموینې لرلای شي."},
            {"annotation.mock", "یو جوړښت د هغو برسېرونو ځای نیوونکی کوي چې نوموي یې: هره دنده یې خپلې بلنې ثبتوي او د جوړښت د خپلې همنومې دندې، یا د اصلي ارزښت ځواب ورکوي. د پېښه ګر دندې (وبلل_شو، د_بلنو_شمېر، بلنې، تمه_د_بللو ...) پوښتي چې څه وشول."},
            {"an indented line", "یوه ننوتې کرښه"},
            {"the end of the block", "د بلاک پای"},
            {"a module name or a file name in quotes", "د ماډیول نوم یا د فایل نوم په قوسونو \"\" کې"},
            {"the name of a native library in quotes", "د بهرني کتابتون نوم په قوسونو \"\" کې"},
            {"width", "پلنوالی"},
            {"height", "لوړوالی"},
            {"frame rate", "د چوکاټونو شمېر"},
            {"text size", "د متن اندازه"},
            {"line width", "د لیکې پلنوالی"},
            {"radius", "وړانګه"},
            {"shift count", "د ښويېدو شمېر"},
            // concurrency (module کارونه): the names of the handles, the name of the first task
            // and the sentences the deadlock report is built from
            {"task", "کار"},
            {"lock", "قلف"},
            {"condition", "حالت"},
            {"main", "اصلي"},
            {"{0} waits for the lock {1} holds", "{0} د هغه قلف انتظار باسي چې {1} یې لري"},
            {"{0} waits for {1} to end", "{0} د {1} د پای انتظار باسي"},
            {"{0} waits for a condition", "{0} د یوه حالت انتظار باسي"},
            {"in the task {}", "په کار {} کې"},
        };

        // ------------------------------------------------------------ messages
        using enum DiagId;
        pack.messages = {
            // Lexical analysis
            {UnexpectedCharacter, "ناڅاپي توری '{}'"},
            {UnexpectedCharacterHint, "ناڅاپي توری '{0}'؛ ایا مو '{1}' مطلب و؟"},
            {UnterminatedString, "متن نه دی بند شوی (متن باید په همغه کرښه کې په »، ” یا \" بند شي)"},
            {InvalidEscape, "ناسم ځانګړی ترکیب '\\{}' (سم ترکیبونه: \\n \\t \\r \\0 \\\" \\\\ \\« \\»)"},
            {IntegerTooLarge, "عدد {} ډېر لوی دی (تر ټولو لوی عدد ۹۲۲۳۳۷۲۰۳۶۸۵۴۷۷۵۸۰۷ دی)"},
            {FloatTooLarge, "اعشاري شمېره {} ډېره لویه ده"},
            {MalformedNumber, "ناسم عدد '{}'"},
            {UnterminatedComment, "تبصره نه ده بنده شوې (بندوونکې ''' نشته)"},
            {InconsistentIndentation, "دا کرښه له مخکینۍ کرښې لږه ننوتې ده، خو د کوم چاپېر بلاک کچې ته نه ده راګرځېدلې"},
            // Syntax analysis
            {ExpectedToken, "تمه د {0} وه، خو {1} پیدا شو"},
            {ExpectedExpression, "تمه د یوې افادې وه، خو {} پیدا شو"},
            {ExpectedType, "تمه د یو ډول (عدد، منطقي، متن یا جوړښت) وه، خو {} پیدا شو"},
            {ExpectedStatement, "تمه د یو امر وه، خو {} پیدا شو"},
            {InvalidAssignmentTarget, "ناسمه موخه: یوازې متغیرونو، د لړۍ توکو او د جوړښت برخو ته ارزښت ورکول کېدای شي"},
            {NotCallable, "دا ارزښت نشي بلل کېدای؛ یوازې دندې او د دندو ارزښتونه بلل کېدای شي"},
            {NestedFunction, "دندې یوازې د پروګرام په سر کچه کې اعلانېدای شي"},
            {VarNeedsTypeOrValue, "متغیر '{}' ډول یا لومړني ارزښت ته اړتیا لري"},
            {TooManyErrors, "ډېرې تېروتنې؛ کار ودرېد"},
            {ExpectedStatementEnd, "تمه د امر د پای وه (نوې کرښه)، خو {} پیدا شو"},
            {StructNotTopLevel, "جوړښتونه یوازې د پروګرام په سر کچه کې اعلانېدای شي"},
            {ExpectedIndentedBlock, "له {0} وروسته یو ننوتی بلاک په کار و، خو {1} پیدا شو"},
            {UnexpectedIndent, "ناڅاپي ننوتنه (دا کرښه له مخکینۍ کرښې ډېره ننوتې ده)"},
            {BracesNotUsed, "له {0} وروسته یو ننوتی بلاک په کار و؛ تندر بلاکونه په ننوتنې ښيي، نه په '{{' او '}}'"},
            {TupleNeedsTwoElements, "ټاپل لږ تر لږه دوه توکي غواړي"},
            {ImportNotTopLevel, "'راوړه' یوازې د پروګرام په سر کچه کې کارېدای شي"},
            // Semantic analysis
            {UndeclaredName, "'{}' نه دی اعلان شوی"},
            {AlreadyDeclared, "'{}' دمخه په دې ساحه کې اعلان شوی"},
            {FunctionRedefined, "دنده '{}' دمخه تعریف شوې"},
            {BuiltinRedefined, "'{}' دننۍ دنده ده او بیا نشي تعریفېدای"},
            {AssignTypeMismatch, "د '{0}' ډول ارزښت د '{2}' ډول '{1}' ته نشي ورکول کېدای"},
            {BinaryOperandMismatch, "عملګر '{0}' په '{1}' او '{2}' نشي کارول کېدای"},
            {UnaryOperandMismatch, "عملګر '{0}' په '{1}' نشي کارول کېدای"},
            {ConditionNotBool, "شرط باید منطقي وي، خو '{}' دی"},
            {ArgumentCount, "'{0}' {1} دلیلونو ته اړتیا لري، خو {2} ورکړل شول"},
            {ArgumentType, "د '{1}' دلیل {0} باید '{2}' وي، خو '{3}' دی"},
            {NotAFunction, "'{}' دنده نه ده"},
            {AssignToConstant, "ثابت '{}' ته نوی ارزښت نشي ورکول کېدای"},
            {OutsideLoop, "'{}' یوازې د تکرار دننه کارېدای شي"},
            {ReturnOutsideFunction, "'راګرځه' یوازې د دندې دننه کارېدای شي"},
            {MissingReturnValue, "دنده '{0}' باید د '{1}' ډول ارزښت راوګرځوي"},
            {VoidReturnsValue, "دنده '{}' د راګرځېدو ډول نه لري، نو ارزښت نشي راګرځولی"},
            {NotAllPathsReturn, "د دندې '{}' ټولې لارې ارزښت نه راګرځوي"},
            {NotIndexable, "د '{}' ډول ارزښت په لړ شمېرې نشي لوستل کېدای"},
            {IndexNotInt, "لړ شمېره باید عدد وي، خو '{}' ده"},
            {ArrayElementMismatch, "د لړۍ ټول توکي باید یو ډول ولري: تمه د '{0}' وه، خو '{1}' پیدا شو"},
            {EmptyArrayNeedsType, "د تشې لړۍ د توکو ډول نشي معلومېدای؛ ډول ولیکئ، لکه: متغیر a: عدد[] = []"},
            {VoidValue, "دنده '{}' ارزښت نه راګرځوي، نو پایله یې نشي کارېدای"},
            {BadMainSignature, "'اصلي' (main) باید پارامټرونه او د راګرځېدو ډول ونه لري"},
            {StringImmutable, "متنونه نه بدلېږي؛ د متن یو توری نشي بدلېدای"},
            {FunctionUsedAsValue, "'{0}' دننۍ دنده ده او د ارزښت په توګه نشي کارېدای؛ په قوسونو یې وبولئ، لکه {1}(...)"},
            {BuiltinArgumentType, "دننۍ دنده '{0}' د '{1}' ډول په ارزښت نشي کارېدای"},
            {ReturnTypeMismatch, "دنده '{0}' باید '{1}' راوګرځوي، خو دا ارزښت '{2}' دی"},
            {TypeRedefined, "نوم '{}' دمخه د بل ډول یا دندې لپاره کارېږي"},
            {DuplicateField, "جوړښت '{0}' دمخه د '{1}' په نوم برخه لري"},
            {RecursiveStruct, "جوړښت '{0}' د '{1}' برخې له لارې ځان لري؛ تکراري جوړښتونه نه منل کېږي"},
            {UnknownType, "نامعلوم ډول '{}'"},
            {NoSuchField, "د '{0}' ډول ارزښت د '{1}' په نوم برخه نه لري"},
            {NamedArgumentsNotAllowed, "نوم لرونکي دلیلونه (نوم: ارزښت) یوازې د جوړښت د جوړولو لپاره کارېدای شي"},
            {MixedArguments, "د '{}' جوړولو لپاره یا ټول دلیلونه په نوم ورکړئ یا ټول په ترتیب"},
            {FieldGivenTwice, "برخه '{}' له یو ځل ډېر ورکړل شوې"},
            {StructUsedAsValue, "'{0}' د جوړښت ډول دی؛ ارزښت یې په {1}(...) جوړ کړئ"},
            {NotAStruct, "برخې یوازې د جوړښت په ارزښتونو کارېږي، نه د '{}' ډول ارزښت باندې"},
            {UnusedVariable, "متغیر '{}' اعلان شوی خو هېڅکله نه دی کارول شوی"},
            {UnreachableCode, "دا کوډ هېڅکله نه اجرا کېږي"},
            {EmptyMapNeedsType, "تشه نقشه د ډول ښودنې ته اړتیا لري، لکه متغیر m: {{متن: عدد}} = {{}}"},
            {MapKeyType, "د نقشې کیلۍ باید عدد، منطقي یا متن وي، نه '{}'"},
            {MapElementMismatch, "د نقشې ټول {0} باید یو ډول ولري: تمه د '{1}' وه، خو '{2}' پیدا شو"},
            {NotATuple, "'.{0}' د ټاپل توکی ټاکي، خو ارزښت د '{1}' ډول دی"},
            {TupleIndexOutOfRange, "د '{0}' ډول ټاپل {1} توکی نه لري"},
            {MapKeyMismatch, "د '{0}' ډول نقشې کیلۍ باید '{1}' وي، خو '{2}' ده"},
            {DestructureMismatch, "د '{0}' ډول ارزښت په {1} نومونو نشي وېشل کېدای"},
            {ModuleNotFound, "ماډیول '{0}' پیدا نشو (د {1} لټون وشو)"},
            {ModuleLanguageMismatch, "ماډیول '{0}' په {1} لیکل شوی، خو دا پروګرام په {2} دی؛ د thunderc --translate په مرسته یې وژباړئ"},
            {ModuleStatement, "ماډیول یوازې دندې، جوړښتونه، متغیرونه او ثابتونه اعلانولای شي؛ دا امر به د هر راوړو پر مهال اجرا شي"},
            {ModuleDefinesMain, "ماډیول 'اصلي' (main) نشي تعریفولای؛ یوازې هغه پروګرام چې چلېږي یې تعریفولای شي"},
            {ModuleRedefined, "ټولګی '{}' مخکې اعلان شوی (هر ټولګی یو ځل، په یوه ځای کې اعلانېږي)"},
            {NameIsModule, "'{}' د یوه ټولګي نوم دی؛ دې {} ته بل نوم ورکړئ"},
            {PrivateMember, "'{0}' د '{1}' ټولګي شخصي برخه ده (هغه نومونه چې په '_' پیلېږي یوازې د خپل ټولګي دننه کارېدلای شي)"},
            {MemberNeedsModule, "'{0}' د '{1}' ټولګي برخه ده: {1}.{0} ولیکئ"},
            {NotAResource, "د '{}' ډول ارزښت په '{}' نه تړل کېږي: سرچینه یا داسې جوړښت دی چې د وتړه() دنده لري، یا د یوه ټولګي د دندې پایله، لکه لاستی.پرانیزه()"},
            {NoCloseFunction, "'{}' نه تړل کېږي: داسې دنده '{}' ولیکئ چې دا واخلي"},
            {NoSuchModuleMember, "ټولګی '{}' د '{}' په نوم برخه نه لري"},
            {ModuleNotTopLevel, "'ټولګی' یوازې د فایل په سر کچه اعلانېدلای شي (ټولګي یو په بل کې نه ځي)"},
            {WithPosition, "په دې ژبه کې '{0}' {1} لیکل کېږي"},
            {InterfaceNotTopLevel, "'برسېر' یوازې د فایل په سر کچه اعلانېدلای شي"},
            {ExpectedLambdaBody, "د دندې ارزښت تنه په کار وه: په همغه کرښه یو امر، یا یو ننوتی بلاک (له قوسونو بهر)"},
            {UnknownAnnotation, "نامعلومه نښه '@{}' (نښې دا دي: {})"},
            {OrphanAnnotation, "نښه د یوه اعلان ('دنده'، 'جوړښت' یا 'ټولګی') له کرښې مخکې راځي، خو له هغې وروسته {} راغلی"},
            {NotAnInterfaceOrMixin, "'{}' دلته له ':' وروسته نشي راتلای: د جوړښت له نوم وروسته هغه برسېرونه راځي چې عملي کوي یې او هغه ګډونونه چې رانغاړي یې"},
            {MixinHasBases, "ګډون '{}' خپل ګډون نشي لرلای (ګډونونه یوازې یوه کچه ژور ځي)"},
            {MixinGeneric, "ګډون عمومي (ډول‌داره) کېدای نشي (د ډول پارامټرونه نشي لرلای)"},
            {InterfaceNotImplemented, "جوړښت '{0}' وايي چې برسېر '{1}' عملي کوي خو '{2}' په نوم غړی نه لري"},
            {InterfaceSignatureMismatch, "د جوړښت '{1}' غړی '{0}' له برسېر '{2}' سره سمون نه خوري: باید '{3}' وي"},
            {PrivateStructMember, "'{0}' د '{1}' شخصي غړی دی (هغه غړي چې نوم یې په '_' پیلېږي یوازې د جوړښت د خپلو دندو دننه کارېدلای شي)"},
            {NoSuchMethod, "د '{0}' ډول د '{1}' په نوم دنده یا برخه نه لري"},
            {InterfaceMemberNotCalled, "'{0}' د '{1}' غړی دی او په بللو کارېږي: {0}(...)"},
            {BadConstructor, "د '{}' جوړوونکې دنده د راګرځېدو ډول یا د ډول پارامټرونه نشي لرلای"},
            {DuplicateMember, "جوړښت '{0}' دمخه د '{1}' په نوم غړی لري"},
            {NoCloseMethod, "'{0}' نه تړل کېږي: جوړښت ته د '{1}()' دنده ورکړئ"},
            {StructIsNotMixin, "'{0}' جوړښت دی، ګډون نه دی: له ':' وروسته یوازې ګډون (چې د 'ګډون {0}' په بڼه اعلان شوی وي) رانغاړل کېدای شي"},
            {MixinIsNotType, "'{}' ګډون دی، ډول نه دی: یوازې یو جوړښت یې له ':' وروسته رانغاړلای شي"},
            {MixinConstructor, "ګډون '{}' جوړوونکی نشي لرلای: هغه جوړښت چې دا رانغاړي ارزښت جوړوي"},
            {AnnotationTarget, "'@{0}' پر {1} نه کارېږي"},
            {AnnotationArgument, "'@{0}' {1} اخلي"},
            {TestShape, "ازموینه '{}' باید هېڅ پارامټر ونه لري او هېڅ ونه راګرځوي"},
            {TestGeneric, "عمومي {} ازموینه کېدای نشي (تر هغه چې ډولونه یې معلوم نه وي، کوډ نه لري)"},
            {DuplicateTest, "ډله '{0}' دمخه د '{1}' په نوم ازموینه لري"},
            {SuiteConstructor, "د ازموینو ډله '{}' باید پرته له دلیلونو جوړه شي: جوړوونکی یې دلیل نشي اخیستلای"},
            {TestFileStatement, "د ازموینې فایل یوازې ازموینې لري: {} د پروګرام په فایل کې ځای لري، نه د ازموینو تر څنګ"},
            {TestCodeInProduction, "د ازموینې کوډ په اصلي کوډ کې نه منل کېږي: {} د ازموینې په فایل کې ځای لري (هغه فایل چې @ازموینه لري)"},
            {TestsImported, "'{}' ازموینې لري او نشي راوړل کېدای: ازموینې چلېږي، نه کارېږي"},
            {MockWithoutInterface, "پېښه ګر '{}' د هېڅ برسېر ځای نه نیسي: هغه برسېرونه چې عملي کوي یې له ':' وروسته ولیکئ"},
            {BuiltinNeedsImport, "'{0}' د '{1}' ماډیول برخه ده؛ د پروګرام په سر کې 'راوړه {1}' ولیکئ"},
            {UnknownBuiltinModule, "'{0}' په نوم دننی ماډیول نشته (د فایل د راوړو لپاره یې نوم په قوسونو کې ولیکئ: راوړه \"{0}\")"},
            {FunctionArgumentMismatch, "د '{1}' دلیل {0} باید د '{2}' ډول دنده وي، خو '{3}' دی"},
            {NotIterable, "'تکرار ... په' یوې لړۍ، متن یا نقشې ته اړتیا لري، نه '{}'"},
            {EnumNotTopLevel, "د 'ګڼنه' اعلانونه باید د فایل په پورتنۍ کچه وي"},
            {EmptyEnum, "ګڼنه '{}' لږ تر لږه یوه غړي ته اړتیا لري"},
            {DuplicateEnumMember, "ګڼنه '{}' غړی '{}' دوه ځله لیکي"},
            {NoSuchEnumMember, "'{}' '{}' په نوم غړی نه لري (غړي یې {} دي؛ 'ارزښتونه' او 'شمېر' هم منل کېږي)"},
            {EnumNeedsIntOrString, "'{}' له عدد (ځای) یا متن (نوم) څخه جوړېدای شي، نه له '{}' څخه"},
            {NativeTypeNotSupported, "بهرنۍ دنده د '{}' ډول نشي کارولی؛ عدد، اعشاري، منطقي، متن او د بیا-بلنې لپاره د دندې ډول کارولای شي"},
            {NativeNeedsLibrary, "'بهرنی دنده {}' کتابتون ته اړتیا لري: په پروګرام کې 'راوړه بهرنی \"...\"' ولیکئ"},
            {NativeTooManyParameters, "بهرنۍ دنده '{}' له {} څخه ډېر پارامټرونه لري"},
            {NothingNeedsType, "'هېڅ' د جوړښت ډول ته اړتیا لري: جوړښت ته یې ورکړئ، له جوړښت سره یې پرتله کړئ، یا اعلان ته ډول ورکړئ"},
            {TypeArgumentCount, "'{}' په <زاویه قوسونو> کې {} د ډول ارزښت(ونه) غواړي خو {} ورکړل شول"},
            {CannotDeduceTypeParameter, "د '{1}' د ډول پارامټر '{0}' دلته نه معلومېږي: ارزښتونو یا اعلان ته ډول ورکړئ"},
            {UnknownLanguage, "په ژبې لارښوونه کې نامعلومه ژبه '{0}'؛ {1} کارول کېږي"},
            // Runtime
            {DivisionByZero, "په صفر وېش"},
            {IntegerOverflow, "عدد له حده واوښت: {0} {1} {2}"},
            {ArrayIndexOutOfBounds, "لړ شمېره {0} د {1} توکو لړۍ لپاره له حده بهر ده"},
            {StringIndexOutOfBounds, "لړ شمېره {0} د {1} تورو متن لپاره له حده بهر ده"},
            {InvalidIntConversion, "«{}» په عدد نشي اړول کېدای"},
            {NegativeArraySize, "د لړۍ اندازه منفي نشي کېدای ({} ورکړل شو)"},
            {ArrayTooLarge, "د لړۍ اندازه {0} ډېره لویه ده (حد یې {1} دی)"},
            {CallStackOverflow, "سټک ډک شو: له {} څخه ډېرې پرله پسې بلنې (ایا تکراري دنده د درېدو حالت نه لري؟)"},
            {KeyNotFound, "کیلۍ {} په نقشه کې نشته"},
            {RangeOutOfBounds, "واټن {0}..{1} د {2} اوږدوالي لپاره له حده بهر دی"},
            {EmptyArray, "'{}' یوې داسې لړۍ ته اړتیا لري چې تشه نه وي"},
            {NegativeCount, "'{0}' ته ورکړل شوې شمېره منفي نشي کېدای ({1} ورکړل شو)"},
            {NotOneCharacter, "متن.د توري کوډ یو داسې متن غواړي چې یوازې یو توری ولري، نه «{}»"},
            {InvalidCharCode, "{} سمه یونیکوډ شمېره نه ده"},
            {FileReadFailed, "'{0}' نشي لوستل کېدای: {1}"},
            {FileWriteFailed, "'{0}' نشي لیکل کېدای: {1}"},
            {CommandFailed, "امر '{}' نشي چلول کېدای"},
            {FunctionNotSet, "د دندې یو متغیر مخکې له دې چې دنده ورته ورکړل شي وبلل شو"},
            {UnknownColor, "«{}» رنګ نه دی (یو نوم لکه \"red\" یا \"سور\"، \"#ff8800\" یا ګرافیک.رنګ(۲۵۵, ۱۳۶, ۰) وکاروئ)"},
            {ValueOutOfRange, "هغه {0} چې '{1}' ته ورکړل شوی باید له {2} تر {3} وي ({4} ورکړل شو)"},
            {EmptyRandomRange, "تصادفي.عدد({0}, {1}): تر ټولو کوچنی عدد باید تر ټولو لوی عدد څخه لوی نه وي"},
            {TooManyShapes, "له وروستي پاکولو راهیسې له {} څخه ډېر شکلونه کښل شوي (د هر چوکاټ په پیل کې ګرافیک.ټاټ پاک کړه() وبلئ)"},
            {StepLimitExceeded, "د {} لارښوونو حد پوره شو (کېدای شي تکرار هېڅکله پای ته نه رسېږي)"},
            {FloatOverflow, "د {0} {1} {2} پایله د اعشاري شمېرې لپاره ډېره لویه ده"},
            {InvalidFloatConversion, "«{}» په اعشاري شمېره نشي اړول کېدای"},
            {MathDomainError, "'{0}' د {1} لپاره تعریف شوی نه دی"},
            {NativeLibraryNotFound, "بهرنی کتابتون '{0}' نشي پرانیستل کېدای: {1} (باید د پروګرام یا د thundervm تر څنګ وي؛ docs/ps/native.md وګورئ)"},
            {NativeFunctionNotFound, "بهرنی کتابتون '{}' په نوم دنده نه لري"},
            {NativeError, "بهرنۍ دنده ناکامه شوه: {}"},
            {FormatCount, "متن.بڼه کړه خپل کالب ته {} ارزښتونو ته اړتیا لري خو {} ورکړل شول"},
            {FormatPlaceholder, "متن.بڼه کړه: '{{{}}}' بشپړ عدد غواړي، نه {}"},
            {NothingAccess, "ارزښت هېڅ دی: لوستلو یا لیکلو ته برخې نشته"},
            {EnumOutOfRange, "{0} په '{1}' کې ځای نه دی (غړي یې له ۰ تر {2} شمېرل شوي)"},
            {EnumUnknownName, "«{0}» د '{1}' د غړي نوم نه دی"},
            {MethodOfNothing, "ارزښت هېڅ دی: د بللو لپاره دنده نشته"},
            {Deadlock, "بند کرښه: ټول کارونه انتظار باسي او هیڅ یو یې مخته تلای نشي ({})"},
            {WaitInCallback, "'{}' دلته انتظار نشي ایستلای: کار د بلنې (پر هر یو، چاڼ کړه، راټول کړه یا بهرنۍ دنده) په منځ کې نشي یو خوا کېدای"},
            {UnknownHandle, "{0} د دې پروګرام د {1} شمېره نه ده"},
            {LockNotHeld, "'{}' هغه قلف غواړي چې دا کار یې لري"},
            {LockHeldAlready, "دا کار دا قلف لا مخکې لري: بیا یې نیول به تل انتظار وي"},
            {JoinSelf, "یو کار د خپل ځان د پای انتظار نشي کولای"},
            {ProgramFailed, "پروګرام ځان ودراوه: {}"},
            {InvalidBytecode, "ناسم بایټ کوډ: {}"},
        };
        // The API reference in Pashto - signatures and descriptions of the built-ins, the
        // enums and the instructions - is long enough for a file of its own.
        add_pashto_reference(pack);
        return pack;
    }
}
