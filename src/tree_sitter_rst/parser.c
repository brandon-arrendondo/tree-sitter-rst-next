#include "parser.h"

#include "chars.h"
#include "tokens.h"
#include <stdio.h>

/// Whether the line starts an item or block that may follow the one before
/// it without a blank line: a bullet, enumerated, field, option or line
/// block item, or an explicit markup block.
static bool starts_continuing_item(const RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    const int32_t current = scanner->lookahead;

    return (valid_symbols[T_CHAR_BULLET] && is_char_bullet(current))
           || (valid_symbols[T_NUMERIC_BULLET] && (is_numeric_bullet(current) || current == '('))
           || (valid_symbols[T_FIELD_MARK] && current == ':')
           || (valid_symbols[T_OPTION_MARK] && (current == '-' || current == '+' || current == '/'))
           || (valid_symbols[T_EXPLICIT_MARKUP_START] && current == '.')
           || (valid_symbols[T_LINE_BLOCK_MARK] && current == '|');
}

/// Whether the line after the current one is indented past `indent`, as the
/// definition of a definition list term is.
static bool next_line_indented_past(RSTScanner *scanner, int indent)
{
    while (!is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }
    if (scanner->lookahead == CHAR_EOF) {
        return false;
    }
    scanner->advance(scanner);
    const int next_indent = get_indent_level(scanner);
    return !is_newline(scanner->lookahead) && next_indent > indent;
}

/// Whether the line is made of adornment characters and spaces only, like a
/// table border (`=====  =====`, `+----+----+`). Consumes what it reads.
static bool is_border_line(RSTScanner *scanner)
{
    if (!is_adornment_char(scanner->lookahead)) {
        return false;
    }
    while (is_adornment_char(scanner->lookahead) || scanner->lookahead == CHAR_SPACE) {
        scanner->advance(scanner);
    }
    return is_newline(scanner->lookahead);
}

/// Whether the line after a dedent that no blank line preceded goes on with
/// the construct the dedent returned to: another item (see
/// `starts_continuing_item`) or a definition list term. Anything else ends
/// the construct, as docutils does after an unexpected unindent. A border
/// line is not taken to end it: tables are not parsed yet, and a table
/// should stay a syntax error rather than be read as other elements.
///
/// The lookahead is at the first character of the line; the scanner may
/// advance past it, since the caller only returns zero-width tokens.
static bool continues_after_unindent(RSTScanner *scanner, int indent)
{
    return starts_continuing_item(scanner)
           || is_border_line(scanner)
           || (scanner->valid_symbols[T_TEXT] && next_line_indented_past(scanner, indent));
}

/// Whether whitespace with `newlines` line breaks, ending at `indent`, ends
/// a body element: a blank line, the end of the file, or a line after an
/// unexpected unindent.
static bool ends_body_element(RSTScanner *scanner, int newlines, int indent)
{
    return newlines > 1
           || scanner->lookahead == CHAR_EOF
           || (scanner->after_unindent && !continues_after_unindent(scanner, indent));
}

/// Skip whitespace and line breaks, counting the line breaks in `newlines`
/// (the end of the file counts as one). Returns the indent of the last line.
static int skip_whitespace(RSTScanner *scanner, int *newlines)
{
    int indent = 0;

    while (true) {
        switch (scanner->lookahead) {
            case CHAR_SPACE:
            case CHAR_VERTICAL_TAB:
            case CHAR_FORM_FEED:
            case CHAR_NBSP:
                indent += 1;
                break;
            case CHAR_TAB:
                indent += TAB_STOP;
                break;
            case CHAR_NEWLINE:
                (*newlines)++;
                indent = 0;
                break;
            case CHAR_CARRIAGE_RETURN:
                indent = 0;
                break;
            case CHAR_EOF:
                (*newlines)++;
                return 0;
            default:
                return indent;
        }
        scanner->skip(scanner);
    }
}

static bool parse_indent(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    lexer->mark_end(lexer);

    // Whitespace in the middle of a line (e.g. between a footnote label and
    // its text) is a separator, not an indentation.
    bool at_line_start = lexer->get_column(lexer) == 0;
    int newlines = 0;
    const int indent = skip_whitespace(scanner, &newlines);

    if (!newlines && scanner->lookahead == ':' && valid_symbols[T_CLASSIFIER_MARK]) {
        return parse_classifier_mark(scanner);
    }

    int current_indent = scanner->back(scanner);
    if (indent > current_indent && valid_symbols[T_INDENT] && (newlines || at_line_start)) {
        scanner->push(scanner, indent);
        lexer->result_symbol = T_INDENT;
        return true;
    }
    if (newlines) {
        if (indent < current_indent && valid_symbols[T_DEDENT]) {
            scanner->pop(scanner);
            scanner->unindented = newlines == 1;
            lexer->result_symbol = T_DEDENT;
            return true;
        }

        if (valid_symbols[T_BLANKLINE] && ends_body_element(scanner, newlines, indent)) {
            lexer->result_symbol = T_BLANKLINE;
            return true;
        }

        if (newlines == 1 && valid_symbols[T_NEWLINE_INDENT] && indent > current_indent) {
            scanner->push(scanner, indent);
            lexer->result_symbol = T_NEWLINE_INDENT;
            return true;
        }

        if (valid_symbols[T_NEWLINE]) {
            lexer->result_symbol = T_NEWLINE;
            return true;
        }
    }
    return false;
}

static bool parse_overline(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;
    int32_t adornment = scanner->lookahead;

    if (!is_adornment_char(scanner->lookahead)
        || (!valid_symbols[T_OVERLINE] && !valid_symbols[T_TRANSITION])) {
        return false;
    }

    scanner->advance(scanner);
    lexer->mark_end(lexer);
    int overline_length = 1;

    while (true) {
        if (scanner->lookahead != adornment) {
            bool ok = fallback_adornment(scanner, adornment, overline_length);
            if (ok) {
                return true;
            }
            if (is_space(scanner->lookahead)) {
                break;
            }
            return parse_text(scanner, false);
        }
        scanner->advance(scanner);
        overline_length++;
    }

    // Mark the transition token
    lexer->mark_end(lexer);

    // Consume all whitespaces.
    while (is_space(scanner->lookahead) && !is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }

    if (!is_newline(scanner->lookahead)) {
        return parse_text(scanner, false);
    }

    scanner->advance(scanner);

    bool is_empty = true;
    while (!is_newline(scanner->lookahead)) {
        if (!is_space(scanner->lookahead)) {
            is_empty = false;
        }
        scanner->advance(scanner);
    }

    if (is_empty) {
        // Transitions need to be at least 4 chars long
        if (overline_length >= 4 && valid_symbols[T_TRANSITION]) {
            lexer->result_symbol = T_TRANSITION;
            return true;
        }
        return parse_text(scanner, false);
    }

    scanner->advance(scanner);

    int underline_length = 0;

    while (!is_newline(scanner->lookahead)) {
        if (scanner->lookahead != adornment) {
            if (is_space(scanner->lookahead)) {
                break;
            }
            return parse_text(scanner, false);
        }
        scanner->advance(scanner);
        underline_length++;
    }

    // Consume all whitespaces.
    while (is_space(scanner->lookahead) && !is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }

    if (!is_newline(scanner->lookahead)) {
        return parse_text(scanner, false);
    }

    // The overline_length and the underline_length length must match
    if (overline_length >= 1 && overline_length == underline_length) {
        lexer->result_symbol = T_OVERLINE;
        return true;
    }

    return parse_text(scanner, false);
}

static bool parse_underline(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;
    int32_t adornment = scanner->lookahead;

    if (!is_adornment_char(scanner->lookahead)
        || (!valid_symbols[T_UNDERLINE] && !valid_symbols[T_TRANSITION])) {
        return false;
    }

    scanner->advance(scanner);
    lexer->mark_end(lexer);
    int underline_length = 1;

    while (!is_newline(scanner->lookahead)) {
        if (scanner->lookahead != adornment) {
            bool ok = fallback_adornment(scanner, adornment, underline_length);
            if (ok) {
                return true;
            }
            if (is_space(scanner->lookahead)) {
                break;
            }
            return parse_text(scanner, false);
        }
        scanner->advance(scanner);
        underline_length++;
    }

    // Mark the transition token
    lexer->mark_end(lexer);

    // Consume all whitespaces.
    while (is_space(scanner->lookahead) && !is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }

    if (!is_newline(scanner->lookahead)) {
        return parse_text(scanner, false);
    }

    // Transitions need to be at least 4 chars long
    if (underline_length >= 4 && valid_symbols[T_TRANSITION]) {
        lexer->result_symbol = T_TRANSITION;
        return true;
    }

    if (underline_length >= 1 && valid_symbols[T_UNDERLINE]) {
        lexer->result_symbol = T_UNDERLINE;
        return true;
    }

    return parse_text(scanner, false);
}

