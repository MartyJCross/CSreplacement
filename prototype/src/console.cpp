#include "console.h"
#include <algorithm>
#include <cctype>

void Console::print(const std::string& text) {
    size_t from = 0;
    while (from <= text.size()) {
        const size_t nl = text.find('\n', from);
        log.push_back(text.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    if (log.size() > kMaxLog) log.erase(log.begin(), log.begin() + std::ptrdiff_t(log.size() - kMaxLog));
    scroll = 0;
}

std::string Console::submit() {
    std::string entered = line;
    line.clear();
    historyPos = -1;
    print("] " + entered);
    if (!entered.empty() && (history.empty() || history.back() != entered)) history.push_back(entered);
    if (history.size() > 64) history.erase(history.begin());
    return entered;
}

void Console::historyUp() {
    if (history.empty()) return;
    historyPos = historyPos < 0 ? int(history.size()) - 1 : std::max(0, historyPos - 1);
    line = history[size_t(historyPos)];
}

void Console::historyDown() {
    if (historyPos < 0) return;
    if (++historyPos >= int(history.size())) {
        historyPos = -1;
        line.clear();
    } else {
        line = history[size_t(historyPos)];
    }
}

bool Console::complete(const std::vector<std::string>& names) {
    const size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || line.find(' ', start) != std::string::npos) return false;  // (the first word only)
    const std::string word = lowerCase(line.substr(start));
    std::vector<std::string> hits;
    for (const std::string& n : names)
        if (n.compare(0, word.size(), word) == 0 && std::find(hits.begin(), hits.end(), n) == hits.end()) hits.push_back(n);
    if (hits.empty()) return false;
    std::string common = hits[0];
    for (const std::string& h : hits) {
        size_t k = 0;
        while (k < common.size() && k < h.size() && common[k] == h[k]) ++k;
        common.resize(k);
    }
    line = common + (hits.size() == 1 ? " " : "");
    if (hits.size() > 1) {
        std::sort(hits.begin(), hits.end());
        std::string row;
        for (const std::string& h : hits) {
            if (row.size() + h.size() > 90) {
                print("  " + row);
                row.clear();
            }
            row += h + "  ";
        }
        if (!row.empty()) print("  " + row);
    }
    return true;
}

std::vector<std::string> splitCommands(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') quoted = !quoted;
        if (ch == ';' && !quoted) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(cur);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const std::string& s) { return s.find_first_not_of(' ') == std::string::npos; }),
              out.end());
    return out;
}

std::vector<std::string> splitArgs(const std::string& command) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, any = false;
    for (char ch : command) {
        if (ch == '"') {
            quoted = !quoted;
            any = true;
        } else if (ch == ' ' && !quoted) {
            if (any) out.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur.push_back(ch);
            any = true;
        }
    }
    if (any) out.push_back(cur);
    return out;
}

std::string lowerCase(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}
