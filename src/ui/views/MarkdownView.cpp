#include "ui/views/MarkdownView.hpp"

#include "core/StringEncoding.hpp"
#include "ui/common/Localization.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <md4c.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {
namespace {

enum InlineStyle : unsigned {
    InlineNone = 0,
    InlineEmphasis = 1u << 0,
    InlineStrong = 1u << 1,
    InlineCode = 1u << 2,
    InlineStrike = 1u << 3,
    InlineLink = 1u << 4,
    InlineImage = 1u << 5,
    InlineMath = 1u << 6,
    InlineWiki = 1u << 7,
    InlineMathDisplay = 1u << 8
};

bool hasStyle(unsigned value, InlineStyle style)
{
    return (value & style) != 0;
}

enum class MarkdownBlockKind {
    Paragraph,
    Heading,
    Quote,
    ListItem,
    Code,
    Html,
    Table,
    HorizontalRule
};

struct InlineRun {
    std::string text;
    unsigned style = InlineNone;
    std::string href;
};

struct MarkdownCell {
    std::vector<InlineRun> runs;
    std::string text;
    MD_ALIGN align = MD_ALIGN_DEFAULT;
    bool header = false;
};

struct MarkdownRow {
    std::vector<MarkdownCell> cells;
    bool header = false;
};

struct MarkdownBlock {
    MarkdownBlockKind kind = MarkdownBlockKind::Paragraph;
    std::string text;
    std::string codeLanguage;
    std::vector<InlineRun> runs;
    std::vector<MarkdownRow> rows;
    unsigned headingLevel = 1;
    unsigned tableColumnCount = 0;
    int quoteDepth = 0;
    int listDepth = 0;
    bool ordered = false;
    unsigned itemNumber = 0;
    bool task = false;
    bool taskChecked = false;
};

struct ListState {
    bool ordered = false;
    unsigned nextNumber = 1;
};

struct SpanState {
    unsigned style = InlineNone;
    std::string href;
};

struct MarkdownParseState {
    std::vector<MarkdownBlock> blocks;
    MarkdownBlock current;
    bool currentActive = false;
    int quoteDepth = 0;
    std::vector<ListState> lists;
    std::vector<SpanState> spans;
    bool tableActive = false;
    MarkdownBlock tableBlock;
    bool rowActive = false;
    MarkdownRow currentRow;
    bool cellActive = false;
    MarkdownCell currentCell;
};

std::string_view textView(const MD_CHAR* text, MD_SIZE size)
{
    return {reinterpret_cast<const char*>(text), static_cast<size_t>(size)};
}

std::string attributeText(const MD_ATTRIBUTE& attribute)
{
    if (!attribute.text || attribute.size == 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(attribute.text), static_cast<size_t>(attribute.size));
}

void appendUtf8(std::string& output, std::uint32_t codepoint)
{
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string decodeEntity(std::string_view entity)
{
    if (entity == "&amp;") return "&";
    if (entity == "&lt;") return "<";
    if (entity == "&gt;") return ">";
    if (entity == "&quot;") return "\"";
    if (entity == "&apos;") return "'";
    if (entity == "&nbsp;") return " ";
    if (entity == "&copy;") return "(c)";
    if (entity == "&reg;") return "(r)";
    if (entity == "&trade;") return "TM";
    if (entity == "&mdash;") return "--";
    if (entity == "&ndash;") return "-";
    if (entity.size() > 3 && entity[0] == '&' && entity[1] == '#') {
        const bool hex = entity.size() > 4 && (entity[2] == 'x' || entity[2] == 'X');
        const size_t start = hex ? 3 : 2;
        const size_t end = entity.back() == ';' ? entity.size() - 1 : entity.size();
        std::string digits(entity.substr(start, end - start));
        char* parseEnd = nullptr;
        const unsigned long value = std::strtoul(digits.c_str(), &parseEnd, hex ? 16 : 10);
        if (parseEnd && *parseEnd == '\0') {
            std::string decoded;
            appendUtf8(decoded, static_cast<std::uint32_t>(value));
            if (!decoded.empty()) {
                return decoded;
            }
        }
    }
    return std::string(entity);
}

void trimTrailingWhitespace(std::string& text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }
}

void flushCurrent(MarkdownParseState& state)
{
    if (!state.currentActive) {
        return;
    }
    trimTrailingWhitespace(state.current.text);
    if (!state.current.text.empty() || state.current.kind == MarkdownBlockKind::HorizontalRule) {
        state.blocks.push_back(std::move(state.current));
    }
    state.current = {};
    state.currentActive = false;
}

void startBlock(MarkdownParseState& state, MarkdownBlockKind kind)
{
    flushCurrent(state);
    state.current = {};
    state.current.kind = kind;
    state.current.quoteDepth = state.quoteDepth;
    state.currentActive = true;
}

unsigned currentInlineStyle(const MarkdownParseState& state)
{
    unsigned style = InlineNone;
    for (const SpanState& span : state.spans) {
        style |= span.style;
    }
    return style;
}

std::string currentHref(const MarkdownParseState& state)
{
    for (auto it = state.spans.rbegin(); it != state.spans.rend(); ++it) {
        if (!it->href.empty()) {
            return it->href;
        }
    }
    return {};
}

void appendRun(std::vector<InlineRun>& runs, std::string_view text, unsigned style, const std::string& href)
{
    if (text.empty()) {
        return;
    }
    if (!runs.empty() && runs.back().style == style && runs.back().href == href) {
        runs.back().text.append(text.data(), text.size());
        return;
    }
    InlineRun run;
    run.text.assign(text.data(), text.size());
    run.style = style;
    run.href = href;
    runs.push_back(std::move(run));
}

void appendText(MarkdownParseState& state, std::string_view text)
{
    if (text.empty()) {
        return;
    }

    const unsigned style = currentInlineStyle(state);
    const std::string href = currentHref(state);
    if (state.cellActive) {
        state.currentCell.text.append(text.data(), text.size());
        appendRun(state.currentCell.runs, text, style, href);
        return;
    }

    if (!state.currentActive) {
        startBlock(state, state.quoteDepth > 0 ? MarkdownBlockKind::Quote : MarkdownBlockKind::Paragraph);
    }
    state.current.text.append(text.data(), text.size());
    appendRun(state.current.runs, text, style, href);
}

int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto& state = *static_cast<MarkdownParseState*>(userdata);
    if (type == MD_BLOCK_QUOTE) {
        ++state.quoteDepth;
    } else if (type == MD_BLOCK_UL) {
        state.lists.push_back(ListState{false, 1});
    } else if (type == MD_BLOCK_OL) {
        const auto* ol = static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
        state.lists.push_back(ListState{true, ol ? ol->start : 1});
    } else if (type == MD_BLOCK_H) {
        const auto* heading = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
        startBlock(state, MarkdownBlockKind::Heading);
        state.current.headingLevel = heading ? std::clamp(heading->level, 1u, 6u) : 1u;
    } else if (type == MD_BLOCK_P) {
        if (state.tableActive || state.cellActive) {
            return 0;
        }
        if (state.currentActive && state.current.kind == MarkdownBlockKind::ListItem) {
            if (!state.current.text.empty()) {
                appendText(state, "\n\n");
            }
            return 0;
        }
        startBlock(state, state.quoteDepth > 0 ? MarkdownBlockKind::Quote : MarkdownBlockKind::Paragraph);
    } else if (type == MD_BLOCK_CODE) {
        const auto* code = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
        startBlock(state, MarkdownBlockKind::Code);
        if (code) {
            std::string info = attributeText(code->info);
            const size_t space = info.find_first_of(" \t\r\n");
            if (space != std::string::npos) {
                info.resize(space);
            }
            state.current.codeLanguage = info;
        }
    } else if (type == MD_BLOCK_HTML) {
        startBlock(state, MarkdownBlockKind::Html);
    } else if (type == MD_BLOCK_LI) {
        const auto* item = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
        startBlock(state, MarkdownBlockKind::ListItem);
        state.current.listDepth = static_cast<int>(state.lists.size());
        if (!state.lists.empty()) {
            ListState& list = state.lists.back();
            state.current.ordered = list.ordered;
            state.current.itemNumber = list.ordered ? list.nextNumber++ : 0;
        }
        if (item && item->is_task) {
            state.current.task = true;
            state.current.taskChecked = item->task_mark == 'x' || item->task_mark == 'X';
        }
    } else if (type == MD_BLOCK_HR) {
        startBlock(state, MarkdownBlockKind::HorizontalRule);
        flushCurrent(state);
    } else if (type == MD_BLOCK_TABLE) {
        const auto* table = static_cast<const MD_BLOCK_TABLE_DETAIL*>(detail);
        flushCurrent(state);
        state.tableActive = true;
        state.tableBlock = {};
        state.tableBlock.kind = MarkdownBlockKind::Table;
        state.tableBlock.tableColumnCount = table ? table->col_count : 0;
    } else if (type == MD_BLOCK_TR) {
        state.rowActive = true;
        state.currentRow = {};
    } else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
        const auto* cell = static_cast<const MD_BLOCK_TD_DETAIL*>(detail);
        state.cellActive = true;
        state.currentCell = {};
        state.currentCell.header = type == MD_BLOCK_TH;
        state.currentCell.align = cell ? cell->align : MD_ALIGN_DEFAULT;
    }
    return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void*, void* userdata)
{
    auto& state = *static_cast<MarkdownParseState*>(userdata);
    if (type == MD_BLOCK_H || type == MD_BLOCK_CODE || type == MD_BLOCK_HTML || type == MD_BLOCK_LI) {
        flushCurrent(state);
    } else if (type == MD_BLOCK_P) {
        if (!state.cellActive && !(state.currentActive && state.current.kind == MarkdownBlockKind::ListItem)) {
            flushCurrent(state);
        }
    } else if (type == MD_BLOCK_QUOTE) {
        state.quoteDepth = std::max(0, state.quoteDepth - 1);
    } else if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
        if (!state.lists.empty()) {
            state.lists.pop_back();
        }
    } else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
        trimTrailingWhitespace(state.currentCell.text);
        state.currentRow.header = state.currentRow.header || state.currentCell.header;
        state.currentRow.cells.push_back(std::move(state.currentCell));
        state.currentCell = {};
        state.cellActive = false;
    } else if (type == MD_BLOCK_TR) {
        state.tableBlock.rows.push_back(std::move(state.currentRow));
        state.currentRow = {};
        state.rowActive = false;
    } else if (type == MD_BLOCK_TABLE) {
        if (!state.tableBlock.rows.empty()) {
            state.blocks.push_back(std::move(state.tableBlock));
        }
        state.tableBlock = {};
        state.tableActive = false;
    }
    return 0;
}

int enterSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto& state = *static_cast<MarkdownParseState*>(userdata);
    SpanState span;
    if (type == MD_SPAN_EM) {
        span.style = InlineEmphasis;
    } else if (type == MD_SPAN_STRONG) {
        span.style = InlineStrong;
    } else if (type == MD_SPAN_CODE) {
        span.style = InlineCode;
    } else if (type == MD_SPAN_DEL) {
        span.style = InlineStrike;
    } else if (type == MD_SPAN_A) {
        const auto* link = static_cast<const MD_SPAN_A_DETAIL*>(detail);
        span.style = InlineLink;
        span.href = link ? attributeText(link->href) : std::string{};
    } else if (type == MD_SPAN_IMG) {
        const auto* image = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
        span.style = InlineImage;
        span.href = image ? attributeText(image->src) : std::string{};
    } else if (type == MD_SPAN_LATEXMATH || type == MD_SPAN_LATEXMATH_DISPLAY) {
        span.style = InlineMath;
        if (type == MD_SPAN_LATEXMATH_DISPLAY) {
            span.style |= InlineMathDisplay;
        }
    } else if (type == MD_SPAN_WIKILINK) {
        const auto* wiki = static_cast<const MD_SPAN_WIKILINK_DETAIL*>(detail);
        span.style = InlineWiki | InlineLink;
        span.href = wiki ? attributeText(wiki->target) : std::string{};
    } else if (type == MD_SPAN_U) {
        span.style = InlineStrong;
    }
    state.spans.push_back(std::move(span));
    return 0;
}

int leaveSpan(MD_SPANTYPE, void*, void* userdata)
{
    auto& state = *static_cast<MarkdownParseState*>(userdata);
    if (!state.spans.empty()) {
        state.spans.pop_back();
    }
    return 0;
}

int textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    auto& state = *static_cast<MarkdownParseState*>(userdata);
    if (type == MD_TEXT_BR) {
        appendText(state, "\n");
    } else if (type == MD_TEXT_SOFTBR) {
        appendText(state, " ");
    } else if (type == MD_TEXT_NULLCHAR) {
        appendText(state, "\xEF\xBF\xBD");
    } else if (type == MD_TEXT_ENTITY) {
        appendText(state, decodeEntity(textView(text, size)));
    } else {
        appendText(state, textView(text, size));
    }
    return 0;
}

std::vector<MarkdownBlock> parseMarkdown(const std::string& markdown)
{
    MarkdownParseState state;
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = textCallback;
    if (md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &state) != 0 && !markdown.empty()) {
        state.blocks.clear();
        MarkdownBlock fallback;
        fallback.kind = MarkdownBlockKind::Paragraph;
        fallback.text = markdown;
        fallback.runs.push_back(InlineRun{markdown, InlineNone, {}});
        state.blocks.push_back(std::move(fallback));
    }
    flushCurrent(state);
    return std::move(state.blocks);
}

float headingFontScale(unsigned level)
{
    switch (level) {
    case 1: return 1.70f;
    case 2: return 1.48f;
    case 3: return 1.30f;
    case 4: return 1.16f;
    case 5: return 1.06f;
    default: return 1.0f;
    }
}

ImU32 linkColor()
{
    return IM_COL32(92, 174, 255, 255);
}

void drawWrappedText(ImU32 color, const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void drawScaledText(ImU32 color, const std::string& text, float fontScale)
{
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fontScale);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool tokenIsWhitespace(std::string_view token)
{
    return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t';
    });
}

std::vector<std::string_view> inlineTokens(std::string_view text)
{
    std::vector<std::string_view> result;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t start = pos;
        if (text[pos] == '\n') {
            result.push_back(text.substr(pos, 1));
            ++pos;
        } else if (text[pos] == ' ' || text[pos] == '\t') {
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
                ++pos;
            }
            result.push_back(text.substr(start, pos - start));
        } else {
            while (pos < text.size() && text[pos] != '\n' && text[pos] != ' ' && text[pos] != '\t') {
                ++pos;
            }
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
                ++pos;
            }
            result.push_back(text.substr(start, pos - start));
        }
    }
    return result;
}

float tokenWidth(std::string_view token, float fontScale)
{
    std::string value(token);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fontScale);
    const float width = ImGui::CalcTextSize(value.c_str(), nullptr, false).x;
    ImGui::PopFont();
    return width;
}

enum class MathNodeKind {
    Row,
    Text,
    Fraction,
    Script,
    Sqrt
};

struct MathNode {
    MathNodeKind kind = MathNodeKind::Row;
    std::string text;
    std::vector<MathNode> children;
};

struct MathBox {
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
};

size_t utf8LengthAt(std::string_view text, size_t pos)
{
    if (pos >= text.size()) {
        return 0;
    }
    const unsigned char ch = static_cast<unsigned char>(text[pos]);
    if ((ch & 0x80) == 0) return 1;
    if ((ch & 0xE0) == 0xC0) return std::min<size_t>(2, text.size() - pos);
    if ((ch & 0xF0) == 0xE0) return std::min<size_t>(3, text.size() - pos);
    if ((ch & 0xF8) == 0xF0) return std::min<size_t>(4, text.size() - pos);
    return 1;
}

MathNode makeMathText(std::string text)
{
    MathNode node;
    node.kind = MathNodeKind::Text;
    node.text = std::move(text);
    return node;
}

MathNode makeMathRow(std::vector<MathNode> children)
{
    MathNode node;
    node.kind = MathNodeKind::Row;
    node.children = std::move(children);
    return node;
}

