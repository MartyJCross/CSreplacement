#include "binds.h"
#include <sstream>

namespace {

const BindInfo kInfo[kBindCount] = {
    {"forward", "MOVE FORWARD"}, {"back", "MOVE BACK"}, {"left", "MOVE LEFT"}, {"right", "MOVE RIGHT"},
    {"jump", "JUMP"}, {"crouch", "CROUCH"}, {"walk", "WALK"},
    {"fire", "FIRE / THROW / SLASH"}, {"altfire", "SCOPE / LOB / STAB"}, {"reload", "RELOAD"},
    {"use", "PLANT / DEFUSE / PICK UP"},
    {"primary", "PRIMARY WEAPON"}, {"secondary", "PISTOL"}, {"knife", "KNIFE"}, {"grenade", "GRENADES (AGAIN: NEXT)"},
    {"lastweapon", "LAST WEAPON"}, {"inspect", "INSPECT"}, {"drop", "DROP GUN / QUICK THROW"},
    {"buy", "BUY MENU"}, {"scores", "SCOREBOARD"}, {"call", "CALL A PLAY"}, {"ping", "PING"},
    {"noclip", "NOCLIP (PRACTICE)"}, {"cleardecals", "CLEAR BULLET HOLES"},
};

// Names for the codes that aren't a letter or a digit (SDL scancode numbers).
struct Named { int code; const char* name; };
const Named kNamed[] = {
    {40, "ENTER"}, {41, "ESCAPE"}, {42, "BACKSPACE"}, {43, "TAB"}, {44, "SPACE"}, {45, "MINUS"}, {46, "EQUALS"},
    {47, "LBRACKET"}, {48, "RBRACKET"}, {49, "BACKSLASH"}, {51, "SEMICOLON"}, {52, "APOSTROPHE"}, {53, "GRAVE"},
    {54, "COMMA"}, {55, "PERIOD"}, {56, "SLASH"}, {57, "CAPSLOCK"},
    {58, "F1"}, {59, "F2"}, {60, "F3"}, {61, "F4"}, {62, "F5"}, {63, "F6"}, {64, "F7"}, {65, "F8"}, {66, "F9"},
    {67, "F10"}, {68, "F11"}, {69, "F12"},
    {73, "INSERT"}, {74, "HOME"}, {75, "PGUP"}, {76, "DELETE"}, {77, "END"}, {78, "PGDN"},
    {79, "RIGHTARROW"}, {80, "LEFTARROW"}, {81, "DOWNARROW"}, {82, "UPARROW"},
    {84, "KP_SLASH"}, {85, "KP_MULTIPLY"}, {86, "KP_MINUS"}, {87, "KP_PLUS"}, {88, "KP_ENTER"},
    {89, "KP_1"}, {90, "KP_2"}, {91, "KP_3"}, {92, "KP_4"}, {93, "KP_5"}, {94, "KP_6"}, {95, "KP_7"}, {96, "KP_8"},
    {97, "KP_9"}, {98, "KP_0"}, {99, "KP_DEL"}, {100, "BACKSLASH2"},
    {224, "LCTRL"}, {225, "LSHIFT"}, {226, "LALT"}, {228, "RCTRL"}, {229, "RSHIFT"}, {230, "RALT"},
    {kMouseCode + 1, "MOUSE1"}, {kMouseCode + 2, "MOUSE3"}, {kMouseCode + 3, "MOUSE2"}, {kMouseCode + 4, "MOUSE4"},
    {kMouseCode + 5, "MOUSE5"}, {kWheelUp, "MWHEELUP"}, {kWheelDown, "MWHEELDOWN"},
};

}  // namespace

const BindInfo& bindInfo(int action) { return kInfo[action >= 0 && action < kBindCount ? action : 0]; }

std::string keyName(int code) {
    if (code >= 4 && code <= 29) return std::string(1, char('A' + (code - 4)));
    if (code >= 30 && code <= 38) return std::string(1, char('1' + (code - 30)));
    if (code == 39) return "0";
    for (const Named& n : kNamed)
        if (n.code == code) return n.name;
    if (code >= 0 && code < kMouseCode) return "KEY" + std::to_string(code);  // (a key with no name of ours)
    return "";
}