/// If the adnorment is not valid, try to parse a different token.
///
/// Lots of adornments are also valid tokens, so we need to check for each one of them.
static bool fallback_adornment(RSTScanner *scanner, int32_t adornment, int adornment_length)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (adornment_length == 1) {
        if (is_space(scanner->lookahead)) {
            if (is_char_bullet(adornment) && valid_symbols[T_CHAR_BULLET]) {
                bool ok = parse_inner_list_element(scanner, 1, T_CHAR_BULLET);
                if (ok) {
                    return true;
                }
            }
            else if (adornment == '|' && valid_symbols[T_LINE_BLOCK_MARK]) {
                bool ok = parse_inner_list_element(scanner, 1, T_LINE_BLOCK_MARK);
                if (ok) {
                    return true;
                }
            }
        }
        else {
            if ((adornment == '-' || adornment == '+' || adornment == '/')
                && valid_symbols[T_OPTION_MARK]) {
                bool ok = parse_option_mark(scanner, adornment, 1);
                if (ok) {
                    return true;
                }
            }
            if (adornment == '*' && valid_symbols[T_EMPHASIS]) {
                return parse_inner_inline_markup(scanner, IM_EMPHASIS);
            }
            if (adornment == ':' && (valid_symbols[T_ROLE_NAME_PREFIX] || valid_symbols[T_ROLE_NAME_SUFFIX])) {
                bool ok = parse_inner_role(scanner);
                if (ok) {
                    return true;
                }
                return parse_text(scanner, false);
            }
            if (adornment == ':' && valid_symbols[T_FIELD_MARK]) {
                bool ok = parse_inner_field_mark(scanner);
                if (ok) {
                    return true;
                }
                return parse_text(scanner, false);
            }
            if (adornment == '`' && (valid_symbols[T_INTERPRETED_TEXT] || valid_symbols[T_INTERPRETED_TEXT_PREFIX] || valid_symbols[T_REFERENCE])) {
                return parse_inner_inline_markup(scanner, IM_INTERPRETED_TEXT | IM_INTERPRETED_TEXT_PREFIX | IM_REFERENCE);
            }
            if (adornment == '|' && valid_symbols[T_SUBSTITUTION_REFERENCE]) {
                return parse_inner_inline_markup(scanner, IM_SUBSTITUTION_REFERENCE);
            }
            if (adornment == '_' && scanner->lookahead == '`' && valid_symbols[T_INLINE_TARGET]) {
                return parse_inner_inline_markup(scanner, IM_INLINE_TARGET);
            }
            if (adornment == '[' && (valid_symbols[T_FOOTNOTE_REFERENCE] || valid_symbols[T_CITATION_REFERENCE])) {
                return parse_inner_inline_markup(scanner, IM_FOOTNOTE_REFERENCE | IM_CITATION_REFERENCE);
            }
            if (adornment == '#'
                && (scanner->lookahead == '.' || scanner->lookahead == ')')) {
                scanner->advance(scanner);
                bool ok = parse_inner_list_element(scanner, 2, T_NUMERIC_BULLET);
                if (ok) {
                    return true;
                }
            }
            if (adornment == '(' && is_numeric_bullet(scanner->lookahead) && valid_symbols[T_NUMERIC_BULLET]) {
                return parse_inner_numeric_bullet(scanner, true);
            }
        }
    }
    else if (adornment_length >= 2) {
        if (adornment_length == 2 && adornment == '-'
            && !is_space(scanner->lookahead)
            && valid_symbols[T_OPTION_MARK]) {
            bool ok = parse_option_mark(scanner, '-', 2);
            if (ok) {
                return true;
            }
        }
        if (is_space(scanner->lookahead)) {
            if (adornment_length == 3
                && adornment == '>'
                && valid_symbols[T_DOCTEST_BLOCK_MARK]) {
                lexer->mark_end(lexer);
                lexer->result_symbol = T_DOCTEST_BLOCK_MARK;
                return true;
            }
            if (adornment_length == 2 && adornment == '.') {
                return parse_inner_list_element(scanner, 2, T_EXPLICIT_MARKUP_START);
            }
            if (adornment_length == 2
                && adornment == '_'
                && valid_symbols[T_ANONYMOUS_TARGET_MARK]) {
                lexer->mark_end(lexer);
                lexer->result_symbol = T_ANONYMOUS_TARGET_MARK;
                return true;
            }
            if (adornment_length == 2
                && adornment == ':'
                && (valid_symbols[T_LITERAL_INDENTED_BLOCK_MARK] || valid_symbols[T_LITERAL_QUOTED_BLOCK_MARK])) {
                return parse_innner_literal_block_mark(scanner);
            }
        }
        else {
            if (adornment == '*' && valid_symbols[T_STRONG]) {
                return parse_inner_inline_markup(scanner, IM_STRONG);
            }
            if (adornment == '`' && valid_symbols[T_LITERAL]) {
                return parse_inner_inline_markup(scanner, IM_LITERAL);
            }
            if (adornment == '|' && valid_symbols[T_SUBSTITUTION_REFERENCE]) {
                return parse_inner_inline_markup(scanner, IM_SUBSTITUTION_REFERENCE);
            }
            if (adornment == '[' && (valid_symbols[T_FOOTNOTE_REFERENCE] || valid_symbols[T_CITATION_REFERENCE])) {
                return parse_inner_inline_markup(scanner, IM_FOOTNOTE_REFERENCE | IM_CITATION_REFERENCE);
            }
        }
    }
    return false;
}

bool parse_char_bullet(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (!is_char_bullet(scanner->lookahead) || !valid_symbols[T_CHAR_BULLET]) {
        return false;
    }

    int32_t bullet = scanner->lookahead;
    scanner->advance(scanner);
    return parse_inner_char_bullet(scanner, bullet);
}

/// Parse the rest of a bullet list item, or an option list item, after its
/// first character, `bullet`, has been consumed.
static bool parse_inner_char_bullet(RSTScanner *scanner, int32_t bullet)
{
    const bool *valid_symbols = scanner->valid_symbols;

    bool ok = parse_inner_list_element(scanner, 1, T_CHAR_BULLET);

    if (ok) {
        return true;
    }

    // Not a bullet (no space followed the mark). A '-' or '+' may instead start
    // an option list item, e.g. "-a  description" or "--long  description". This
    // lets option lists be recognized inside block bodies, where sections and
    // transitions (and hence the adornment fallback) are not valid.
    if ((bullet == '-' || bullet == '+') && valid_symbols[T_OPTION_MARK]) {
        int prefix_length = 1;
        if (bullet == '-' && scanner->lookahead == '-') {
            scanner->advance(scanner);
            prefix_length = 2;
        }
        if (parse_option_mark(scanner, bullet, prefix_length)) {
            return true;
        }
    }

    return parse_text(scanner, true);
}

static bool parse_numeric_bullet(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (!valid_symbols[T_NUMERIC_BULLET]) {
        return false;
    }

    bool parenthesized = false;
    if (scanner->lookahead == '(') {
        scanner->advance(scanner);
        parenthesized = true;
    }

    if (is_numeric_bullet(scanner->lookahead)) {
        return parse_inner_numeric_bullet(scanner, parenthesized);
    }

    return false;
}