std::string mathCommandText(std::string_view command)
{
    struct Entry {
        std::string_view command;
        const char* text;
    };
    static constexpr Entry entries[] = {{"alpha", "α"},     {"beta", "β"},
                                        {"gamma", "γ"},     {"delta", "δ"},
                                        {"epsilon", "ε"},   {"varepsilon", "ε"},
                                        {"zeta", "ζ"},      {"eta", "η"},
                                        {"theta", "θ"},     {"vartheta", "ϑ"},
                                        {"iota", "ι"},      {"kappa", "κ"},
                                        {"lambda", "λ"},    {"mu", "μ"},
                                        {"nu", "ν"},        {"xi", "ξ"},
                                        {"pi", "π"},        {"rho", "ρ"},
                                        {"sigma", "σ"},     {"tau", "τ"},
                                        {"upsilon", "υ"},   {"phi", "φ"},
                                        {"varphi", "ϕ"},    {"chi", "χ"},
                                        {"psi", "ψ"},       {"omega", "ω"},
                                        {"Gamma", "Γ"},     {"Delta", "Δ"},
                                        {"Theta", "Θ"},     {"Lambda", "Λ"},
                                        {"Xi", "Ξ"},        {"Pi", "Π"},
                                        {"Sigma", "Σ"},     {"Phi", "Φ"},
                                        {"Psi", "Ψ"},       {"Omega", "Ω"},
                                        {"sum", "∑"},       {"prod", "∏"},
                                        {"int", "∫"},       {"oint", "∮"},
                                        {"infty", "∞"},     {"partial", "∂"},
                                        {"nabla", "∇"},     {"pm", "±"},
                                        {"mp", "∓"},        {"times", "×"},
                                        {"cdot", "·"},      {"div", "÷"},
                                        {"le", "≤"},        {"leq", "≤"},
                                        {"ge", "≥"},        {"geq", "≥"},
                                        {"neq", "≠"},       {"approx", "≈"},
                                        {"equiv", "≡"},     {"propto", "∝"},
                                        {"to", "→"},        {"rightarrow", "→"},
                                        {"leftarrow", "←"}, {"Rightarrow", "⇒"},
                                        {"Leftarrow", "⇐"}, {"leftrightarrow", "↔"},
                                        {"in", "∈"},        {"notin", "∉"},
                                        {"subset", "⊂"},    {"subseteq", "⊆"},
                                        {"supset", "⊃"},    {"supseteq", "⊇"},
                                        {"cup", "∪"},       {"cap", "∩"},
                                        {"emptyset", "∅"},  {"forall", "∀"},
                                        {"exists", "∃"},    {"neg", "¬"},
                                        {"land", "∧"},      {"lor", "∨"},
                                        {"sin", "sin"},     {"cos", "cos"},
                                        {"tan", "tan"},     {"log", "log"},
                                        {"ln", "ln"},       {"lim", "lim"},
                                        {"min", "min"},     {"max", "max"}};
    for (const Entry& entry : entries) {
        if (command == entry.command) {
            return entry.text;
        }
    }
    return std::string(command);
}

class MathParser {
public:
    explicit MathParser(std::string_view source)
        : source_(source)
    {}

    MathNode parse()
    {
        return parseRow('\0');
    }

private:
    MathNode parseRow(char end)
    {
        std::vector<MathNode> children;
        while (pos_ < source_.size()) {
            if (end != '\0' && source_[pos_] == end) {
                ++pos_;
                break;
            }
            children.push_back(parseScriptedAtom());
        }
        return makeMathRow(std::move(children));
    }

    MathNode parseScriptedAtom()
    {
        MathNode base = parseAtom();
        MathNode sup;
        MathNode sub;
        bool hasSup = false;
        bool hasSub = false;
        while (pos_ < source_.size() && (source_[pos_] == '^' || source_[pos_] == '_')) {
            const char marker = source_[pos_++];
            MathNode value = parseScriptArgument();
            if (marker == '^') {
                sup = std::move(value);
                hasSup = true;
            } else {
                sub = std::move(value);
                hasSub = true;
            }
        }
        if (!hasSup && !hasSub) {
            return base;
        }
        MathNode node;
        node.kind = MathNodeKind::Script;
        node.children.push_back(std::move(base));
        node.children.push_back(hasSup ? std::move(sup) : MathNode{});
        node.children.push_back(hasSub ? std::move(sub) : MathNode{});
        return node;
    }

    MathNode parseScriptArgument()
    {
        if (pos_ < source_.size() && source_[pos_] == '{') {
            ++pos_;
            return parseRow('}');
        }
        return parseAtom();
    }

