#include "print_tokens.hpp"

#include <sstream>

const char *const tokenType2ClangStr[] = {
    "unknown",          // 0
    "unknown",          // 1
    "unknown",          // 2
    "unknown",          // 3
    "unknown",          // 4
    "unknown",          // 5
    "unknown",          // 6
    "unknown",          // 7
    "unknown",          // 8
    "unknown",          // 9
    "unknown",          // 10
    "unknown",          // 11
    "unknown",          // 12
    "unknown",          // 13
    "unknown",          // 14
    "unknown",          // 15
    "unknown",          // 16
    "unknown",          // 17
    "unknown",          // 18
    "unknown",          // 19
    "unknown",          // 20
    "break",            // 21
    "unknown",          // 22
    "unknown",          // 23
    "const",            // 24
    "continue",         // 25
    "unknown",          // 26
    "unknown",          // 27
    "unknown",          // 28
    "else",             // 29
    "unknown",          // 30
    "unknown",          // 31
    "unknown",          // 32
    "unknown",          // 33
    "unknown",          // 34
    "if",               // 35
    "unknown",          // 36
    "int",              // 37
    "unknown",          // 38
    "unknown",          // 39
    "unknown",          // 40
    "return",           // 41
    "unknown",          // 42
    "unknown",          // 43
    "unknown",          // 44
    "unknown",          // 45
    "unknown",          // 46
    "unknown",          // 47
    "unknown",          // 48
    "unknown",          // 49
    "unknown",          // 50
    "void",             // 51
    "unknown",          // 52
    "while",            // 53
    "unknown",          // 54
    "unknown",          // 55
    "unknown",          // 56
    "unknown",          // 57
    "unknown",          // 58
    "unknown",          // 59
    "unknown",          // 60
    "unknown",          // 61
    "unknown",          // 62
    "unknown",          // 63
    "l_paren",          // 64
    "r_paren",          // 65
    "l_square",         // 66
    "r_square",         // 67
    "l_brace",          // 68
    "r_brace",          // 69
    "less",             // 70
    "lessequal",        // 71
    "greater",          // 72
    "greaterequal",     // 73
    "unknown",          // 74
    "unknown",          // 75
    "plus",             // 76
    "unknown",          // 77
    "minus",            // 78
    "unknown",          // 79
    "star",             // 80
    "slash",            // 81
    "percent",          // 82
    "unknown",          // 83
    "unknown",          // 84
    "ampamp",           // 85
    "pipepipe",         // 86
    "unknown",          // 87
    "exclaim",          // 88
    "unknown",          // 89
    "unknown",          // 90
    "unknown",          // 91
    "semi",             // 92
    "comma",            // 93
    "equal",            // 94
    "unknown",          // 95
    "unknown",          // 96
    "unknown",          // 97
    "unknown",          // 98
    "unknown",          // 99
    "unknown",          // 100
    "unknown",          // 101
    "unknown",          // 102
    "unknown",          // 103
    "unknown",          // 104
    "equalequal",       // 105
    "exclaimequal",     // 106
    "unknown",          // 107
    "unknown",          // 108
    "unknown",          // 109
    "identifier",       // 110
    "numeric_constant", // 111
    "unknown",          // 112
    "unknown",          // 113
    "unknown",          // 114
    "unknown",          // 115
    "unknown",          // 116
    "unknown",          // 117
    "unknown",          // 118
    "unknown",          // 119
    "unknown",          // 120
};

auto parse_linemarkers(const std::string &s)
{
    struct LineMarkers
    {
        size_t linenum;
        std::string filename;
        std::vector<int> flags;
    };

    char pound;
    LineMarkers lm;
    std::istringstream iss(s);
    iss >> pound >> lm.linenum >> lm.filename;
    lm.filename = lm.filename.substr(1, lm.filename.length() - 2); // 去除双引号
    for (int f; iss >> f;)
    {
        lm.flags.push_back(f);
    }
    return lm;
}