/// Match one decimal place of a roman numeral at `pos`, with `digits` its
/// one, five and ten: a five and up to three ones (`viii`), or up to three
/// ones, or a single one with a five or ten after it (`iv`, `ix`).
/// Returns the position after the match.
static size_t match_roman_place(const int32_t *text, size_t length, size_t pos, const int32_t digits[3])
{
    const int32_t one = digits[0];
    const int32_t five = digits[1];
    const int32_t ten = digits[2];
    int ones = 0;
    bool has_five = false;

    if (pos < length && text[pos] == five) {
        has_five = true;
        pos++;
    }
    while (ones < 3 && pos < length && text[pos] == one) {
        pos++;
        ones++;
    }
    if (!has_five && ones == 1 && pos < length && (text[pos] == five || text[pos] == ten)) {
        pos++;
    }
    return pos;
}

/// Whether the `length` characters of `text` are a roman numeral written as
/// docutils reads one, all in lower or all in upper case (`iv`, `XII`), and
/// not a word made of the same letters (`did`, `civil`).
static bool is_roman_numeral(const int32_t *text, size_t length)
{
    static const int32_t lower[3][3] = { { 'c', 'd', 'm' }, { 'x', 'l', 'c' }, { 'i', 'v', 'x' } };
    static const int32_t upper[3][3] = { { 'C', 'D', 'M' }, { 'X', 'L', 'C' }, { 'I', 'V', 'X' } };

    if (length == 0 || length > ROMAN_NUMERAL_MAX_LENGTH) {
        return false;
    }
    const bool is_upper = is_numeric_bullet_roman_upper(text[0]);
    const int32_t (*digits)[3] = is_upper ? upper : lower;
    const int32_t thousand = is_upper ? 'M' : 'm';
    size_t pos = 0;
    while (pos < length && pos < 4 && text[pos] == thousand) {
        pos++;
    }
    pos = match_roman_place(text, length, pos, digits[0]);
    pos = match_roman_place(text, length, pos, digits[1]);
    pos = match_roman_place(text, length, pos, digits[2]);
    return pos == length;
}

/// Whether the rest of the line starts with an enumerator: `1.`, `(a)`,
/// `#)` and the like, followed by whitespace. The enumerator is a number,
/// a single letter, a roman numeral or `#`, so a word ending a sentence
/// (`application.`) is not one. Consumes what it reads.
static bool starts_with_enumerator(RSTScanner *scanner)
{
    if (scanner->lookahead == '(') {
        scanner->advance(scanner);
    }
    int32_t text[ROMAN_NUMERAL_MAX_LENGTH];
    int length = 0;
    bool arabic = true;
    while (is_alphanumeric(scanner->lookahead) || scanner->lookahead == '#') {
        arabic = arabic && is_number(scanner->lookahead);
        if (length < ROMAN_NUMERAL_MAX_LENGTH) {
            text[length] = scanner->lookahead;
        }
        scanner->advance(scanner);
        length++;
    }
    if (length == 0 || (length > 1 && !arabic && !is_roman_numeral(text, (size_t)length))) {
        return false;
    }
    if (scanner->lookahead != '.' && scanner->lookahead != ')') {
        return false;
    }
    scanner->advance(scanner);
    return is_space(scanner->lookahead);
}

static bool parse_inner_numeric_bullet(RSTScanner *scanner, bool parenthesized)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!is_numeric_bullet(scanner->lookahead) || !valid_symbols[T_NUMERIC_BULLET]) {
        return false;
    }

    scanner->advance(scanner);
    int consumed_chars = 1;

    if (is_numeric_bullet_simple(scanner->previous)) {
        while (is_numeric_bullet_simple(scanner->lookahead) && scanner->lookahead != '#') {
            scanner->advance(scanner);
            consumed_chars++;
        }
    }
    else if (is_numeric_bullet_abc_lower(scanner->previous)) {
        if (is_numeric_bullet_roman_lower(scanner->previous)) {
            while (is_numeric_bullet_roman_lower(scanner->lookahead)) {
                scanner->advance(scanner);
                consumed_chars++;
            }
        }
    }
    else if (is_numeric_bullet_abc_upper(scanner->previous)) {
        if (is_numeric_bullet_roman_upper(scanner->previous)) {
            while (is_numeric_bullet_roman_upper(scanner->lookahead)) {
                scanner->advance(scanner);
                consumed_chars++;
            }
        }
    }
    else {
        return false;
    }

    if ((parenthesized && scanner->lookahead == ')')
        || (!parenthesized && (scanner->lookahead == '.' || scanner->lookahead == ')'))) {
        scanner->advance(scanner);
        consumed_chars++;
        consumed_chars += parenthesized ? 1 : 0;
        bool ok = parse_inner_list_element(scanner, consumed_chars, T_NUMERIC_BULLET);
        if (ok) {
            return true;
        }
    }
    else {
        if (is_abc(scanner->lookahead) && valid_symbols[T_STANDALONE_HYPERLINK]) {
            return parse_inner_standalone_hyperlink(scanner);
        }
        if (is_alphanumeric(scanner->lookahead) && valid_symbols[T_REFERENCE]) {
            return parse_reference(scanner);
        }
        if (valid_symbols[T_TEXT]) {
            lexer->mark_end(lexer);
            lexer->result_symbol = T_TEXT;
            return true;
        }
        return false;
    }
    return parse_text(scanner, true);
}

static bool parse_explict_markup_start(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (scanner->lookahead != '.' || !valid_symbols[T_EXPLICIT_MARKUP_START]) {
        return false;
    }

    scanner->advance(scanner);

    if (scanner->lookahead != '.') {
        return false;
    }

    scanner->advance(scanner);

    bool ok = parse_inner_list_element(scanner, 2, T_EXPLICIT_MARKUP_START);
    if (ok) {
        return true;
    }

    return false;
}


/// The indent of an explicit markup body: its least indented line, as in
/// docutils, so a first line indented deeper than the lines after it does
/// not end the body at the next line. The scanner is on the first body
/// line, after its indent, which is `indent`; it reads ahead to the first
/// line indented no deeper than the marker's block. A first line that is
/// not indented past the block leaves the body empty: its indent is one
/// past the block's.
static int explicit_markup_body_indent(RSTScanner *scanner, int indent)
{
    if (indent <= scanner->back(scanner)) {
        return scanner->back(scanner) + 1;
    }
    while (scanner->lookahead != CHAR_EOF) {
        while (!is_newline(scanner->lookahead)) {
            scanner->advance(scanner);
        }
        if (scanner->lookahead == CHAR_EOF) {
            break;
        }
        scanner->advance(scanner);
        const int line_indent = get_indent_level(scanner);
        if (is_newline(scanner->lookahead)) {
            continue;
        }
        if (line_indent <= scanner->back(scanner)) {
            break;
        }
        if (line_indent < indent) {
            indent = line_indent;
        }
    }
    return indent;
}
/// The length of the run of one adornment character that makes up the rest
/// of the line: 0 if the line doesn't start with an adornment character, -1
/// if another character follows the run. Consumes what it reads.
static int adornment_run_length(RSTScanner *scanner)
{
    const int32_t adornment = scanner->lookahead;

    if (!is_adornment_char(adornment)) {
        return 0;
    }
    int length = 0;
    while (!is_newline(scanner->lookahead)) {
        if (scanner->lookahead != adornment) {
            return -1;
        }
        length++;
        scanner->advance(scanner);
    }
    return length;
}

/// Whether the enumerator just read, with the rest of its line `indent`
/// columns in, starts a list item. The lookahead is after the enumerator.
///
/// As in docutils' `is_enumerated_list_item`, it does if the next line is
/// blank, indented past the item's block, outside that block, or starts with
/// another enumerator (docutils wants the next ordinal; any enumerator is
/// accepted here). Otherwise the enumerator is paragraph text ("1. text\ntext").
/// It is also text when the next line underlines the line as a title
/// (https://github.com/stsewd/tree-sitter-rst/issues/30): a run of one
/// adornment character at least as long as the line.
static bool enumerator_starts_item(RSTScanner *scanner, int indent)
{
    int line_length = indent;

    while (!is_newline(scanner->lookahead)) {
        line_length++;
        scanner->advance(scanner);
    }
    scanner->advance(scanner);

    const int32_t first = scanner->lookahead;
    bool at_block_column = false;
    bool indented = false;
    if (!is_newline(first)) {
        const int next_indent = get_indent_level(scanner);
        at_block_column = !is_newline(scanner->lookahead) && next_indent == scanner->back(scanner);
        // `(` and `#` start enumerators, never underlines.
        const bool may_be_underline = next_indent == 0 && is_adornment_char(first)
                                      && first != '(' && first != '#';
        if (at_block_column && !may_be_underline && !starts_with_enumerator(scanner)) {
            return false;
        }
        indented = next_indent > 0;
    }
    if (indented) {
        return true;
    }

    // Not a title's underline, and not a line at the block's column that
    // starts with punctuation but isn't an underline (it continues the
    // paragraph).
    const int underline = adornment_run_length(scanner);
    return !(underline > 0 && underline >= line_length) && !(at_block_column && underline < 0);
}

