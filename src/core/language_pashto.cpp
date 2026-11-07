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
        pack.add_keyword(TokenKind::KwIf, "که");
        pack.add_keyword(TokenKind::KwElse, "کنه");
        pack.add_keyword(TokenKind::KwWhile, "ترڅو");
        pack.add_keyword(TokenKind::KwFor, "لپاره");
        pack.add_keyword(TokenKind::KwBreak, "ودرېږه");
        pack.add_keyword(TokenKind::KwBreak, "ودریږه"); // written with ی instead of ې
        pack.add_keyword(TokenKind::KwContinue, "دوام");
        pack.add_keyword(TokenKind::KwPass, "تېر");
        pack.add_keyword(TokenKind::KwImport, "راوړه");
        pack.add_keyword(TokenKind::KwReturn, "راګرځه");
        pack.add_keyword(TokenKind::KwReturn, "راگرځه"); // written with گ (Persian keyboards)
        pack.add_keyword(TokenKind::KwTrue, "رښتیا");
        pack.add_keyword(TokenKind::KwFalse, "دروغ");
        pack.add_keyword(TokenKind::KwInt, "عدد");
        pack.add_keyword(TokenKind::KwBool, "منطقي");
        pack.add_keyword(TokenKind::KwString, "متن");

        // ------------------------------------------------------------ built-in functions
        // Multi-word names are written with a space between the words, as in ordinary Pashto;
        // the lexer joins them into one token.
        pack.add_builtin(BuiltinId::Print, "چاپ");
        pack.add_builtin(BuiltinId::Println, "کرښه چاپ");
        pack.add_builtin(BuiltinId::Len, "اوږدوالی");
        pack.add_builtin(BuiltinId::Str, "متن کړه");
        pack.add_builtin(BuiltinId::ToInt, "عدد کړه");
        pack.add_builtin(BuiltinId::Array, "لړۍ");
        pack.add_builtin(BuiltinId::Inspect, "کتنه");
        pack.add_builtin(BuiltinId::Dump, "وښایه");
        pack.add_builtin(BuiltinId::Copy, "کاپي");
        pack.add_builtin(BuiltinId::Has, "لري");
        pack.add_builtin(BuiltinId::Remove, "لرې کړه");
        pack.add_builtin(BuiltinId::Keys, "کیلۍ");
        pack.add_builtin(BuiltinId::Values, "ارزښتونه");
        // input
        pack.add_builtin(BuiltinId::Input, "ولوله");
        pack.add_builtin(BuiltinId::EndOfInput, "د ننوت پای");
        // text
        pack.add_builtin(BuiltinId::Substring, "د متن برخه");
        pack.add_builtin(BuiltinId::Trim, "پاک کړه");
        pack.add_builtin(BuiltinId::ToUpper, "لوی توري کړه");
        pack.add_builtin(BuiltinId::ToLower, "واړه توري کړه");
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
        pack.add_builtin(BuiltinId::RemoveAt, "له ځایه لرې کړه");
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
        pack.add_builtin(BuiltinId::ReadFile, "فایل ولوله");
        pack.add_builtin(BuiltinId::WriteFile, "فایل ولیکه");
        pack.add_builtin(BuiltinId::AppendFile, "فایل ته ورزیات کړه");
        pack.add_builtin(BuiltinId::FileExists, "فایل شته");
        pack.add_builtin(BuiltinId::RemoveFile, "فایل ړنګ کړه");
        pack.add_builtin(BuiltinId::ListDir, "د پوښۍ توکي");
        pack.add_builtin(BuiltinId::CurrentDir, "اوسنۍ پوښۍ");
        pack.add_builtin(BuiltinId::Time, "وخت");
        pack.add_builtin(BuiltinId::ClockMs, "ساعت");
        pack.add_builtin(BuiltinId::Sleep, "انتظار");
        pack.add_builtin(BuiltinId::DateTime, "نېټه او وخت");
        pack.add_builtin(BuiltinId::Platform, "عامل سیسټم");
        pack.add_builtin(BuiltinId::Exit, "پای ته ورسوه");

        // random numbers
        pack.add_builtin(BuiltinId::RandomInt, "تصادفي عدد");
        pack.add_builtin(BuiltinId::SeedRandom, "تصادفي تخم");
        // module graphics
        pack.add_builtin(BuiltinId::Canvas, "کینوس");
        pack.add_builtin(BuiltinId::Clear, "کینوس پاک کړه");
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
        pack.add_builtin(BuiltinId::KeyPressed, "کېکاږل شوې تڼۍ");
        pack.add_builtin(BuiltinId::KeyDown, "تڼۍ کېکاږل شوې ده");
        pack.add_builtin(BuiltinId::MouseX, "د موږک x");
        pack.add_builtin(BuiltinId::MouseY, "د موږک y");
        pack.add_builtin(BuiltinId::MouseDown, "موږک کېکاږل شوی");

        // ------------------------------------------------------------ built-in modules
        pack.add_module(BuiltinModule::Os, "سیسټم"); // "os" is accepted too, in every language
        pack.add_module(BuiltinModule::Graphics, "ګرافیک");

        // ------------------------------------------------------------ punctuation, digits, quotes
        // Pashto programs use the ASCII punctuation and quotes of the English form (that is what
        // the translator writes); the Arabic forms are accepted as well. Only the digits differ.
        pack.add_punctuation(TokenKind::Comma, U'،', false);     // ، Arabic comma
        pack.add_punctuation(TokenKind::Semicolon, U'؛', false); // ؛ Arabic semicolon
        pack.add_punctuation(TokenKind::Percent, U'٪', false);   // ٪ Arabic percent sign
        pack.add_punctuation(TokenKind::Star, U'×', false);      // × multiplication sign
        pack.add_punctuation(TokenKind::Slash, U'÷', false);     // ÷ division sign
        pack.digits = {U'۰', U'۱', U'۲', U'۳', U'۴', U'۵', U'۶', U'۷', U'۸',
            U'۹'}; // ۰ ۱ ۲ ۳ ۴ ۵ ۶ ۷ ۸ ۹
        pack.quotes = {{U'"', U'"'}, {U'«', U'»'}}; // "متن" and «متن»

        pack.display.digits = {"۰", "۱", "۲", "۳", "۴", "۵", "۶", "۷", "۸", "۹"};
        pack.display.true_text = "رښتیا";
        pack.display.false_text = "دروغ";
        pack.display.separator = "، ";
        pack.display.open_quote = "«";
        pack.display.close_quote = "»";
        pack.display.function_word = "دنده";

        // ------------------------------------------------------------ words used in messages
        pack.phase_names = {"لغوي", "نحوي", "معنايي", "د اجرا"};
        pack.error_word = "تېروتنه";
        pack.warning_word = "خبرداری";
        pack.terms = {
            {"end of file", "د فایل پای"},
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
            {"a new line, ',' or ';'", "نوې کرښه، ',' یا ';'"},
            {"an indented line", "یوه ننوتې کرښه"},
            {"the end of the block", "د بلاک پای"},
            {"builtin.print", "ارزښتونه پرته له نوې کرښې چاپوي."},
            {"builtin.println", "ارزښتونه چاپوي او وروسته نوې کرښه پیلوي."},
            {"builtin.len", "د متن د تورو یا د لړۍ د توکو شمېر."},
            {"builtin.str", "هر ارزښت په متن اړوي."},
            {"builtin.to_int", "متن په عدد اړوي؛ که متن عدد نه وي، د اجرا تېروتنه رامنځته کېږي."},
            {"builtin.array", "د یو ارزښت n کاپيانې لرونکې لړۍ جوړوي (لړۍ او جوړښتونه کاپي کېږي)."},
            {"builtin.inspect", "د ارزښت بشپړ، څو کرښیز بیان؛ جوړښتونه برخه په برخه د ډولونو سره ښيي."},
            {"builtin.dump", "د کتنه(ارزښت) پایله چاپوي او وروسته نوې کرښه پیلوي."},
            {"builtin.copy", "د جوړښت، لړۍ، ټاپل یا نقشې خپلواکه کاپي (دننه ارزښتونه هم کاپي کېږي)."},
            {"builtin.has", "که نقشه دا کیلۍ ولري رښتیا راګرځوي."},
            {"builtin.remove", "کیلۍ او ارزښت یې له نقشې لرې کوي (که کیلۍ نه وي، هېڅ نه کېږي)."},
            {"builtin.keys", "د نقشې کیلۍ د لړۍ په بڼه، د ورزیاتېدو په ترتیب."},
            {"builtin.values", "د نقشې ارزښتونه د لړۍ په بڼه، د کیلیو د ورزیاتېدو په ترتیب."},
            {"a module name or a file name in quotes", "د ماډیول نوم یا د فایل نوم په قوسونو \"\" کې"},
            {"builtin.input", "پوښتنه (که وي) چاپوي، بیا له ننوت څخه یوه کرښه لولي؛ د ننوت په پای کې \"\" راګرځوي."},
            {"builtin.end_of_input", "که ننوت نورې کرښې ونه لري رښتیا راګرځوي."},
            {"builtin.substring", "له start څخه تر end پورې (end پرته) توري."},
            {"builtin.trim", "متن د پیل او پای له تشو ځایونو پرته."},
            {"builtin.to_upper", "متن چې a-z توري یې په A-Z بدل شوي."},
            {"builtin.to_lower", "متن چې A-Z توري یې په a-z بدل شوي."},
            {"builtin.split", "د جلا کوونکي ترمنځ د متن ټوټې (د \"\" سره، یو یو توری)."},
            {"builtin.join", "ټوټې یو متن کوي، د جلا کوونکي سره د هغوی ترمنځ."},
            {"builtin.replace", "متن چې هر old پکې په new بدل شوی."},
            {"builtin.starts_with", "که متن په prefix پیل شي رښتیا."},
            {"builtin.ends_with", "که متن په suffix پای ته ورسېږي رښتیا."},
            {"builtin.contains", "که متن دا برخه یا لړۍ دا توکی ولري رښتیا."},
            {"builtin.index_of", "د برخې یا توکي د لومړي ځل ځای، یا -۱ که نه وي."},
            {"builtin.repeat", "متن n ځله تکرار شوی."},
            {"builtin.reverse", "نوی متن یا لړۍ په سرچپه ترتیب."},
            {"builtin.char_code", "د یو توري د متن یونیکوډ شمېره."},
            {"builtin.from_char_code", "هغه یو توری چې دا یونیکوډ شمېره لري."},
            {"builtin.is_int", "که عدد کړه(s) بریالی وي رښتیا."},
            {"builtin.first", "د ټاپل لومړی توکی."},
            {"builtin.second", "د ټاپل دوهم توکی."},
            {"builtin.swap", "جوړه چې دواړه توکي یې ځای بدل کړي."},
            {"builtin.zip", "په یو شان ځایونو توکي جوړې کوي (د لنډې لړۍ په اندازه)."},
            {"builtin.enumerate", "هر توکی د خپل ځای له شمېرې سره جوړه کوي."},
            {"builtin.items", "د نقشې توکي د (کیلۍ، ارزښت) جوړو په بڼه."},
            {"builtin.push", "ارزښت د لړۍ پای ته ورزیاتوي."},
            {"builtin.pop", "د لړۍ وروستی توکی لرې کوي او راګرځوي یې."},
            {"builtin.insert", "ارزښت په ټاکلي ځای کې ننباسي."},
            {"builtin.remove_at", "په ټاکلي ځای کې توکی لرې کوي او راګرځوي یې."},
            {"builtin.slice", "نوی متن یا لړۍ له start څخه تر end پورې (end پرته)."},
            {"builtin.concat", "نوې لړۍ: د a توکي او بیا د b توکي."},
            {"builtin.sort", "نوې لړۍ چې توکي یې له کوچني نه تر لوی ترتیب شوي (عددونه یا متنونه)."},
            {"builtin.range", "له start څخه تر end پورې (end پرته) عددونه."},
            {"builtin.sum", "د عددونو مجموعه."},
            {"builtin.map", "نوې لړۍ چې f پر هر توکي کارول شوې."},
            {"builtin.filter", "نوې لړۍ د هغو توکو چې keep ورته رښتیا راګرځوي."},
            {"builtin.reduce", "توکي له کیڼ نه ښي ته یو ځای کوي: f(f(initial, a[0]), a[1]) ..."},
            {"builtin.get", "د کیلۍ ارزښت، یا default که نقشه دا کیلۍ ونه لري."},
            {"builtin.args", "هغه دلیلونه چې پروګرام ته ورکړل شوي (thundervm program.thc a b c)."},
            {"builtin.get_env", "د چاپېریال د متغیر ارزښت، یا \"\" که نه وي ټاکل شوی."},
            {"builtin.run", "د عامل سیسټم یو امر چلوي؛ وتنه یې د پروګرام وتنه کېږي. د وتلو حالت راګرځوي."},
            {"builtin.run_output", "یو امر چلوي او د وتلو حالت او وتنه یې راګرځوي."},
            {"builtin.read_file", "د متني فایل منځپانګه."},
            {"builtin.write_file", "متني فایل جوړوي یا یې بیا لیکي."},
            {"builtin.append_file", "د فایل پای ته متن ورزیاتوي."},
            {"builtin.file_exists", "که په دې لار فایل یا پوښۍ وي رښتیا."},
            {"builtin.remove_file", "فایل ړنګوي (که نه وي، هېڅ نه کېږي)."},
            {"builtin.list_dir", "د پوښۍ د توکو نومونه، ترتیب شوي."},
            {"builtin.current_dir", "د پروګرام اوسنۍ کاري پوښۍ."},
            {"builtin.time", "له ۱۹۷۰ کال د جنورۍ له لومړۍ نېټې راهیسې ثانیې (UTC)."},
            {"builtin.clock_ms", "د پروګرام له پیل راهیسې ملي ثانیې."},
            {"builtin.sleep", "پروګرام د ټاکلو ملي ثانیو لپاره ودروي."},
            {"builtin.date_time", "سیمه ییزه نېټه او وخت: \"YYYY-MM-DD HH:MM:SS\"."},
            {"builtin.platform", "عامل سیسټم: \"linux\"، \"windows\"، \"macos\" یا \"unknown\"."},
            {"builtin.exit", "پروګرام سمدستي د ورکړل شوي حالت سره پای ته رسوي."},
            {"builtin.random_int", "له low څخه تر high پورې (دواړه پکې شامل) یو تصادفي بشپړ عدد."},
            {"builtin.seed_random", "تصادفي عددونه له یو تخم څخه بیا پیلوي، نو پروګرام کولای شي همغه لړۍ تکرار کړي."},
            {"builtin.canvas", "د انځور ساحې اندازه په پکسلونو ټاکي او سپینه یې کوي (اصلي اندازه ۴۰۰ × ۳۰۰)."},
            {"builtin.clear", "ټول کینوس په یو رنګ ډکوي او مخکې انځور شوي شیان لرې کوي."},
            {"builtin.pixel", "یو پکسل رنګوي."},
            {"builtin.line", "له (x1، y1) څخه تر (x2، y2) پورې لیکه کاږي."},
            {"builtin.rect", "ډک مستطیل کاږي چې پورته کیڼ کونج یې (x، y) دی."},
            {"builtin.rect_outline", "د مستطیل څنډه کاږي."},
            {"builtin.circle", "د (x، y) مرکز شاوخوا ډکه دایره کاږي."},
            {"builtin.circle_outline", "د دایرې څنډه کاږي."},
            {"builtin.text", "متن لیکي چې پورته کیڼ کونج یې په (x، y) کې دی."},
            {"builtin.text_size", "د هغه متن لوړوالی په پکسلونو چې له دې وروسته لیکل کېږي (اصلي ۱۶)."},
            {"builtin.line_width", "د هغو لیکو او څنډو پلنوالی چې له دې وروسته کښل کېږي (اصلي ۱)."},
            {"builtin.rgb", "هغه رنګ چې دومره سور، شین او آبي ولري (هر یو له ۰ تر ۲۵۵)."},
            {"builtin.frame", "انځور ښيي، بیا د حرکت د راتلونکي چوکاټ لپاره انتظار کوي."},
            {"builtin.show", "انځور همدا اوس ښيي، پرته له انتظاره."},
            {"builtin.frame_rate", "چوکاټ په یوه ثانیه کې څو چوکاټونه ښيي (له ۱ تر ۱۲۰، اصلي ۳۰)."},
            {"builtin.key_pressed", "هغه بله تڼۍ چې کاروونکي کېکاږلې (\"left\"، \"space\"، \"a\" ...)، یا \"\" که نه وي."},
            {"builtin.key_down", "تر هغه وخته رښتیا چې تڼۍ کېکاږل شوې وي."},
            {"builtin.mouse_x", "په کینوس د موږک x ځای."},
            {"builtin.mouse_y", "په کینوس د موږک y ځای."},
            {"builtin.mouse_down", "تر هغه وخته رښتیا چې د موږک تڼۍ په کینوس کېکاږل شوې وي."},
            {"width", "پلنوالی"},
            {"height", "لوړوالی"},
            {"frame rate", "د چوکاټونو شمېر"},
            {"text size", "د متن اندازه"},
            {"line width", "د لیکې پلنوالی"},
            {"radius", "وړانګه"},
        };

        // ------------------------------------------------------------ messages
        using enum DiagId;
        pack.messages = {
            // Lexical analysis
            {UnexpectedCharacter, "ناڅاپي توری '{}'"},
            {UnexpectedCharacterHint, "ناڅاپي توری '{0}'؛ ایا مو '{1}' مطلب و؟"},
            {UnterminatedString, "متن نه دی بند شوی (متن باید په همغه کرښه کې په » یا \" بند شي)"},
            {InvalidEscape, "ناسم ځانګړی ترکیب '\\{}' (سم ترکیبونه: \\n \\t \\r \\0 \\\" \\\\ \\« \\»)"},
            {IntegerTooLarge, "عدد {} ډېر لوی دی (تر ټولو لوی عدد ۹۲۲۳۳۷۲۰۳۶۸۵۴۷۷۵۸۰۷ دی)"},
            {MalformedNumber, "ناسم عدد '{}'"},
            {UnterminatedComment, "تبصره نه ده بنده شوې (*/ نشته)"},
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
            {ExpectedStatementEnd, "تمه د امر د پای وه (نوې کرښه یا ';')، خو {} پیدا شو"},
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
            {BadMainSignature, "'main' باید پارامټرونه او د راګرځېدو ډول ونه لري"},
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
            {ModuleDefinesMain, "ماډیول 'main' نشي تعریفولای؛ یوازې هغه پروګرام چې چلېږي یې تعریفولای شي"},
            {BuiltinNeedsImport, "'{0}' د '{1}' ماډیول برخه ده؛ د پروګرام په سر کې 'راوړه {1}' ولیکئ"},
            {UnknownBuiltinModule, "'{0}' په نوم دننی ماډیول نشته (د فایل د راوړو لپاره یې نوم په قوسونو کې ولیکئ: راوړه \"{0}\")"},
            {FunctionArgumentMismatch, "د '{1}' دلیل {0} باید د '{2}' ډول دنده وي، خو '{3}' دی"},
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
            {NotOneCharacter, "د توري کوډ یو داسې متن غواړي چې یوازې یو توری ولري، نه «{}»"},
            {InvalidCharCode, "{} سمه یونیکوډ شمېره نه ده"},
            {FileReadFailed, "'{0}' نشي لوستل کېدای: {1}"},
            {FileWriteFailed, "'{0}' نشي لیکل کېدای: {1}"},
            {CommandFailed, "امر '{}' نشي چلول کېدای"},
            {FunctionNotSet, "د دندې یو متغیر مخکې له دې چې دنده ورته ورکړل شي وبلل شو"},
            {UnknownColor, "«{}» رنګ نه دی (یو نوم لکه \"red\" یا \"سور\"، \"#ff8800\" یا رنګ(۲۵۵, ۱۳۶, ۰) وکاروئ)"},
            {ValueOutOfRange, "هغه {0} چې '{1}' ته ورکړل شوی باید له {2} تر {3} وي ({4} ورکړل شو)"},
            {EmptyRandomRange, "تصادفي عدد({0}, {1}): تر ټولو کوچنی عدد باید تر ټولو لوی عدد څخه لوی نه وي"},
            {TooManyShapes, "له وروستي پاکولو راهیسې له {} څخه ډېر شکلونه کښل شوي (د هر چوکاټ په پیل کې کینوس پاک کړه() وبلئ)"},
            {StepLimitExceeded, "د {} لارښوونو حد پوره شو (کېدای شي تکرار هېڅکله پای ته نه رسېږي)"},
            {InvalidBytecode, "ناسم بایټ کوډ: {}"},
        };
        return pack;
    }
}