    MathNode parseAtom()
    {
        if (pos_ >= source_.size()) {
            return makeMathText({});
        }
        const char ch = source_[pos_];
        if (ch == '{') {
            ++pos_;
            return parseRow('}');
        }
        if (ch == '}') {
            ++pos_;
            return makeMathText({});
        }
        if (ch == '\\') {
            return parseCommand();
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            while (pos_ < source_.size() &&
                   (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\r' || source_[pos_] == '\n')) {
                ++pos_;
            }
            return makeMathText(" ");
        }
        const size_t len = utf8LengthAt(source_, pos_);
        std::string text(source_.substr(pos_, len));
        pos_ += len;
        return makeMathText(std::move(text));
    }

    MathNode parseCommand()
    {
        ++pos_;
        if (pos_ >= source_.size()) {
            return makeMathText("\\");
        }
        if (!std::isalpha(static_cast<unsigned char>(source_[pos_]))) {
            const char escaped = source_[pos_++];
            if (escaped == ',' || escaped == ';' || escaped == ':') {
                return makeMathText(" ");
            }
            if (escaped == '!') {
                return makeMathText({});
            }
            return makeMathText(std::string(1, escaped));
        }

        const size_t start = pos_;
        while (pos_ < source_.size() && std::isalpha(static_cast<unsigned char>(source_[pos_]))) {
            ++pos_;
        }
        const std::string_view command = source_.substr(start, pos_ - start);
        if (command == "frac" || command == "dfrac" || command == "tfrac") {
            MathNode node;
            node.kind = MathNodeKind::Fraction;
            node.children.push_back(parseRequiredGroup());
            node.children.push_back(parseRequiredGroup());
            return node;
        }
        if (command == "sqrt") {
            skipOptionalBracket();
            MathNode node;
            node.kind = MathNodeKind::Sqrt;
            node.children.push_back(parseRequiredGroup());
            return node;
        }
        if (command == "left" || command == "right") {
            skipSpaces();
            return parseAtom();
        }
        if (command == "text" || command == "mathrm" || command == "operatorname") {
            return parseRequiredGroup();
        }
        return makeMathText(mathCommandText(command));
    }

    MathNode parseRequiredGroup()
    {
        skipSpaces();
        if (pos_ < source_.size() && source_[pos_] == '{') {
            ++pos_;
            return parseRow('}');
        }
        return parseAtom();
    }

    void skipSpaces()
    {
        while (pos_ < source_.size() && (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\r' || source_[pos_] == '\n')) {
            ++pos_;
        }
    }

    void skipOptionalBracket()
    {
        skipSpaces();
        if (pos_ >= source_.size() || source_[pos_] != '[') {
            return;
        }
        int depth = 1;
        ++pos_;
        while (pos_ < source_.size() && depth > 0) {
            if (source_[pos_] == '[') {
                ++depth;
            } else if (source_[pos_] == ']') {
                --depth;
            }
            ++pos_;
        }
    }

    std::string_view source_;
    size_t pos_ = 0;
};

MathBox measureMathNode(const MathNode& node, float fontSize);

MathBox measureMathText(const std::string& text, float fontSize)
{
    if (text.empty()) {
        return {};
    }
    if (text == " ") {
        return {fontSize * 0.28f, fontSize, fontSize * 0.76f};
    }
    ImGui::PushFont(nullptr, fontSize);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false);
    ImGui::PopFont();
    return {size.x, std::max(size.y, fontSize), fontSize * 0.76f};
}

MathBox measureMathRow(const std::vector<MathNode>& children, float fontSize)
{
    float width = 0.0f;
    float baseline = fontSize * 0.76f;
    float descent = fontSize * 0.24f;
    for (const MathNode& child : children) {
        const MathBox box = measureMathNode(child, fontSize);
        width += box.width;
        baseline = std::max(baseline, box.baseline);
        descent = std::max(descent, box.height - box.baseline);
    }
    return {width, baseline + descent, baseline};
}

MathBox measureMathNode(const MathNode& node, float fontSize)
{
    switch (node.kind) {
    case MathNodeKind::Text: return measureMathText(node.text, fontSize);
    case MathNodeKind::Fraction: {
        const float childFont = fontSize * 0.86f;
        const MathBox numerator = node.children.empty() ? MathBox{} : measureMathNode(node.children[0], childFont);
        const MathBox denominator = node.children.size() < 2 ? MathBox{} : measureMathNode(node.children[1], childFont);
        const float width = std::max(numerator.width, denominator.width) + fontSize * 0.65f;
        const float gap = std::max(3.0f, fontSize * 0.18f);
        const float height = numerator.height + denominator.height + gap * 2.0f + 1.0f;
        return {width, height, numerator.height + gap + 1.0f};
    }
    case MathNodeKind::Script: {
        const MathBox base = node.children.empty() ? MathBox{} : measureMathNode(node.children[0], fontSize);
        const float scriptFont = fontSize * 0.68f;
        const MathBox sup = node.children.size() > 1 ? measureMathNode(node.children[1], scriptFont) : MathBox{};
        const MathBox sub = node.children.size() > 2 ? measureMathNode(node.children[2], scriptFont) : MathBox{};
        const float scriptWidth = std::max(sup.width, sub.width);
        const float baseline = std::max(base.baseline, sup.height + fontSize * 0.08f);
        const float descent = std::max(base.height - base.baseline, sub.height + fontSize * 0.04f);
        return {base.width + scriptWidth + 1.0f, baseline + descent, baseline};
    }
    case MathNodeKind::Sqrt: {
        const MathBox radicand = node.children.empty() ? MathBox{} : measureMathNode(node.children[0], fontSize);
        return {radicand.width + fontSize * 0.72f, radicand.height + fontSize * 0.22f, radicand.baseline + fontSize * 0.22f};
    }
    case MathNodeKind::Row:
    default: return measureMathRow(node.children, fontSize);
    }
}

void drawMathNode(const MathNode& node, const ImVec2& pos, float fontSize, ImU32 color, ImDrawList* dl);

void drawMathText(const std::string& text, const ImVec2& pos, float fontSize, ImU32 color, ImDrawList* dl)
{
    if (text.empty()) {
        return;
    }
    ImGui::PushFont(nullptr, fontSize);
    dl->AddText(nullptr, fontSize, pos, color, text.c_str());
    ImGui::PopFont();
}

void drawMathRow(const std::vector<MathNode>& children, const ImVec2& pos, float fontSize, ImU32 color, ImDrawList* dl)
{
    const MathBox row = measureMathRow(children, fontSize);
    float x = pos.x;
    for (const MathNode& child : children) {
        const MathBox box = measureMathNode(child, fontSize);
        drawMathNode(child, ImVec2(x, pos.y + row.baseline - box.baseline), fontSize, color, dl);
        x += box.width;
    }
}

void drawMathNode(const MathNode& node, const ImVec2& pos, float fontSize, ImU32 color, ImDrawList* dl)
{
    switch (node.kind) {
    case MathNodeKind::Text: drawMathText(node.text, pos, fontSize, color, dl); break;
    case MathNodeKind::Fraction: {
        const MathBox box = measureMathNode(node, fontSize);
        const float childFont = fontSize * 0.86f;
        const MathBox numerator = node.children.empty() ? MathBox{} : measureMathNode(node.children[0], childFont);
        const MathBox denominator = node.children.size() < 2 ? MathBox{} : measureMathNode(node.children[1], childFont);
        const float gap = std::max(3.0f, fontSize * 0.18f);
        const float numeratorX = pos.x + (box.width - numerator.width) * 0.5f;
        const float denominatorX = pos.x + (box.width - denominator.width) * 0.5f;
        if (!node.children.empty()) {
            drawMathNode(node.children[0], ImVec2(numeratorX, pos.y), childFont, color, dl);
        }
        const float lineY = pos.y + numerator.height + gap;
        dl->AddLine(ImVec2(pos.x + fontSize * 0.12f, lineY), ImVec2(pos.x + box.width - fontSize * 0.12f, lineY), color, 1.2f);
        if (node.children.size() > 1) {
            drawMathNode(node.children[1], ImVec2(denominatorX, lineY + gap + 1.0f), childFont, color, dl);
        }
        break;
    }
    case MathNodeKind::Script: {
        const MathBox box = measureMathNode(node, fontSize);
        const MathBox base = node.children.empty() ? MathBox{} : measureMathNode(node.children[0], fontSize);
        if (!node.children.empty()) {
            drawMathNode(node.children[0], ImVec2(pos.x, pos.y + box.baseline - base.baseline), fontSize, color, dl);
        }
        const float scriptFont = fontSize * 0.68f;
        if (node.children.size() > 1 && (!node.children[1].children.empty() || !node.children[1].text.empty())) {
            drawMathNode(node.children[1], ImVec2(pos.x + base.width + 1.0f, pos.y), scriptFont, color, dl);
        }
        if (node.children.size() > 2 && (!node.children[2].children.empty() || !node.children[2].text.empty())) {
            drawMathNode(node.children[2], ImVec2(pos.x + base.width + 1.0f, pos.y + box.baseline + fontSize * 0.04f), scriptFont, color,
                         dl);
        }
        break;
    }
    case MathNodeKind::Sqrt: {
        const MathBox box = measureMathNode(node, fontSize);
        const MathBox radicand = node.children.empty() ? MathBox{} : measureMathNode(node.children[0], fontSize);
        const float left = pos.x + fontSize * 0.10f;
        const float top = pos.y + fontSize * 0.12f;
        const float rootMid = pos.y + box.baseline + fontSize * 0.10f;
        const float rootX = pos.x + fontSize * 0.34f;
        const float contentX = pos.x + fontSize * 0.62f;
        dl->AddLine(ImVec2(left, rootMid - fontSize * 0.18f), ImVec2(rootX, rootMid), color, 1.2f);
        dl->AddLine(ImVec2(rootX, rootMid), ImVec2(contentX - fontSize * 0.08f, top), color, 1.2f);
        dl->AddLine(ImVec2(contentX - fontSize * 0.08f, top), ImVec2(pos.x + box.width, top), color, 1.2f);
        if (!node.children.empty()) {
            drawMathNode(node.children[0], ImVec2(contentX, pos.y + box.baseline - radicand.baseline), fontSize, color, dl);
        }
        break;
    }
    case MathNodeKind::Row:
    default: drawMathRow(node.children, pos, fontSize, color, dl); break;
    }
}

MathNode parseMathFormula(std::string_view formula)
{
    MathParser parser(formula);
    return parser.parse();
}

MathBox measureMathFormula(std::string_view formula, float fontScale)
{
    const MathNode root = parseMathFormula(formula);
    return measureMathNode(root, ImGui::GetStyle().FontSizeBase * fontScale);
}

void drawMathFormula(const UiPalette& theme, std::string_view formula, ImU32 color, float fontScale, bool display)
{
    const float scale = display ? fontScale * 1.12f : fontScale;
    const float fontSize = ImGui::GetStyle().FontSizeBase * scale;
    const MathNode root = parseMathFormula(formula);
    const MathBox box = measureMathNode(root, fontSize);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    if (display) {
        const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        pos.x += std::max(0.0f, (available - box.width) * 0.5f);
        pos.y += ImGui::GetTextLineHeight() * 0.25f;
    }
    const ImVec2 paddedSize(box.width + 2.0f, box.height + (display ? ImGui::GetTextLineHeight() * 0.5f : 0.0f));
    drawMathNode(root, pos, fontSize, color, ImGui::GetWindowDrawList());
    ImGui::Dummy(paddedSize);
}

void openExternalLink(const std::string& href)
{
    if (href.empty()) {
        return;
    }
    const std::wstring wide = widen(href);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

ImU32 runColor(const UiPalette& theme, const InlineRun& run, ImU32 baseColor)
{
    if (hasStyle(run.style, InlineLink)) {
        return linkColor();
    }
    if (hasStyle(run.style, InlineCode) || hasStyle(run.style, InlineMath)) {
        return theme.text;
    }
    if (hasStyle(run.style, InlineImage)) {
        return theme.textMuted;
    }
    return baseColor;
}

void drawInlineToken(const UiPalette& theme, const InlineRun& run, std::string_view token, ImU32 baseColor, float fontScale)
{
    if (hasStyle(run.style, InlineMath)) {
        drawMathFormula(theme, token, runColor(theme, run, baseColor), fontScale, false);
        return;
    }

    std::string value(token);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fontScale);
    if (hasStyle(run.style, InlineCode) && !tokenIsWhitespace(token)) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::CalcTextSize(value.c_str(), nullptr, false);
        const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(theme.frameBg.x, theme.frameBg.y, theme.frameBg.z, 0.42f));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x - 3.0f, pos.y + 1.0f), ImVec2(pos.x + size.x + 3.0f, pos.y + size.y - 1.0f),
                                                  bg, theme.frameRounding * 0.5f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, runColor(theme, run, baseColor));
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hasStyle(run.style, InlineStrike)) {
        const float y = (min.y + max.y) * 0.5f;
        dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), runColor(theme, run, baseColor), 1.0f);
    }
    if (hasStyle(run.style, InlineLink)) {
        dl->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), linkColor(), 1.0f);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (!run.href.empty()) {
                ImGui::SetTooltip("%s", run.href.c_str());
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                openExternalLink(run.href);
            }
        }
    }
}