/// The body indent of an explicit markup block whose marker was just read,
/// with the rest of its line `indent` columns in, or -1 for an empty comment.
///
/// A marker alone on its line followed by a blank line is an empty comment,
/// which doesn't consume any following indented text. Otherwise the body's
/// indent is that of its least indented line.
static int explicit_markup_indent(RSTScanner *scanner, int indent)
{
    if (is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
        const int line_indent = get_indent_level(scanner);
        if (is_newline(scanner->lookahead)) {
            return scanner->valid_symbols[T_EMPTY_COMMENT] ? -1 : indent;
        }
        return explicit_markup_body_indent(scanner, line_indent);
    }

    // Go to the next line.
    while (!is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }
    scanner->advance(scanner);

    // The first non-empty line after the marker starts the body.
    while (true) {
        indent = get_indent_level(scanner);
        if (!is_newline(scanner->lookahead) || scanner->lookahead == CHAR_EOF) {
            break;
        }
        scanner->advance(scanner);
    }
    return explicit_markup_body_indent(scanner, indent);
}

/// The body indent of a list item whose bullet or enumerator is alone on its
/// line, `indent` being the column after the marker, or -1 when an
/// enumerator is paragraph text. The lookahead is at the end of the line.
///
/// As in docutils, the body starts on the next line, and its indent is that
/// of its least indented line, which may be less than the marker's width
/// (`(10)` over a body indented by three). A next line not indented past the
/// item's block leaves the body empty; before it, an enumerator is text
/// unless that line is blank or starts with another enumerator.
static int alone_marker_indent(RSTScanner *scanner, int indent, bool enumerator)
{
    scanner->advance(scanner);
    const int line_indent = get_indent_level(scanner);
    const bool blank = is_newline(scanner->lookahead);
    const int back = scanner->back(scanner);
    if (!blank && line_indent > back) {
        return explicit_markup_body_indent(scanner, line_indent);
    }
    const bool is_text = enumerator && !blank && line_indent == back
                         && !starts_with_enumerator(scanner);
    return is_text ? -1 : indent;
}

static bool parse_inner_list_element(RSTScanner *scanner, int consumed_chars, enum TokenType token_type)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!valid_symbols[token_type]) {
        return false;
    }

    if (is_space(scanner->lookahead)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = token_type;

        // Set indent level to the first non-whitespace char
        int indent = scanner->back(scanner) + consumed_chars + get_indent_level(scanner);

        if (token_type == T_EXPLICIT_MARKUP_START) {
            indent = explicit_markup_indent(scanner, indent);
            if (indent < 0) {
                lexer->result_symbol = T_EMPTY_COMMENT;
                return true;
            }
        }
        // A bullet or enumerator alone on its line.
        const bool alone = (token_type == T_CHAR_BULLET || token_type == T_NUMERIC_BULLET)
                           && is_newline(scanner->lookahead)
                           && scanner->lookahead != CHAR_EOF;
        if (alone) {
            indent = alone_marker_indent(scanner, indent, token_type == T_NUMERIC_BULLET);
        }
        if (!alone && token_type == T_NUMERIC_BULLET && !enumerator_starts_item(scanner, indent)) {
            indent = -1;
        }
        if (indent < 0) {
            return parse_text(scanner, false);
        }

        scanner->push(scanner, indent);
        return true;
    }

    return false;
}

/// Parse the ` : ` separator between a definition list term and a classifier.
///
/// Called from `parse_indent` with the whitespace before the `:` consumed.
/// A term is a single line followed directly by its indented definition, so
/// the separator is only emitted when the next line is indented past the
/// current level. Otherwise ` : ` is ordinary text (e.g. `Foo : bar`).
static bool parse_classifier_mark(RSTScanner *scanner)
{
    TSLexer *lexer = scanner->lexer;

    scanner->advance(scanner);
    if (!is_space(scanner->lookahead) || is_newline(scanner->lookahead)) {
        return false;
    }
    get_indent_level(scanner);
    lexer->mark_end(lexer);

    // The classifier itself can't be empty.
    if (is_newline(scanner->lookahead)) {
        return false;
    }

    // Go to the next line.
    while (!is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }
    if (scanner->lookahead == CHAR_EOF) {
        return false;
    }
    scanner->advance(scanner);

    int indent = get_indent_level(scanner);
    if (is_newline(scanner->lookahead) || indent <= scanner->back(scanner)) {
        return false;
    }

    lexer->result_symbol = T_CLASSIFIER_MARK;
    return true;
}

static bool parse_field_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != ':' || !valid_symbols[T_FIELD_MARK]) {
        return false;
    }

    scanner->advance(scanner);
    lexer->mark_end(lexer);

    if (is_space(scanner->lookahead)) {
        return parse_text(scanner, true);
    }

    bool ok = parse_inner_field_mark(scanner);
    if (ok) {
        return true;
    }
    return parse_text(scanner, false);
}

static bool parse_inner_field_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!valid_symbols[T_FIELD_MARK]) {
        return false;
    }

    bool is_escaped = false;
    while (!is_newline(scanner->lookahead)) {
        if (scanner->lookahead == '/') {
            scanner->advance(scanner);
            is_escaped = true;
        }
        else {
            is_escaped = false;
        }

        if (scanner->lookahead == ':' && !is_space(scanner->previous) && !is_escaped) {
            scanner->advance(scanner);
            if (is_space(scanner->lookahead)) {
                break;
            }
        }

        scanner->advance(scanner);
    }

    if (scanner->previous == ':' && is_space(scanner->lookahead)) {
        lexer->result_symbol = T_FIELD_MARK;
        return true;
    }
    return false;
}

static bool parse_field_mark_end(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != ':' || !valid_symbols[T_FIELD_MARK_END]) {
        return false;
    }

    scanner->advance(scanner);
    lexer->mark_end(lexer);

    if (is_space(scanner->lookahead)) {
        // Consume all whitespaces.
        get_indent_level(scanner);
        // Go to the next line.
        while (!is_newline(scanner->lookahead)) {
            scanner->advance(scanner);
        }
        scanner->advance(scanner);

        // The first non-empty line after the field name marker
        // determines the indentation of the field body.
        int indent = 0;
        while (true) {
            indent = get_indent_level(scanner);
            if (!is_newline(scanner->lookahead) || scanner->lookahead == CHAR_EOF) {
                break;
            }
            scanner->advance(scanner);
        }
        if (indent > scanner->back(scanner)) {
            scanner->push(scanner, indent);
        }
        else {
            scanner->push(scanner, scanner->back(scanner) + 1);
        }

        lexer->result_symbol = T_FIELD_MARK_END;
        return true;
    }

    return parse_text(scanner, false);
}

/// Consume a single option name plus an optional `=argument`, assuming the
/// option prefix (`-`, `--`, `+`, `/`) has already been consumed.
///
/// Short options (`-`, `+`, `/`) take a single alphanumeric name character;
/// long options (`--`) take a run of alphanumerics, hyphens and underscores.
/// Returns false if no valid option name follows the prefix.
static bool parse_inner_option(RSTScanner *scanner, bool long_option, int *spaces)
{
    *spaces = 0;
    if (!is_alphanumeric(scanner->lookahead)) {
        return false;
    }
    scanner->advance(scanner);

    if (long_option) {
        while (is_alphanumeric(scanner->lookahead)
               || scanner->lookahead == '-'
               || scanner->lookahead == '_') {
            scanner->advance(scanner);
        }
    }

    // Optional `=argument`: consume up to the next whitespace or option separator.
    if (scanner->lookahead == '=') {
        scanner->advance(scanner);
        while (!is_space(scanner->lookahead) && scanner->lookahead != ',') {
            scanner->advance(scanner);
        }
        return true;
    }

    // Optional ` argument` after one space: a word (`FILE`) or `<anything>`.
    // A space not followed by an argument is counted in `spaces`, as part of
    // the separation before the description.
    if (scanner->lookahead == ' ') {
        scanner->advance(scanner);
        if (scanner->lookahead == '<') {
            scanner->advance(scanner);
            while (scanner->lookahead != '>') {
                if (is_newline(scanner->lookahead) || scanner->lookahead == '<') {
                    return false;
                }
                scanner->advance(scanner);
            }
            scanner->advance(scanner);
        }
        else if (is_abc(scanner->lookahead)) {
            while (is_alphanumeric(scanner->lookahead)
                   || scanner->lookahead == '-'
                   || scanner->lookahead == '_') {
                scanner->advance(scanner);
            }
        }
        else {
            *spaces = 1;
        }
    }

    return true;
}