int keyCode(const std::string& name) {
    std::string n;
    for (char c : name) n.push_back(c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c);
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return 4 + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '1' && n[0] <= '9') return 30 + (n[0] - '1');
    if (n == "0") return 39;
    for (const Named& k : kNamed)
        if (n == k.name) return k.code;
    if (n.size() > 3 && n.compare(0, 3, "KEY") == 0) {
        int v = 0;
        for (size_t i = 3; i < n.size(); ++i) {
            if (n[i] < '0' || n[i] > '9') return kNoKey;
            v = v * 10 + (n[i] - '0');
        }
        return v < kMouseCode ? v : kNoKey;
    }
    return kNoKey;
}

Binds::Binds() {
    for (auto& a : keys)
        for (int& k : a) k = kNoKey;
    auto def = [&](int action, int a, int b = kNoKey, int c = kNoKey) {
        keys[action][0] = a;
        keys[action][1] = b;
        keys[action][2] = c;
    };
    def(kBindForward, keyCode("W"));
    def(kBindBack, keyCode("S"));
    def(kBindLeft, keyCode("A"));
    def(kBindRight, keyCode("D"));
    def(kBindJump, keyCode("SPACE"), kWheelDown, kWheelUp);
    def(kBindCrouch, keyCode("LCTRL"));
    def(kBindWalk, keyCode("LSHIFT"));
    def(kBindFire, keyCode("MOUSE1"));
    def(kBindAltFire, keyCode("MOUSE2"));
    def(kBindReload, keyCode("R"));
    def(kBindUse, keyCode("E"));
    def(kBindPrimary, keyCode("1"));
    def(kBindSecondary, keyCode("2"));
    def(kBindKnife, keyCode("3"));
    def(kBindGrenade, keyCode("4"));
    def(kBindLastWeapon, keyCode("Q"));
    def(kBindInspect, keyCode("F"));
    def(kBindDrop, keyCode("G"));
    def(kBindBuy, keyCode("B"));
    def(kBindScores, keyCode("TAB"));
    def(kBindCall, keyCode("Z"));
    def(kBindPing, keyCode("P"), keyCode("MOUSE3"));
    def(kBindNoclip, keyCode("V"));
    def(kBindClearDecals, keyCode("C"));
}

bool Binds::has(int action, int code) const {
    if (code == kNoKey || action < 0 || action >= kBindCount) return false;
    for (int k : keys[action])
        if (k == code) return true;
    return false;
}

void Binds::set(int action, int code, bool replace) {
    if (action < 0 || action >= kBindCount || code == kNoKey) return;
    for (auto& a : keys)  // one key, one action
        for (int& k : a)
            if (k == code) k = kNoKey;
    int* slots = keys[action];
    if (replace) {
        slots[0] = code;
        return;
    }
    for (int s = 0; s < kBindSlots; ++s)
        if (slots[s] == kNoKey) { slots[s] = code; return; }
    slots[kBindSlots - 1] = code;  // (full: the last one goes)
}

void Binds::clear(int action) {
    if (action < 0 || action >= kBindCount) return;
    for (int& k : keys[action]) k = kNoKey;
}

std::string Binds::text(int action) const {
    std::string out;
    if (action < 0 || action >= kBindCount) return out;
    for (int k : keys[action]) {
        if (k == kNoKey) continue;
        if (!out.empty()) out += ' ';
        out += keyName(k);
    }
    return out;
}

bool Binds::parse(int action, const std::string& names) {
    if (action < 0 || action >= kBindCount) return false;
    std::istringstream in(names);
    std::string word;
    int got[kBindSlots] = {kNoKey, kNoKey, kNoKey}, n = 0;
    while (in >> word) {
        if (word == "-" || word == "NONE" || word == "none") continue;
        const int c = keyCode(word);
        if (c == kNoKey) return false;
        if (n < kBindSlots) got[n++] = c;
    }
    for (int s = 0; s < kBindSlots; ++s) keys[action][s] = got[s];
    return true;
}