bool drawInlineImage(const InlineRun& run, const MarkdownImageResolver* imageResolver)
{
    if (!imageResolver || !*imageResolver || run.href.empty()) {
        return false;
    }
    const std::optional<MarkdownImage> image = (*imageResolver)(run.href);
    if (!image || image->textureId == 0 || image->width <= 0 || image->height <= 0) {
        return false;
    }
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float maxHeight = 480.0f;
    const float scale = std::min({1.0f, availableWidth / static_cast<float>(image->width), maxHeight / static_cast<float>(image->height)});
    const ImVec2 size(std::max(1.0f, image->width * scale), std::max(1.0f, image->height * scale));
    ImGui::Image(static_cast<ImTextureID>(image->textureId), size);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", run.text.empty() ? run.href.c_str() : run.text.c_str());
    }
    return true;
}

void drawInlineRuns(const UiPalette& theme, const std::vector<InlineRun>& runs, ImU32 baseColor, float fontScale = 1.0f,
                    const MarkdownImageResolver* imageResolver = nullptr)
{
    const float maxX = ImGui::GetCursorScreenPos().x + std::max(1.0f, ImGui::GetContentRegionAvail().x);
    bool lineStart = true;
    bool drewAny = false;
    for (const InlineRun& run : runs) {
        if (hasStyle(run.style, InlineImage) && !lineStart) {
            ImGui::NewLine();
            lineStart = true;
        }
        if (hasStyle(run.style, InlineImage) && drawInlineImage(run, imageResolver)) {
            lineStart = true;
            drewAny = true;
            continue;
        }
        if (hasStyle(run.style, InlineMath)) {
            if (hasStyle(run.style, InlineMathDisplay)) {
                if (!lineStart) {
                    ImGui::NewLine();
                }
                drawMathFormula(theme, run.text, runColor(theme, run, baseColor), fontScale, true);
                lineStart = true;
                drewAny = true;
                continue;
            }
            const MathBox mathBox = measureMathFormula(run.text, fontScale);
            const float width = mathBox.width + 2.0f;
            if (!lineStart && ImGui::GetCursorScreenPos().x + width > maxX) {
                ImGui::NewLine();
                lineStart = true;
            }
            if (!lineStart) {
                ImGui::SameLine(0.0f, 0.0f);
            }
            drawInlineToken(theme, run, run.text, baseColor, fontScale);
            lineStart = false;
            drewAny = true;
            continue;
        }
        for (std::string_view token : inlineTokens(run.text)) {
            if (token == "\n") {
                ImGui::NewLine();
                lineStart = true;
                drewAny = true;
                continue;
            }
            if (lineStart && tokenIsWhitespace(token)) {
                continue;
            }
            const float width = tokenWidth(token, fontScale);
            if (!lineStart && !tokenIsWhitespace(token) && ImGui::GetCursorScreenPos().x + width > maxX) {
                ImGui::NewLine();
                lineStart = true;
            }
            if (!lineStart) {
                ImGui::SameLine(0.0f, 0.0f);
            }
            drawInlineToken(theme, run, token, baseColor, fontScale);
            lineStart = false;
            drewAny = true;
        }
    }
    if (!drewAny) {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
    }
}

std::string listMarker(const MarkdownBlock& block)
{
    if (block.task) {
        return block.taskChecked ? "[x] " : "[ ] ";
    }
    if (block.ordered) {
        return std::to_string(block.itemNumber) + ". ";
    }
    return "- ";
}

std::string tablePlainText(const MarkdownBlock& block)
{
    std::string text;
    for (const MarkdownRow& row : block.rows) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        for (size_t i = 0; i < row.cells.size(); ++i) {
            if (i > 0) {
                text += " | ";
            }
            text += row.cells[i].text;
        }
    }
    return text;
}

std::string blockPlainText(const MarkdownBlock& block)
{
    if (block.kind == MarkdownBlockKind::ListItem) {
        return std::string(static_cast<size_t>(std::max(0, block.listDepth - 1)) * 2, ' ') + listMarker(block) + block.text;
    }
    if (block.kind == MarkdownBlockKind::Table) {
        return tablePlainText(block);
    }
    return block.text;
}

float selectableTextHeight(const std::string& text)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, -1.0f);
    return std::max(ImGui::GetTextLineHeightWithSpacing() + style.FramePadding.y * 2.0f, textSize.y + style.FramePadding.y * 2.0f + 2.0f);
}