/// Parse an option list marker, e.g. `-a`, `--long=file`, `/V` or `-f, --force`.
///
/// Called from `fallback_adornment` once the first option's prefix has been
/// consumed (`prefix`/`prefix_length`: `-`/`+`/`/` with length 1, or `--` with
/// length 2). The emitted `T_OPTION_MARK` token spans the whole option group;
/// the following description is parsed as a `body` at the computed indent.
///
/// The option group must be followed by either a newline (description on the
/// next, indented line) or at least two spaces (description on the same line).
/// A single space followed by text means this is an ordinary paragraph, not an
/// option list, so we bail out and let the caller fall back to text.
static bool parse_option_mark(RSTScanner *scanner, int32_t prefix, int prefix_length)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!valid_symbols[T_OPTION_MARK]) {
        return false;
    }

    bool long_option;
    if (prefix_length == 1 && (prefix == '-' || prefix == '+' || prefix == '/')) {
        long_option = false;
    }
    else if (prefix_length == 2 && prefix == '-') {
        long_option = true;
    }
    else {
        return false;
    }

    int spaces_before = 0;
    if (!parse_inner_option(scanner, long_option, &spaces_before)) {
        return false;
    }

    // Additional options in the same group, separated by `, `.
    while (spaces_before == 0 && scanner->lookahead == ',') {
        scanner->advance(scanner);
        if (scanner->lookahead != ' ') {
            return false;
        }
        scanner->advance(scanner);

        if (scanner->lookahead != '-' && scanner->lookahead != '+' && scanner->lookahead != '/') {
            return false;
        }
        int32_t next_prefix = scanner->lookahead;
        scanner->advance(scanner);
        bool next_long = false;
        if (next_prefix == '-' && scanner->lookahead == '-') {
            scanner->advance(scanner);
            next_long = true;
        }
        if (!parse_inner_option(scanner, next_long, &spaces_before)) {
            return false;
        }
    }

    // The option group must be followed by whitespace.
    if (spaces_before == 0 && !is_space(scanner->lookahead)) {
        return false;
    }
    lexer->mark_end(lexer);

    int spaces = spaces_before + get_indent_level(scanner);
    bool same_line = !is_newline(scanner->lookahead);
    if (same_line) {
        // Same-line description: require at least two spaces of separation,
        // otherwise this is an ordinary paragraph, not an option list.
        if (spaces < 2) {
            return false;
        }
        // Go to the next line.
        while (!is_newline(scanner->lookahead)) {
            scanner->advance(scanner);
        }
    }
    scanner->advance(scanner);

    // The first non-blank line after the option line determines the
    // indentation of the description, mirroring field bodies: continuation
    // lines only need to be indented relative to the option.
    int indent = 0;
    while (true) {
        indent = get_indent_level(scanner);
        if (!is_newline(scanner->lookahead) || scanner->lookahead == CHAR_EOF) {
            break;
        }
        scanner->advance(scanner);
    }
    if (indent > scanner->back(scanner)) {
        scanner->push(scanner, indent);
    }
    else if (same_line) {
        scanner->push(scanner, scanner->back(scanner) + 1);
    }
    else {
        // An option without a description is ordinary text. The lookahead has
        // moved past the option, so end the text token at the recorded mark.
        if (!valid_symbols[T_TEXT]) {
            return false;
        }
        lexer->result_symbol = T_TEXT;
        return true;
    }
    lexer->result_symbol = T_OPTION_MARK;
    return true;
}

static bool parse_label(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != '['
        || !(valid_symbols[T_FOOTNOTE_LABEL] || valid_symbols[T_CITATION_LABEL])) {
        return false;
    }
    scanner->advance(scanner);
    int type = parse_inner_label_name(scanner);
    if ((type == IM_CITATION_REFERENCE && valid_symbols[T_CITATION_LABEL])
        || (type == IM_FOOTNOTE_REFERENCE && valid_symbols[T_FOOTNOTE_LABEL])) {
        scanner->advance(scanner);
        if (is_space(scanner->lookahead)) {
            lexer->mark_end(lexer);
            if (type == IM_CITATION_REFERENCE) {
                lexer->result_symbol = T_CITATION_LABEL;
            }
            else if (type == IM_FOOTNOTE_REFERENCE) {
                lexer->result_symbol = T_FOOTNOTE_LABEL;
            }
            return true;
        }
    }

    return false;
}

static int parse_inner_label_name(RSTScanner *scanner)
{
    int type = -1;

    if (is_number(scanner->lookahead)) {
        while (is_number(scanner->lookahead)) {
            scanner->advance(scanner);
        }
        if (scanner->lookahead == ']') {
            type = IM_FOOTNOTE_REFERENCE;
        }
        else {
            if (parse_inner_alphanumeric_label(scanner)) {
                type = IM_CITATION_REFERENCE;
            }
        }
    }
    else if (scanner->lookahead == '*') {
        type = IM_FOOTNOTE_REFERENCE;
        scanner->advance(scanner);
    }
    else if (scanner->lookahead == '#') {
        scanner->advance(scanner);
        if (scanner->lookahead == ']') {
            type = IM_FOOTNOTE_REFERENCE;
        }
        else if (is_alphanumeric(scanner->lookahead)) {
            if (parse_inner_alphanumeric_label(scanner)) {
                type = IM_FOOTNOTE_REFERENCE;
            }
        }
    }
    else if (is_alphanumeric(scanner->lookahead)) {
        if (parse_inner_alphanumeric_label(scanner)) {
            type = IM_CITATION_REFERENCE;
        }
    }
    else {
        return -1;
    }

    if (scanner->lookahead == ']') {
        return type;
    }

    return -1;
}

static bool parse_inner_alphanumeric_label(RSTScanner *scanner)
{
    if (!(is_alphanumeric(scanner->lookahead)
          || is_internal_reference_char(scanner->lookahead))) {
        return false;
    }

    bool internal_symbol = false;
    while (is_alphanumeric(scanner->lookahead) || is_internal_reference_char(scanner->lookahead)) {
        if (is_internal_reference_char(scanner->lookahead)) {
            if (internal_symbol) {
                return false;
            }
            internal_symbol = true;
        }
        else {
            internal_symbol = false;
        }
        scanner->advance(scanner);
    }

    if (scanner->lookahead == ']') {
        return true;
    }

    return false;
}

static bool parse_target_name(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != '_' || !valid_symbols[T_TARGET_NAME]) {
        return false;
    }

    scanner->advance(scanner);

    if (scanner->lookahead == '_') {
        scanner->advance(scanner);
    }
    else if (scanner->lookahead == '`') {
        // Find ending "`:".
        while (true) {
            if (scanner->lookahead == '`') {
                scanner->advance(scanner);
                if (scanner->lookahead == ':') {
                    break;
                }
            }

            if (is_newline(scanner->lookahead)) {
                break;
            }

            scanner->advance(scanner);
        }
    }
    else {
        bool is_escaped = false;
        while (true) {
            if (scanner->lookahead == '\\') {
                scanner->advance(scanner);
                is_escaped = true;
            }
            else {
                is_escaped = false;
            }

            if (is_newline(scanner->lookahead)) {
                break;
            }

            if (scanner->lookahead == ':' && !is_escaped) {
                break;
            }
            scanner->advance(scanner);
        }
    }

    // check for ': '
    if (scanner->lookahead != ':') {
        return false;
    }

    scanner->advance(scanner);

    if (is_space(scanner->lookahead)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = T_TARGET_NAME;
        return true;
    }

    return false;
}

