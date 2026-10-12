// The developer console (~, v0.33): a CS-style command line over the game. This is the text side (the log, the line
// being typed, history, Tab completion, splitting a line into commands and words); main.cpp runs the commands.
#pragma once
#include <string>
#include <vector>

struct Console {
    static constexpr size_t kMaxLog = 400;
    bool open = false;
    std::string line;                  // what's being typed
    std::vector<std::string> log;      // what it printed, newest last
    std::vector<std::string> history;  // lines entered, newest last
    int historyPos = -1;               // browsing history with Up / Down (-1: the line being typed)
    int scroll = 0;                    // lines scrolled back from the newest (Page Up / Page Down)

    void print(const std::string& text);  // (several lines: split on newlines)
    // Enter: the line goes into the history (and the log, after "] "); returns it and clears it.
    std::string submit();
    void historyUp();
    void historyDown();
    // Tab: completes the line's first word as far as every name in `names` starting with it agrees; with more than one
    // left, prints them. True if anything matched.
    bool complete(const std::vector<std::string>& names);
};

// "a; b c" -> {"a", "b c"}: the commands on a line (a ';' inside quotes stays).
std::vector<std::string> splitCommands(const std::string& line);
// `give weapon_ak47` -> {"give", "weapon_ak47"}; "quoted words" stay together, the quotes go.
std::vector<std::string> splitArgs(const std::string& command);
std::string lowerCase(std::string s);