void drawReadonlySelectableText(const UiPalette& theme, const char* id, const std::string& text, ImU32 color, bool framed,
                                float fontScale = 1.0f)
{
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    std::string value = text;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fontScale);
    const float textWidth = ImGui::CalcTextSize(value.c_str(), nullptr, false, -1.0f).x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    const float height = selectableTextHeight(value);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, framed ? theme.frameBg : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, framed ? theme.frameHovered : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, framed ? theme.frameActive : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, framed ? theme.frameRounding : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, framed ? 1.0f : 0.0f);
    const ImGuiID inputId = ImGui::GetID("##text");
    ImGui::InputTextMultiline("##text", &value, ImVec2(std::max(width, textWidth), height),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo);
    ImGuiInputTextState* state = ImGui::GetInputTextState(inputId);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
    if (ImGui::BeginPopupContextItem("markdown-text-menu", ImGuiPopupFlags_MouseButtonRight)) {
        state = ImGui::GetInputTextState(inputId);
        const bool hasSel = state && state->HasSelection();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSel || !value.empty())) {
            if (hasSel && state) {
                const int a = state->GetSelectionStart();
                const int b = state->GetSelectionEnd();
                const int start = a < b ? a : b;
                const int end = a < b ? b : a;
                const std::string selected = value.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
                ImGui::SetClipboardText(selected.c_str());
            } else {
                ImGui::SetClipboardText(value.c_str());
            }
        }
        if (ImGui::MenuItem("Select All", "Ctrl+A", false, !value.empty())) {
            if (state) {
                state->SelectAll();
            }
        }
        if (ImGui::MenuItem("Copy All")) {
            ImGui::SetClipboardText(value.c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    ImGui::PopFont();
}

enum class SyntaxTokenKind {
    Plain,
    Keyword,
    Type,
    String,
    Number,
    Comment,
    Punctuation
};

struct SyntaxToken {
    std::string text;
    SyntaxTokenKind kind = SyntaxTokenKind::Plain;
};

std::string lowerAscii(std::string_view value)
{
    std::string result(value);
    for (char& ch : result) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return result;
}

bool syntaxWordIn(std::string_view word, std::initializer_list<std::string_view> values)
{
    return std::find(values.begin(), values.end(), word) != values.end();
}

bool syntaxIsIdentifierStart(char ch)
{
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool syntaxIsIdentifier(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool syntaxIsKeyword(std::string_view language, std::string_view word)
{
    if (language == "json" || language == "jsonc") {
        return syntaxWordIn(word, {"true", "false", "null"});
    }
    if (language == "python" || language == "py") {
        return syntaxWordIn(word, {"and",    "as",   "assert", "async",  "await",    "break", "class", "continue", "def",
                                   "del",    "elif", "else",   "except", "finally",  "for",   "from",  "global",   "if",
                                   "import", "in",   "is",     "lambda", "nonlocal", "not",   "or",    "pass",     "raise",
                                   "return", "try",  "while",  "with",   "yield",    "True",  "False", "None"});
    }
    if (language == "powershell" || language == "ps1" || language == "pwsh") {
        return syntaxWordIn(word, {"if",        "else",   "elseif",  "foreach", "for",     "while", "switch", "function",
                                   "param",     "return", "try",     "catch",   "finally", "throw", "class",  "using",
                                   "namespace", "begin",  "process", "end",     "true",    "false", "null"});
    }
    if (language == "javascript" || language == "js" || language == "typescript" || language == "ts") {
        return syntaxWordIn(word,
                            {"async",  "await", "break",      "case",   "catch",   "class",   "const",  "continue", "debugger", "default",
                             "delete", "do",    "else",       "export", "extends", "finally", "for",    "from",     "function", "if",
                             "import", "in",    "instanceof", "let",    "new",     "return",  "switch", "this",     "throw",    "try",
                             "typeof", "var",   "void",       "while",  "yield",   "true",    "false",  "null",     "undefined"});
    }
    return syntaxWordIn(word, {"alignas",  "alignof",  "auto",      "bool",      "break",    "case",      "catch",   "char",     "class",
                               "concept",  "const",    "constexpr", "consteval", "continue", "decltype",  "default", "delete",   "do",
                               "double",   "else",     "enum",      "explicit",  "export",   "extern",    "false",   "float",    "for",
                               "friend",   "if",       "inline",    "int",       "long",     "namespace", "new",     "noexcept", "nullptr",
                               "operator", "private",  "protected", "public",    "return",   "short",     "signed",  "sizeof",   "static",
                               "struct",   "switch",   "template",  "this",      "throw",    "true",      "try",     "typedef",  "typename",
                               "union",    "unsigned", "using",     "virtual",   "void",     "volatile",  "while"});
}

bool syntaxIsType(std::string_view language, std::string_view word)
{
    if (language == "json" || language == "jsonc" || language == "markdown" || language == "md") {
        return false;
    }
    return syntaxWordIn(word, {"std",     "string",  "wstring", "vector",   "map",      "unordered_map", "set",    "optional",
                               "variant", "size_t",  "uint8_t", "uint16_t", "uint32_t", "uint64_t",      "int8_t", "int16_t",
                               "int32_t", "int64_t", "ImVec2",  "ImVec4",   "ImU32",    "HWND",          "DWORD",  "BOOL"});
}

void pushSyntaxToken(std::vector<SyntaxToken>& tokens, std::string_view text, SyntaxTokenKind kind)
{
    if (text.empty()) {
        return;
    }
    if (!tokens.empty() && tokens.back().kind == kind) {
        tokens.back().text.append(text.data(), text.size());
        return;
    }
    tokens.push_back(SyntaxToken{std::string(text), kind});
}

std::vector<SyntaxToken> highlightMarkdownLine(std::string_view line)
{
    std::vector<SyntaxToken> tokens;
    size_t pos = 0;
    while (pos < line.size() && line[pos] == ' ') {
        ++pos;
    }
    if (pos < line.size() && line[pos] == '#') {
        pushSyntaxToken(tokens, line.substr(0, pos), SyntaxTokenKind::Plain);
        pushSyntaxToken(tokens, line.substr(pos), SyntaxTokenKind::Keyword);
    } else if (pos + 1 < line.size() && (line[pos] == '-' || line[pos] == '*' || line[pos] == '+') && line[pos + 1] == ' ') {
        pushSyntaxToken(tokens, line.substr(0, pos), SyntaxTokenKind::Plain);
        pushSyntaxToken(tokens, line.substr(pos, 2), SyntaxTokenKind::Keyword);
        pushSyntaxToken(tokens, line.substr(pos + 2), SyntaxTokenKind::Plain);
    } else if (pos < line.size() && line[pos] == '>') {
        pushSyntaxToken(tokens, line.substr(0, pos), SyntaxTokenKind::Plain);
        pushSyntaxToken(tokens, line.substr(pos), SyntaxTokenKind::Comment);
    } else {
        pushSyntaxToken(tokens, line, SyntaxTokenKind::Plain);
    }
    return tokens;
}

std::vector<SyntaxToken> highlightCodeLine(std::string_view language, std::string_view line)
{
    const std::string lang = lowerAscii(language);
    if (lang == "markdown" || lang == "md") {
        return highlightMarkdownLine(line);
    }

    std::vector<SyntaxToken> tokens;
    size_t pos = 0;
    while (pos < line.size()) {
        const char ch = line[pos];
        if ((lang == "python" || lang == "py" || lang == "powershell" || lang == "ps1" || lang == "pwsh") && ch == '#') {
            pushSyntaxToken(tokens, line.substr(pos), SyntaxTokenKind::Comment);
            break;
        }
        if (ch == '/' && pos + 1 < line.size() && line[pos + 1] == '/') {
            pushSyntaxToken(tokens, line.substr(pos), SyntaxTokenKind::Comment);
            break;
        }
        if (ch == '/' && pos + 1 < line.size() && line[pos + 1] == '*') {
            const size_t end = line.find("*/", pos + 2);
            const size_t count = end == std::string_view::npos ? line.size() - pos : end + 2 - pos;
            pushSyntaxToken(tokens, line.substr(pos, count), SyntaxTokenKind::Comment);
            pos += count;
            continue;
        }
        if (ch == '"' || ch == '\'' || (ch == '`' && (lang == "powershell" || lang == "ps1" || lang == "pwsh"))) {
            const char quote = ch;
            size_t end = pos + 1;
            bool escaped = false;
            while (end < line.size()) {
                const char current = line[end++];
                if (current == quote && !escaped) {
                    break;
                }
                escaped = current == '\\' && !escaped;
                if (current != '\\') {
                    escaped = false;
                }
            }
            pushSyntaxToken(tokens, line.substr(pos, end - pos), SyntaxTokenKind::String);
            pos = end;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) && (pos == 0 || !syntaxIsIdentifier(line[pos - 1]))) {
            const size_t start = pos++;
            while (pos < line.size() && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '.' || line[pos] == '_')) {
                ++pos;
            }
            pushSyntaxToken(tokens, line.substr(start, pos - start), SyntaxTokenKind::Number);
            continue;
        }
        if (syntaxIsIdentifierStart(ch) || (ch == '$' && (lang == "powershell" || lang == "ps1" || lang == "pwsh"))) {
            const size_t start = pos++;
            while (pos < line.size() && (syntaxIsIdentifier(line[pos]) || line[pos] == '$')) {
                ++pos;
            }
            const std::string_view word = line.substr(start, pos - start);
            if (syntaxIsKeyword(lang, word)) {
                pushSyntaxToken(tokens, word, SyntaxTokenKind::Keyword);
            } else if (syntaxIsType(lang, word)) {
                pushSyntaxToken(tokens, word, SyntaxTokenKind::Type);
            } else {
                pushSyntaxToken(tokens, word, SyntaxTokenKind::Plain);
            }
            continue;
        }
        if (std::ispunct(static_cast<unsigned char>(ch)) && ch != '_') {
            pushSyntaxToken(tokens, line.substr(pos, 1), SyntaxTokenKind::Punctuation);
        } else {
            pushSyntaxToken(tokens, line.substr(pos, 1), SyntaxTokenKind::Plain);
        }
        ++pos;
    }
    return tokens;
}

ImU32 syntaxColor(const UiPalette& theme, SyntaxTokenKind kind)
{
    switch (kind) {
    case SyntaxTokenKind::Keyword: return IM_COL32(113, 175, 255, 255);
    case SyntaxTokenKind::Type: return IM_COL32(205, 158, 255, 255);
    case SyntaxTokenKind::String: return IM_COL32(126, 202, 153, 255);
    case SyntaxTokenKind::Number: return IM_COL32(239, 176, 112, 255);
    case SyntaxTokenKind::Comment: return theme.textMuted;
    case SyntaxTokenKind::Punctuation: return IM_COL32(190, 195, 205, 255);
    case SyntaxTokenKind::Plain:
    default: return theme.text;
    }
}

std::vector<std::string_view> splitCodeLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        size_t lineEnd = end;
        if (lineEnd > start && text[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        lines.push_back(text.substr(start, lineEnd - start));
        start = end + 1;
    }
    return lines;
}

float highlightedCodeWidth(std::string_view text)
{
    float width = 1.0f;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
    for (std::string_view line : splitCodeLines(text)) {
        width = std::max(width, ImGui::CalcTextSize(std::string(line).c_str(), nullptr, false).x);
    }
    ImGui::PopFont();
    return width;
}

float highlightedCodeHeight(std::string_view text)
{
    return std::max(ImGui::GetTextLineHeightWithSpacing(),
                    ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(splitCodeLines(text).size()));
}

void drawHighlightedCodeAt(const UiPalette& theme, std::string_view language, std::string_view text, const ImVec2& start)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float fontSize = ImGui::GetStyle().FontSizeBase;
    float y = start.y;
    for (std::string_view line : splitCodeLines(text)) {
        float x = start.x;
        for (const SyntaxToken& token : highlightCodeLine(language, line)) {
            dl->AddText(nullptr, fontSize, ImVec2(x, y), syntaxColor(theme, token.kind), token.text.c_str());
            ImGui::PushFont(nullptr, fontSize);
            x += ImGui::CalcTextSize(token.text.c_str(), nullptr, false).x;
            ImGui::PopFont();
        }
        y += lineHeight;
    }
}