static bool parse_anonymous_target_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != '_' || !valid_symbols[T_ANONYMOUS_TARGET_MARK]) {
        return false;
    }

    scanner->advance(scanner);

    if (scanner->lookahead != '_') {
        return false;
    }

    scanner->advance(scanner);

    if (is_space(scanner->lookahead)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = T_ANONYMOUS_TARGET_MARK;
        return true;
    }

    return false;
}

static bool parse_directive_name(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!is_alphanumeric(scanner->lookahead) || !valid_symbols[T_DIRECTIVE_NAME]) {
        return false;
    }

    scanner->advance(scanner);

    bool internal_symbol = false;
    bool keep_parsing = true;
    while (is_alphanumeric(scanner->lookahead)
           || is_internal_reference_char(scanner->lookahead)
           || (is_space(scanner->lookahead) && !is_newline(scanner->lookahead))) {
        // Directive names can have one (and only one) space before `::`.
        if (is_space(scanner->lookahead)) {
            lexer->mark_end(lexer);
            scanner->advance(scanner);
            scanner->advance(scanner);
            keep_parsing = false;
            break;
        }
        if (is_internal_reference_char(scanner->lookahead)) {
            if (internal_symbol) {
                break;
            }
            internal_symbol = true;
            lexer->mark_end(lexer);
        }
        else {
            internal_symbol = false;
        }
        scanner->advance(scanner);
    }

    if (scanner->lookahead != ':' || scanner->previous != ':') {
        return parse_text(scanner, keep_parsing);
    }
    scanner->advance(scanner);

    if (is_space(scanner->lookahead)) {
        lexer->result_symbol = T_DIRECTIVE_NAME;
        return true;
    }

    return false;
}

static bool parse_substitution_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != '|' || !valid_symbols[T_SUBSTITUTION_MARK]) {
        return false;
    }

    scanner->advance(scanner);

    if (!is_space(scanner->lookahead)) {
        bool ok = parse_inner_inline_markup(scanner, IM_SUBSTITUTION_REFERENCE);
        if (ok
            && lexer->result_symbol == T_SUBSTITUTION_REFERENCE
            && is_space(scanner->lookahead)
            && !is_newline(scanner->lookahead)) {
            lexer->result_symbol = T_SUBSTITUTION_MARK;
            return true;
        }
    }
    return false;
}

static bool parse_literal_block_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (scanner->lookahead != ':'
        || !(valid_symbols[T_LITERAL_INDENTED_BLOCK_MARK] || valid_symbols[T_LITERAL_QUOTED_BLOCK_MARK])) {
        return false;
    }

    scanner->advance(scanner);

    if (scanner->lookahead != ':') {
        if (valid_symbols[T_ROLE_NAME_PREFIX] || valid_symbols[T_ROLE_NAME_SUFFIX]) {
            return parse_inner_role(scanner);
        }
        return false;
    }

    scanner->advance(scanner);

    return parse_innner_literal_block_mark(scanner);
}

static bool parse_innner_literal_block_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!(valid_symbols[T_LITERAL_INDENTED_BLOCK_MARK] || valid_symbols[T_LITERAL_QUOTED_BLOCK_MARK])) {
        return false;
    }

    // `::` followed by something other than whitespace (e.g. `("::")`) is text.
    if (!is_space(scanner->lookahead)) {
        return parse_text(scanner, false);
    }

    lexer->mark_end(lexer);

    // Consume all whitespaces.
    while (is_space(scanner->lookahead) && !is_newline(scanner->lookahead)) {
        scanner->advance(scanner);
    }

    if (!is_newline(scanner->lookahead)) {
        return parse_text(scanner, false);
    }

    scanner->advance(scanner);

    // Make sure there is blank line
    while (!is_newline(scanner->lookahead)) {
        if (!is_space(scanner->lookahead)) {
            return parse_literal_block_mark_as_text(scanner);
        }
        scanner->advance(scanner);
    }
    scanner->advance(scanner);

    // Skip all whitespaces and newlines and
    // get the indentation level of the first non-empty line.
    int indent = -1;
    while (scanner->lookahead != CHAR_EOF) {
        int local_indent = get_indent_level(scanner);
        if (!is_newline(scanner->lookahead)) {
            indent = local_indent;
            break;
        }
        scanner->advance(scanner);
    }

    // Literal blocks need to be indented, and quoted literal blocks
    // need to start with an adornment at the same indentation level.
    if (indent > scanner->back(scanner) && valid_symbols[T_LITERAL_INDENTED_BLOCK_MARK]) {
        scanner->push(scanner, scanner->back(scanner) + 1);
        lexer->result_symbol = T_LITERAL_INDENTED_BLOCK_MARK;
        return true;
    }
    if (indent == scanner->back(scanner)
        && is_adornment_char(scanner->lookahead)
        && valid_symbols[T_LITERAL_QUOTED_BLOCK_MARK]) {
        lexer->result_symbol = T_LITERAL_QUOTED_BLOCK_MARK;
        return true;
    }
    return parse_literal_block_mark_as_text(scanner);
}

/// Emit a `::` that doesn't start a literal block as text.
///
/// `mark_end` has been called right after the `::`, so the text token ends
/// there even though the lookahead has moved on.
static bool parse_literal_block_mark_as_text(RSTScanner *scanner)
{
    if (!scanner->valid_symbols[T_TEXT]) {
        return false;
    }
    scanner->lexer->result_symbol = T_TEXT;
    return true;
}

static bool parse_quoted_literal_block(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!is_adornment_char(scanner->lookahead) || !valid_symbols[T_QUOTED_LITERAL_BLOCK]) {
        return false;
    }

    int32_t adornment = scanner->lookahead;
    int current_indent = scanner->back(scanner);

    while (true) {
        while (!is_newline(scanner->lookahead)) {
            scanner->advance(scanner);
        }
        lexer->mark_end(lexer);

        scanner->advance(scanner);

        // Check if it's an empty line or if the indentation was broken.
        int indent = get_indent_level(scanner);
        if (indent != current_indent || scanner->lookahead != adornment) {
            break;
        }

        if (scanner->lookahead != adornment) {
            return parse_text(scanner, false);
        }
    }
    lexer->result_symbol = T_QUOTED_LITERAL_BLOCK;
    return true;
}

static bool parse_line_block_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (scanner->lookahead != '|' || !valid_symbols[T_LINE_BLOCK_MARK]) {
        return false;
    }

    scanner->advance(scanner);

    if (is_space(scanner->lookahead)) {
        return parse_inner_list_element(scanner, 1, T_LINE_BLOCK_MARK);
    }

    return false;
}

static bool parse_attribution_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (!is_attribution_mark(scanner->lookahead) || !valid_symbols[T_ATTRIBUTION_MARK]) {
        return false;
    }

    int consumed_chars = 0;
    if (scanner->lookahead == '-') {
        while (scanner->lookahead == '-') {
            consumed_chars++;
            scanner->advance(scanner);
        }

        if (consumed_chars >= 2 && consumed_chars <= 3
            && parse_inner_list_element(scanner, consumed_chars, T_ATTRIBUTION_MARK)) {
            return true;
        }

        // Not an attribution. A block quote may also start with a bullet list
        // ("- item") or an option list ("-a" or "--long").
        if (consumed_chars == 1 && valid_symbols[T_CHAR_BULLET]) {
            return parse_inner_char_bullet(scanner, '-');
        }
        if (consumed_chars <= 2 && parse_option_mark(scanner, '-', consumed_chars)) {
            return true;
        }
        return parse_text(scanner, true);
    }
    else {
        scanner->advance(scanner);
        consumed_chars++;
    }

    return parse_inner_list_element(scanner, consumed_chars, T_ATTRIBUTION_MARK);
}

static bool parse_doctest_block_mark(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != '>' || !valid_symbols[T_DOCTEST_BLOCK_MARK]) {
        return false;
    }

    int consumed_chars = 0;
    while (scanner->lookahead == '>') {
        consumed_chars++;
        scanner->advance(scanner);
    }

    if (consumed_chars == 3 && is_space(scanner->lookahead)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = T_DOCTEST_BLOCK_MARK;
        return true;
    }
    return false;
}