void print_token_clang(const antlr4::Token *token, std::ofstream &outFile, std::string srcFileName, int line,
                       bool startOfLine, bool leadingSpace)
{
    auto tokenType = token->getType();

    auto tokenTypeName = tokenType == -1 ? "eof" : tokenType2ClangStr[tokenType];

    std::string locInfo = std::string{} + "Loc=<" + srcFileName + ":" + std::to_string(line) + ":" +
                          std::to_string(token->getCharPositionInLine() + 1) + ">";

    if (token->getText() != "<EOF>")
        outFile << tokenTypeName << " '" << token->getText() << "'";
    else
        outFile << tokenTypeName << " '" << "'";
    outFile << "\t";
    if (startOfLine)
        outFile << " [StartOfLine]";
    if (leadingSpace)
        outFile << " [LeadingSpace]";
    outFile << "\t";
    outFile << locInfo << std::endl;
}

void print_tokens_clang(antlr4::CommonTokenStream &tokens, std::ofstream &outFile)
{
    size_t line = 1;
    std::string curFileName;
    bool startOfLine = true;
    bool lastIsWhitespace;
    bool lastIsDirective;

    for (auto &&token : tokens.getTokens())
    {
        if (token->getType() == 115) // Directive
        {
            auto lm = parse_linemarkers(token->getText());
            line = lm.linenum;
            curFileName = lm.filename;

            startOfLine = false;
            lastIsWhitespace = false;
            lastIsDirective = true;
        }
        else if (token->getType() == 117) // Whitespace
        {
            // 不影响 startOfLine
            lastIsWhitespace = true;
            lastIsDirective = false;
        }
        else if (token->getType() == 118) // Newline
        {
            if (!lastIsDirective)
            {
                line += 1;
            }

            startOfLine = true;
            lastIsWhitespace = false;
            lastIsDirective = false;
        }
        else
        {
            print_token_clang(token, outFile, curFileName, line, startOfLine, lastIsWhitespace);

            startOfLine = false;
            lastIsWhitespace = false;
            lastIsDirective = false;
        }
    }
}

void print_token_antlr(const antlr4::Token *token, std::ofstream &outFile, std::string srcFileName, int line,
                       bool startOfLine, bool leadingSpace, const CLexer &lexer)
{
    auto &vocabulary = lexer.getVocabulary();

    auto tokenTypeName = token->getText() + ":" + std::to_string(token->getType()) + ":" +
                         std::string(vocabulary.getSymbolicName(token->getType()));

    std::string locInfo = std::string{} + "Loc=<" + srcFileName + ":" + std::to_string(line) + ":" +
                          std::to_string(token->getCharPositionInLine() + 1) + ">";

    if (token->getText() != "<EOF>")
        outFile << tokenTypeName << " '" << token->getText() << "'";
    else
        outFile << tokenTypeName << " '" << "'";
    outFile << "\t";
    if (startOfLine)
        outFile << " [StartOfLine]";
    if (leadingSpace)
        outFile << " [LeadingSpace]";
    outFile << "\t";
    outFile << locInfo << std::endl;
}

void print_tokens_antlr(antlr4::CommonTokenStream &tokens, std::ofstream &outFile, const CLexer &lexer)
{
    size_t line = 1;
    std::string curFileName;
    bool startOfLine = true;
    bool lastIsWhitespace;
    bool lastIsDirective;

    for (auto &&token : tokens.getTokens())
    {
        if (token->getType() == 115) // Directive
        {
            auto lm = parse_linemarkers(token->getText());
            line = lm.linenum;
            curFileName = lm.filename;

            startOfLine = false;
            lastIsWhitespace = false;
            lastIsDirective = true;
        }
        else if (token->getType() == 117) // Whitespace
        {
            // 不影响 startOfLine
            lastIsWhitespace = true;
            lastIsDirective = false;
        }
        else if (token->getType() == 118) // Newline
        {
            if (!lastIsDirective)
            {
                line += 1;
            }

            startOfLine = true;
            lastIsWhitespace = false;
            lastIsDirective = false;
        }
        else
        {
            print_token_antlr(token, outFile, curFileName, line, startOfLine, lastIsWhitespace, lexer);

            startOfLine = false;
            lastIsWhitespace = false;
            lastIsDirective = false;
        }
    }
}