void drawHighlightedCode(const UiPalette& theme, std::string_view language, std::string_view text)
{
    drawHighlightedCodeAt(theme, language, text, ImGui::GetCursorScreenPos());
    ImGui::Dummy(ImVec2(highlightedCodeWidth(text) + ImGui::GetStyle().WindowPadding.x * 2.0f, highlightedCodeHeight(text)));
}

void drawCodeBlock(const UiPalette& theme, const std::string& id, const std::string& text, const std::string& language,
                   bool selectable = false)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.frameBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.frameRounding);
    const float height = std::max(ImGui::GetTextLineHeightWithSpacing() * 3.0f,
                                  ImGui::CalcTextSize(text.c_str(), nullptr, false, -FLT_MIN).y + ImGui::GetStyle().WindowPadding.y * 2.0f);
    ImGui::BeginChild(id.c_str(), ImVec2(-FLT_MIN, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    if (selectable && ImGui::BeginPopupContextWindow("code-block-menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
            ImGui::SetClipboardText(text.c_str());
        }
        if (ImGui::MenuItem("Copy Code")) {
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::EndPopup();
    }
    if (selectable) {
        const ImVec2 codePos = ImGui::GetCursorScreenPos();
        const float codeWidth = highlightedCodeWidth(text) + ImGui::GetStyle().WindowPadding.x * 2.0f;
        const float codeHeight = highlightedCodeHeight(text);
        drawHighlightedCodeAt(theme, language, text, codePos);
        std::string selectableText = text;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::InputTextMultiline("##code-select", &selectableText, ImVec2(codeWidth, codeHeight),
                                  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    } else {
        drawHighlightedCode(theme, language, text);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void drawMarkdownTable(const UiPalette& theme, const MarkdownBlock& block, int tableId, const MarkdownImageResolver* imageResolver)
{
    unsigned colCount = block.tableColumnCount;
    for (const MarkdownRow& row : block.rows) {
        colCount = std::max<unsigned>(colCount, static_cast<unsigned>(row.cells.size()));
    }
    if (colCount == 0) {
        return;
    }

    const std::string id = "markdown-table-" + std::to_string(tableId);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id.c_str(), static_cast<int>(colCount), flags)) {
        return;
    }
    for (unsigned column = 0; column < colCount; ++column) {
        ImGui::TableSetupColumn(nullptr);
    }
    for (const MarkdownRow& row : block.rows) {
        ImGui::TableNextRow();
        for (unsigned column = 0; column < colCount; ++column) {
            ImGui::TableSetColumnIndex(static_cast<int>(column));
            if (column >= row.cells.size()) {
                continue;
            }
            const MarkdownCell& cell = row.cells[column];
            if (cell.header) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme.text);
                drawInlineRuns(theme, cell.runs, theme.text, 1.0f, imageResolver);
                ImGui::PopStyleColor();
            } else {
                drawInlineRuns(theme, cell.runs, theme.text, 1.0f, imageResolver);
            }
        }
    }
    ImGui::EndTable();
}

bool hasRichInline(const MarkdownBlock& block)
{
    for (const InlineRun& run : block.runs) {
        if (run.style != InlineNone) {
            return true;
        }
    }
    return false;
}

void applyHeadingScrollTarget(MarkdownOutlineUiState* scrollState, int headingIndex)
{
    if (!scrollState || scrollState->pendingHeadingIndex < 0) {
        return;
    }
    if (scrollState->pendingHeadingIndex != headingIndex) {
        return;
    }
    // Keep the jumped heading near 20% of the viewport; edges clamp naturally.
    ImGui::SetScrollHereY(0.20f);
    scrollState->pendingHeadingIndex = -1;
}

std::string trimCopyLocal(std::string value)
{
    auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool isSetextUnderline(const std::string& line, char ch)
{
    if (line.empty()) {
        return false;
    }
    for (unsigned char c : line) {
        if (c != static_cast<unsigned char>(ch) && !std::isspace(c)) {
            return false;
        }
    }
    return line.find(ch) != std::string::npos;
}

int findHeadingSourceLine(const std::string& markdown, const std::string& title, int startLine)
{
    int lineIndex = 0;
    size_t cursor = 0;
    std::string previous;
    int previousLine = -1;
    while (cursor <= markdown.size()) {
        size_t end = markdown.find('\n', cursor);
        if (end == std::string::npos) {
            end = markdown.size();
        }
        std::string line = markdown.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (lineIndex >= startLine) {
            size_t pos = 0;
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
                ++pos;
            }
            int level = 0;
            while (pos + static_cast<size_t>(level) < line.size() && line[pos + static_cast<size_t>(level)] == '#') {
                ++level;
                if (level > 6) {
                    break;
                }
            }
            if (level >= 1 && level <= 6) {
                const size_t titlePos = pos + static_cast<size_t>(level);
                if (titlePos < line.size() && std::isspace(static_cast<unsigned char>(line[titlePos]))) {
                    if (trimCopyLocal(line.substr(titlePos + 1)) == title) {
                        return lineIndex;
                    }
                }
            }
            if ((isSetextUnderline(line, '=') || isSetextUnderline(line, '-')) && previousLine >= startLine) {
                if (trimCopyLocal(previous) == title) {
                    return previousLine;
                }
            }
        }
        previous = line;
        previousLine = lineIndex;
        if (end >= markdown.size()) {
            break;
        }
        cursor = end + 1;
        ++lineIndex;
    }
    return std::max(0, startLine);
}

std::string elideOutlineText(const std::string& text, float maxWidth)
{
    if (text.empty() || maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    std::string value = text;
    constexpr const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    while (!value.empty() && ImGui::CalcTextSize(value.c_str()).x + ellipsisWidth > maxWidth) {
        if ((static_cast<unsigned char>(value.back()) & 0x80) == 0) {
            value.pop_back();
        } else {
            while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) {
                value.pop_back();
            }
            if (!value.empty()) {
                value.pop_back();
            }
        }
    }
    return value.empty() ? ellipsis : value + ellipsis;
}

} // namespace

std::vector<MarkdownOutlineItem> buildMarkdownOutline(const std::string& markdown)
{
    std::vector<MarkdownOutlineItem> items;
    const std::vector<MarkdownBlock> blocks = parseMarkdown(markdown);
    int searchFromLine = 0;
    for (const MarkdownBlock& block : blocks) {
        if (block.kind != MarkdownBlockKind::Heading) {
            continue;
        }
        MarkdownOutlineItem item;
        item.level = static_cast<int>(std::clamp(block.headingLevel, 1u, 6u));
        item.title = block.text.empty() ? blockPlainText(block) : block.text;
        item.title = trimCopyLocal(item.title);
        item.line = findHeadingSourceLine(markdown, item.title, searchFromLine);
        searchFromLine = item.line + 1;
        items.push_back(std::move(item));
    }
    return items;
}