static bool parse_inline_markup(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    scanner->advance(scanner);
    lexer->mark_end(lexer);

    unsigned type = 0;

    if (scanner->previous == '*' && scanner->lookahead == '*' && valid_symbols[T_STRONG]) {
        type = IM_STRONG;
    }
    else if (scanner->previous == '*' && valid_symbols[T_EMPHASIS]) {
        type = IM_EMPHASIS;
    }
    else if (scanner->previous == '`' && scanner->lookahead == '`' && valid_symbols[T_LITERAL]) {
        type = IM_LITERAL;
    }
    else if (scanner->previous == '`' && (valid_symbols[T_INTERPRETED_TEXT] || valid_symbols[T_INTERPRETED_TEXT_PREFIX] || valid_symbols[T_REFERENCE])) {
        type = IM_INTERPRETED_TEXT | IM_INTERPRETED_TEXT_PREFIX | IM_REFERENCE;
    }
    else if (scanner->previous == '|' && valid_symbols[T_SUBSTITUTION_REFERENCE]) {
        type = IM_SUBSTITUTION_REFERENCE;
    }
    else if (scanner->previous == '_' && scanner->lookahead == '`' && valid_symbols[T_INLINE_TARGET]) {
        type = IM_INLINE_TARGET;
    }
    else if (scanner->previous == '[' && (valid_symbols[T_FOOTNOTE_REFERENCE] || valid_symbols[T_CITATION_REFERENCE])) {
        type = IM_FOOTNOTE_REFERENCE | IM_CITATION_REFERENCE;
    }

    // Skip one char for tokens that start with a double char
    if (type & (IM_STRONG | IM_LITERAL | IM_INLINE_TARGET)) {
        scanner->advance(scanner);
    }

    // The next character can't be a white space
    if (is_space(scanner->lookahead)) {
        if (type & IM_EMPHASIS) {
            bool ok = parse_inner_list_element(scanner, 1, T_CHAR_BULLET);
            if (ok) {
                return true;
            }
        }
        if (valid_symbols[T_TEXT]) {
            lexer->mark_end(lexer);
            lexer->result_symbol = T_TEXT;
            return true;
        }
        return false;
    }

    return parse_inner_inline_markup(scanner, type);
}

static bool parse_inner_inline_markup(RSTScanner *scanner, unsigned type)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    int consumed_chars = 0;
    bool word_found = false;
    bool is_escaped = false;

    if (type & IM_FOOTNOTE_REFERENCE || type & IM_CITATION_REFERENCE) {
        int final_type = parse_inner_label_name(scanner);
        if ((final_type == IM_FOOTNOTE_REFERENCE && type & IM_FOOTNOTE_REFERENCE)
            || (final_type == IM_CITATION_REFERENCE && type & IM_CITATION_REFERENCE)) {
            scanner->advance(scanner);
            if (scanner->lookahead == '_') {
                scanner->advance(scanner);
                if (is_space(scanner->lookahead) || is_end_char(scanner->lookahead)) {
                    lexer->mark_end(lexer);
                    if (final_type == IM_CITATION_REFERENCE) {
                        lexer->result_symbol = T_CITATION_REFERENCE;
                    }
                    else if (final_type == IM_FOOTNOTE_REFERENCE) {
                        lexer->result_symbol = T_FOOTNOTE_REFERENCE;
                    }
                    return true;
                }
            }
        }
        return parse_text(scanner, false);
    }

    while (scanner->lookahead != CHAR_EOF) {
        // Skip indentation
        if (is_newline(scanner->lookahead)) {
            if (!word_found) {
                word_found = true;
                lexer->mark_end(lexer);
            }
            scanner->advance(scanner);
            int indent = get_indent_level(scanner);
            if (indent != scanner->back(scanner) || is_newline(scanner->lookahead)) {
                break;
            }
        }

        // Skip escaped chars
        if (scanner->lookahead == '\\') {
            is_escaped = true;
            scanner->advance(scanner);
            if (is_newline(scanner->lookahead)) {
                break;
            }
        }
        else {
            is_escaped = false;
        }

        // Mark the end of the word if a space was found
        if (!word_found && is_space(scanner->lookahead)) {
            word_found = true;
            lexer->mark_end(lexer);
        }

        // Mark the end of the word if a start char was found
        if (!word_found && is_start_char(scanner->lookahead)) {
            word_found = true;
            lexer->mark_end(lexer);
        }

        // Check if it's a terminal character
        if (consumed_chars > 0 && !is_space(scanner->previous) && is_inline_markup_end_char(scanner->lookahead)
            // Literal is the only inline markup that doesn't care if the previous
            // char is '\'
            && (!is_escaped || (type & IM_LITERAL))) {
            scanner->advance(scanner);

            bool is_valid = true;
            bool advance = false;

            if ((type & IM_STRONG) && scanner->previous == '*' && scanner->lookahead == '*') {
                lexer->result_symbol = T_STRONG;
                while (scanner->lookahead == '*') {
                    scanner->advance(scanner);
                    consumed_chars++;
                }
            }
            else if ((type & IM_EMPHASIS) && scanner->previous == '*') {
                lexer->result_symbol = T_EMPHASIS;
            }
            else if ((type & IM_LITERAL) && scanner->previous == '`' && scanner->lookahead == '`') {
                lexer->result_symbol = T_LITERAL;
                while (scanner->lookahead == '`') {
                    scanner->advance(scanner);
                    consumed_chars++;
                }
            }
            else if ((type & IM_INLINE_TARGET) && scanner->previous == '`') {
                lexer->result_symbol = T_INLINE_TARGET;
            }
            else if ((type & IM_REFERENCE) && scanner->previous == '`' && scanner->lookahead == '_') {
                lexer->result_symbol = T_REFERENCE;

                // Check for annonymous references
                scanner->advance(scanner);
                consumed_chars++;
                if (scanner->lookahead == '_') {
                    advance = true;
                }
            }
            else if ((type & IM_INTERPRETED_TEXT || type & IM_INTERPRETED_TEXT_PREFIX) && scanner->previous == '`') {
                if (scanner->lookahead == ':' && type & IM_INTERPRETED_TEXT_PREFIX && valid_symbols[T_INTERPRETED_TEXT_PREFIX]) {
                    lexer->mark_end(lexer);
                    scanner->advance(scanner);
                    bool ok = parse_role_name(scanner);
                    if (ok) {
                        lexer->result_symbol = T_INTERPRETED_TEXT_PREFIX;
                        return true;
                    }
                    if (valid_symbols[T_INTERPRETED_TEXT]) {
                        lexer->result_symbol = T_INTERPRETED_TEXT;
                        return true;
                    }
                    is_valid = false;
                }
                else {
                    lexer->result_symbol = T_INTERPRETED_TEXT;
                }
            }
            else if ((type & IM_SUBSTITUTION_REFERENCE) && scanner->previous == '|') {
                lexer->result_symbol = T_SUBSTITUTION_REFERENCE;
                // Substitution references can end with '__'.
                if (scanner->lookahead == '_') {
                    scanner->advance(scanner);
                    if (scanner->lookahead == '_') {
                        advance = true;
                    }
                }
            }
            else {
                is_valid = false;
            }

            if (advance) {
                scanner->advance(scanner);
                consumed_chars++;
            }

            // The next char should be a whitespace or an end char
            if (is_valid && (is_space(scanner->lookahead) || is_end_char(scanner->lookahead))) {
                lexer->mark_end(lexer);
                return true;
            }
        }
        else {
            scanner->advance(scanner);
        }

        consumed_chars++;
    }

    if (!word_found && is_newline(scanner->lookahead)) {
        return parse_text(scanner, true);
    }
    return parse_text(scanner, false);
}

static bool parse_reference(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (is_space(scanner->lookahead) || is_internal_reference_char(scanner->lookahead) || !valid_symbols[T_REFERENCE]) {
        return false;
    }
    scanner->advance(scanner);
    return parse_inner_reference(scanner);
}

