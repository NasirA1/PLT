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
        pack.add_keyword(TokenKind::KwReturn, "راګرځه");
        pack.add_keyword(TokenKind::KwReturn, "راگرځه"); // written with گ (Persian keyboards)
        pack.add_keyword(TokenKind::KwTrue, "رښتیا");
        pack.add_keyword(TokenKind::KwFalse, "دروغ");
        pack.add_keyword(TokenKind::KwInt, "عدد");
        pack.add_keyword(TokenKind::KwBool, "منطقي");
        pack.add_keyword(TokenKind::KwString, "متن");

        // ------------------------------------------------------------ built-in functions
        pack.add_builtin(BuiltinId::Print, "چاپ");
        pack.add_builtin(BuiltinId::Println, "چاپ_کرښه");
        pack.add_builtin(BuiltinId::Len, "اوږدوالی");
        pack.add_builtin(BuiltinId::Str, "متن_کړه");
        pack.add_builtin(BuiltinId::ToInt, "عدد_کړه");
        pack.add_builtin(BuiltinId::Array, "لړۍ");
        pack.add_builtin(BuiltinId::Inspect, "کتنه");
        pack.add_builtin(BuiltinId::Dump, "وښایه");
        pack.add_builtin(BuiltinId::Copy, "کاپي");

        // ------------------------------------------------------------ punctuation, digits, quotes
        pack.add_punctuation(TokenKind::Comma, U'،', true);     // ، Arabic comma
        pack.add_punctuation(TokenKind::Semicolon, U'؛', true); // ؛ Arabic semicolon
        pack.add_punctuation(TokenKind::Percent, U'٪', true);   // ٪ Arabic percent sign
        pack.add_punctuation(TokenKind::Star, U'×', false);     // × multiplication sign
        pack.add_punctuation(TokenKind::Slash, U'÷', false);    // ÷ division sign
        pack.digits = {U'۰', U'۱', U'۲', U'۳', U'۴', U'۵', U'۶', U'۷', U'۸',
            U'۹'}; // ۰ ۱ ۲ ۳ ۴ ۵ ۶ ۷ ۸ ۹
        pack.quotes = {{U'«', U'»'}, {U'"', U'"'}}; // «متن» and "متن"

        pack.display.digits = {"۰", "۱", "۲", "۳", "۴", "۵", "۶", "۷", "۸", "۹"};
        pack.display.true_text = "رښتیا";
        pack.display.false_text = "دروغ";
        pack.display.separator = "، ";
        pack.display.open_quote = "«";
        pack.display.close_quote = "»";

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
            {"in {} (line {})", "په {} کې (کرښه {})"},
            {"a new line, ',' or '}'", "نوې کرښه، '،' یا '}'"},
            {"builtin.print", "ارزښتونه پرته له نوې کرښې چاپوي."},
            {"builtin.println", "ارزښتونه چاپوي او وروسته نوې کرښه پیلوي."},
            {"builtin.len", "د متن د تورو یا د لړۍ د توکو شمېر."},
            {"builtin.str", "هر ارزښت په متن اړوي."},
            {"builtin.to_int", "متن په عدد اړوي؛ که متن عدد نه وي، د اجرا تېروتنه رامنځته کېږي."},
            {"builtin.array", "د یو ارزښت n کاپيانې لرونکې لړۍ جوړوي (لړۍ او جوړښتونه کاپي کېږي)."},
            {"builtin.inspect", "د ارزښت بشپړ، څو کرښیز بیان؛ جوړښتونه برخه په برخه د ډولونو سره ښيي."},
            {"builtin.dump", "د کتنه(ارزښت) پایله چاپوي او وروسته نوې کرښه پیلوي."},
            {"builtin.copy", "د جوړښت یا لړۍ خپلواکه کاپي (دننه ارزښتونه هم کاپي کېږي)."},
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
            // Syntax analysis
            {ExpectedToken, "تمه د {0} وه، خو {1} پیدا شو"},
            {ExpectedExpression, "تمه د یوې افادې وه، خو {} پیدا شو"},
            {ExpectedType, "تمه د یو ډول (عدد، منطقي، متن یا جوړښت) وه، خو {} پیدا شو"},
            {ExpectedStatement, "تمه د یو امر وه، خو {} پیدا شو"},
            {InvalidAssignmentTarget, "ناسمه موخه: یوازې متغیرونو، د لړۍ توکو او د جوړښت برخو ته ارزښت ورکول کېدای شي"},
            {NotCallable, "دا افاده نشي بلل کېدای؛ یوازې نوم لرونکې دندې بلل کېدای شي"},
            {NestedFunction, "دندې یوازې د پروګرام په سر کچه کې اعلانېدای شي"},
            {VarNeedsTypeOrValue, "متغیر '{}' ډول یا لومړني ارزښت ته اړتیا لري"},
            {TooManyErrors, "ډېرې تېروتنې؛ کار ودرېد"},
            {ExpectedStatementEnd, "تمه د امر د پای وه (نوې کرښه یا '؛')، خو {} پیدا شو"},
            {StructNotTopLevel, "جوړښتونه یوازې د پروګرام په سر کچه کې اعلانېدای شي"},
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
            {FunctionUsedAsValue, "'{0}' دنده ده؛ په قوسونو یې وبولئ، لکه {1}(...)"},
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
            {StepLimitExceeded, "د {} لارښوونو حد پوره شو (کېدای شي تکرار هېڅکله پای ته نه رسېږي)"},
            {InvalidBytecode, "ناسم بایټ کوډ: {}"},
        };
        return pack;
    }
}