void drawMarkdownPreview(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState* scrollState,
                         const MarkdownImageResolver* imageResolver)
{
    const std::vector<MarkdownBlock> blocks = parseMarkdown(markdown);
    int codeId = 0;
    int tableId = 0;
    int headingIndex = 0;
    for (const MarkdownBlock& block : blocks) {
        if (block.kind == MarkdownBlockKind::Heading) {
            ImGui::Spacing();
            drawInlineRuns(theme, block.runs, theme.text, headingFontScale(block.headingLevel), imageResolver);
            applyHeadingScrollTarget(scrollState, headingIndex);
            ++headingIndex;
            if (block.headingLevel <= 2) {
                ImGui::Separator();
            }
        } else if (block.kind == MarkdownBlockKind::Quote) {
            const float indent = std::max(1, block.quoteDepth) * 12.0f;
            ImGui::Indent(indent);
            drawInlineRuns(theme, block.runs, theme.textMuted, 1.0f, imageResolver);
            ImGui::Unindent(indent);
        } else if (block.kind == MarkdownBlockKind::ListItem) {
            const float indent = std::max(0, block.listDepth - 1) * 18.0f;
            ImGui::Indent(indent);
            ImGui::TextUnformatted(listMarker(block).c_str());
            ImGui::SameLine(0.0f, 0.0f);
            drawInlineRuns(theme, block.runs, theme.text, 1.0f, imageResolver);
            ImGui::Unindent(indent);
        } else if (block.kind == MarkdownBlockKind::Code) {
            drawCodeBlock(theme, "markdown-code-" + std::to_string(codeId++), block.text, block.codeLanguage);
        } else if (block.kind == MarkdownBlockKind::Html) {
            drawReadonlySelectableText(theme, ("markdown-html-" + std::to_string(codeId++)).c_str(), block.text, theme.textMuted, true);
        } else if (block.kind == MarkdownBlockKind::Table) {
            drawMarkdownTable(theme, block, tableId++, imageResolver);
        } else if (block.kind == MarkdownBlockKind::HorizontalRule) {
            ImGui::Separator();
        } else {
            drawInlineRuns(theme, block.runs, theme.text, 1.0f, imageResolver);
        }
        ImGui::Spacing();
    }
}

void drawSelectableMarkdownPreview(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState* scrollState,
                                   const MarkdownImageResolver* imageResolver)
{
    const std::vector<MarkdownBlock> blocks = parseMarkdown(markdown);
    int tableId = 0;
    int headingIndex = 0;
    if (ImGui::BeginPopupContextWindow("markdown-preview-menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
            ImGui::SetClipboardText(markdownPlainText(markdown).c_str());
        }
        if (ImGui::MenuItem("Copy All", "Ctrl+A")) {
            ImGui::SetClipboardText(markdownPlainText(markdown).c_str());
        }
        ImGui::EndPopup();
    }

    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const MarkdownBlock& block = blocks[static_cast<size_t>(i)];
        const std::string id = "markdown-select-" + std::to_string(i);
        if (block.kind == MarkdownBlockKind::Heading) {
            ImGui::Spacing();
            if (hasRichInline(block)) {
                drawInlineRuns(theme, block.runs, theme.text, headingFontScale(block.headingLevel), imageResolver);
            } else {
                drawReadonlySelectableText(theme, id.c_str(), block.text, theme.text, false, headingFontScale(block.headingLevel));
            }
            applyHeadingScrollTarget(scrollState, headingIndex);
            ++headingIndex;
            if (block.headingLevel <= 2) {
                ImGui::Separator();
            }
        } else if (block.kind == MarkdownBlockKind::Quote) {
            const float indent = std::max(1, block.quoteDepth) * 12.0f;
            ImGui::Indent(indent);
            if (hasRichInline(block)) {
                drawInlineRuns(theme, block.runs, theme.textMuted, 1.0f, imageResolver);
            } else {
                drawReadonlySelectableText(theme, id.c_str(), block.text, theme.textMuted, false);
            }
            ImGui::Unindent(indent);
        } else if (block.kind == MarkdownBlockKind::ListItem) {
            const float indent = std::max(0, block.listDepth - 1) * 18.0f;
            ImGui::Indent(indent);
            if (hasRichInline(block)) {
                ImGui::TextUnformatted(listMarker(block).c_str());
                ImGui::SameLine(0.0f, 0.0f);
                drawInlineRuns(theme, block.runs, theme.text, 1.0f, imageResolver);
            } else {
                drawReadonlySelectableText(theme, id.c_str(), listMarker(block) + block.text, theme.text, false);
            }
            ImGui::Unindent(indent);
        } else if (block.kind == MarkdownBlockKind::Code) {
            drawCodeBlock(theme, id, block.text, block.codeLanguage, true);
        } else if (block.kind == MarkdownBlockKind::Html) {
            drawReadonlySelectableText(theme, id.c_str(), block.text, theme.textMuted, true);
        } else if (block.kind == MarkdownBlockKind::Table) {
            drawMarkdownTable(theme, block, tableId++, imageResolver);
        } else if (block.kind == MarkdownBlockKind::HorizontalRule) {
            ImGui::Separator();
        } else if (hasRichInline(block)) {
            drawInlineRuns(theme, block.runs, theme.text, 1.0f, imageResolver);
        } else {
            drawReadonlySelectableText(theme, id.c_str(), block.text, theme.text, false);
        }
        ImGui::Spacing();
    }
}

void drawMarkdownOutlinePanel(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState& state)
{
    ImGui::TextUnformatted(tr("Outline"));
    ImGui::Dummy(ImVec2(1.0f, 4.0f));
    // Must match render order from md4c (includes setext headings).
    const std::vector<MarkdownOutlineItem> headings = buildMarkdownOutline(markdown);
    ImGui::BeginChild("markdown-outline-items", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None);
    if (headings.empty()) {
        ImGui::TextDisabled("%s", tr("No headings"));
    } else {
        for (size_t i = 0; i < headings.size(); ++i) {
            const MarkdownOutlineItem& heading = headings[i];
            ImGui::PushID(static_cast<int>(i));
            const float depth = static_cast<float>(std::max(0, heading.level - 1));
            const float indent = 8.0f + depth * 12.0f;
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            const float rowHeight = 24.0f;
            ImGui::InvisibleButton("outline-row", ImVec2(rowWidth, rowHeight));
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = state.pendingHeadingIndex == static_cast<int>(i);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (hovered || selected) {
                const ImU32 bg = ImGui::ColorConvertFloat4ToU32(selected ? theme.headerActive : theme.headerHovered);
                dl->AddRectFilled(row, ImVec2(row.x + rowWidth, row.y + rowHeight), bg, theme.itemRounding);
            }
            if (ImGui::IsItemClicked()) {
                state.pendingHeadingIndex = static_cast<int>(i);
                state.pendingLine = heading.line;
                state.editorScrollFrames = 2;
            }
            const std::string visible = elideOutlineText(heading.title, std::max(24.0f, rowWidth - indent - 8.0f));
            dl->AddText(ImVec2(row.x + indent, row.y + 4.0f), theme.text, visible.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void drawMarkdownDocument(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState& state, bool selectable,
                          const MarkdownImageResolver* imageResolver)
{
    const float gap = 10.0f;
    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float totalHeight = ImGui::GetContentRegionAvail().y;
    const float outlineWidth = state.showOutline ? std::clamp(totalWidth * 0.22f, 150.0f, 240.0f) : 0.0f;
    const float contentWidth = std::max(120.0f, totalWidth - outlineWidth - (state.showOutline ? gap : 0.0f));

    ImGui::BeginChild("markdown-document-content", ImVec2(contentWidth, totalHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
    if (selectable) {
        drawSelectableMarkdownPreview(theme, markdown, &state, imageResolver);
    } else {
        drawMarkdownPreview(theme, markdown, &state, imageResolver);
    }
    ImGui::EndChild();

    if (state.showOutline) {
        ImGui::SameLine(0.0f, gap);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.frameRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.childBg);
        ImGui::PushStyleColor(ImGuiCol_Border, theme.border);
        ImGui::BeginChild("markdown-document-outline", ImVec2(outlineWidth, totalHeight), ImGuiChildFlags_Borders);
        drawMarkdownOutlinePanel(theme, markdown, state);
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
}

std::string markdownPlainText(const std::string& markdown)
{
    std::string text;
    for (const MarkdownBlock& block : parseMarkdown(markdown)) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        text += blockPlainText(block);
    }
    return text;
}

} // namespace launcher