static bool parse_inner_reference(RSTScanner *scanner)
{
    TSLexer *lexer = scanner->lexer;

    bool internal_symbol = is_internal_reference_char(scanner->previous);
    bool is_word = false;

    while ((!is_space(scanner->lookahead) && !is_end_char(scanner->lookahead)) || is_internal_reference_char(scanner->lookahead)) {
        // Mark the end of the worl?d.
        if (is_start_char(scanner->lookahead) && !is_word) {
            is_word = true;
            lexer->mark_end(lexer);
        }
        if (is_internal_reference_char(scanner->lookahead)) {
            if (internal_symbol) {
                break;
            }
            internal_symbol = true;
        }
        else {
            internal_symbol = false;
        }
        scanner->advance(scanner);
    }

    // Only an anonymous reference can contain
    // and end with two consecutive '_'.
    if (scanner->lookahead == '_' && scanner->previous == '_') {
        scanner->advance(scanner);
    }

    if (scanner->previous == '_' && (is_space(scanner->lookahead) || is_end_char(scanner->lookahead))) {
        lexer->mark_end(lexer);
        lexer->result_symbol = T_REFERENCE;
        return true;
    }

    return parse_text(scanner, !is_word);
}

static bool parse_standalone_hyperlink(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;

    if (!is_abc(scanner->lookahead) || !valid_symbols[T_STANDALONE_HYPERLINK]) {
        return false;
    }
    scanner->advance(scanner);
    return parse_inner_standalone_hyperlink(scanner);
}

static bool parse_inner_standalone_hyperlink(RSTScanner *scanner)
{
    TSLexer *lexer = scanner->lexer;

    const unsigned MAX_SCHEMA_LEN = 20;
    char *schema = malloc(sizeof(char) * MAX_SCHEMA_LEN);
    unsigned consumed_chars = 0;

    // TODO: cast this more safely
    schema[consumed_chars++] = (char)scanner->previous;
    while (consumed_chars < MAX_SCHEMA_LEN) {
        if (!is_alphanumeric(scanner->lookahead)) {
            break;
        }
        // TODO: cast this more safely
        schema[consumed_chars++] = (char)scanner->lookahead;
        scanner->advance(scanner);
    }

    bool is_word = false;
    if (is_start_char(scanner->lookahead)) {
        lexer->mark_end(lexer);
    }

    bool is_valid = false;
    if (scanner->lookahead == ':') {
        is_valid = is_known_schema(schema, consumed_chars);
    }
    else if (scanner->lookahead == '@') {
        is_valid = true;
    }

    // Clean up
    free(schema);
    schema = NULL;

    if (!is_valid) {
        if ((!is_space(scanner->lookahead) && !is_end_char(scanner->lookahead)) || is_internal_reference_char(scanner->lookahead)) {
            return parse_inner_reference(scanner);
        }

        return parse_text(scanner, !is_word);
    }

    scanner->advance(scanner);

    if (scanner->lookahead == '/') {
        scanner->advance(scanner);
    }
    else if (!is_alphanumeric(scanner->lookahead)) {
        return parse_text(scanner, !is_word);
    }

    consumed_chars = 0;
    bool is_escaped = false;
    while (true) {
        lexer->mark_end(lexer);
        if (scanner->lookahead == '\\') {
            scanner->advance(scanner);
            is_escaped = true;
        }
        else {
            is_escaped = false;
        }
        if (is_invalid_uri_char(scanner->lookahead)) {
            break;
        }
        if (is_space(scanner->lookahead)
            || (is_end_char(scanner->lookahead) && !is_escaped && scanner->lookahead != '/')) {
            if (is_end_char(scanner->lookahead)) {
                lexer->mark_end(lexer);
                scanner->advance(scanner);
                if (!is_alphanumeric(scanner->lookahead)) {
                    lexer->result_symbol = T_STANDALONE_HYPERLINK;
                    return true;
                }
            }
            else {
                break;
            }
        }
        scanner->advance(scanner);
        consumed_chars++;
    }

    if (consumed_chars > 0) {
        lexer->result_symbol = T_STANDALONE_HYPERLINK;
        return true;
    }

    return parse_text(scanner, !is_word);
}

static bool parse_role(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (scanner->lookahead != ':'
        || (!valid_symbols[T_ROLE_NAME_SUFFIX] && !valid_symbols[T_ROLE_NAME_PREFIX])) {
        return false;
    }

    scanner->advance(scanner);

    // Mark the end of the word before going deeper.
    lexer->mark_end(lexer);

    if (is_space(scanner->lookahead) && valid_symbols[T_FIELD_MARK_END]) {
        // Consume all whitespaces.
        get_indent_level(scanner);
        lexer->mark_end(lexer);
        // Go to the next line
        while (!is_newline(scanner->lookahead)) {
            scanner->advance(scanner);
        }
        scanner->advance(scanner);

        // The first non-empty line after the field name marker
        // determines the indentation of the field body.
        int indent = 0;
        while (true) {
            indent = get_indent_level(scanner);
            if (!is_newline(scanner->lookahead) || scanner->lookahead == CHAR_EOF) {
                break;
            }
            scanner->advance(scanner);
        }
        if (indent > scanner->back(scanner)) {
            scanner->push(scanner, indent);
        }
        else {
            scanner->push(scanner, scanner->back(scanner) + 1);
        }

        lexer->result_symbol = T_FIELD_MARK_END;
        return true;
    }

    if (is_alphanumeric(scanner->lookahead)) {
        bool ok = parse_inner_role(scanner);
        if (ok) {
            return true;
        }
    }

    return parse_text(scanner, false);
}

static bool parse_inner_role(RSTScanner *scanner)
{
    const bool *valid_symbols = scanner->valid_symbols;
    TSLexer *lexer = scanner->lexer;

    if (!is_alphanumeric(scanner->lookahead)
        || (!valid_symbols[T_ROLE_NAME_SUFFIX] && !valid_symbols[T_ROLE_NAME_PREFIX])) {
        return false;
    }
    // Mark the end at the previous character (`:`),
    // so that the role name is not included in the T_FIELD_MARK token.
    lexer->mark_end(lexer);
    bool ok = parse_role_name(scanner);
    if (ok) {
        if (scanner->lookahead == '`' && valid_symbols[T_ROLE_NAME_PREFIX]) {
            lexer->mark_end(lexer);
            lexer->result_symbol = T_ROLE_NAME_PREFIX;
            return true;
        }

        if (is_space(scanner->lookahead) && valid_symbols[T_FIELD_MARK]) {
            lexer->result_symbol = T_FIELD_MARK;
            return true;
        }

        if ((is_space(scanner->lookahead) || is_end_char(scanner->lookahead))
            && valid_symbols[T_ROLE_NAME_SUFFIX]) {
            lexer->mark_end(lexer);
            lexer->result_symbol = T_ROLE_NAME_SUFFIX;
            return true;
        }
    }
    if (valid_symbols[T_FIELD_MARK]) {
        ok = parse_inner_field_mark(scanner);
        if (ok) {
            return true;
        }
    }
    return false;
}

/// This function assumes the previous token was `:` and it's already consumed.
static bool parse_role_name(RSTScanner *scanner)
{
    if (!is_alphanumeric(scanner->lookahead)) {
        return false;
    }

    bool internal_symbol = true;
    while (is_alphanumeric(scanner->lookahead) || is_internal_reference_char(scanner->lookahead)) {
        if (is_internal_reference_char(scanner->lookahead)) {
            if (internal_symbol) {
                return false;
            }
            internal_symbol = true;
        }
        else {
            internal_symbol = false;
        }
        scanner->advance(scanner);
    }

    if (scanner->previous == ':') {
        return true;
    }

    return false;
}

/// Parse normal text.
///
/// Text nodes are separated by white spaces or a start char like `(`
static bool parse_text(RSTScanner *scanner, bool mark_end)
{
    TSLexer *lexer = scanner->lexer;
    const bool *valid_symbols = scanner->valid_symbols;

    if (!valid_symbols[T_TEXT]) {
        return false;
    }

    if (is_start_char(scanner->lookahead)) {
        scanner->advance(scanner);
    }
    else {
        while (!is_space(scanner->lookahead)) {
            if (is_start_char(scanner->lookahead)) {
                break;
            }
            scanner->advance(scanner);
        }
    }

    if (mark_end) {
        lexer->mark_end(lexer);
    }
    lexer->result_symbol = T_TEXT;
    return true;
}
