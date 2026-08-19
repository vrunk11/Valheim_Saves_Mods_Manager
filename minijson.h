// ============================================================================
//  minijson.h - lecteur / ecrivain JSON minimaliste, header-only, sans deps.
//  Suffisant pour un fichier de config : objets, tableaux, chaines, nombres,
//  booleens, null, echappements \uXXXX (avec paires de substitution).
//  Les chaines sont manipulees en UTF-8.
// ============================================================================
#ifndef MINIJSON_H
#define MINIJSON_H

#include <string>
#include <vector>
#include <utility>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace mj {

enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };

struct Value {
    Type type;
    bool b;
    double num;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value> > obj;

    Value() : type(NUL), b(false), num(0) {}

    const Value* find(const char* key) const {
        if (type != OBJ) return 0;
        for (size_t i = 0; i < obj.size(); ++i)
            if (obj[i].first == key) return &obj[i].second;
        return 0;
    }
    std::string s(const char* key, const char* def = "") const {
        const Value* v = find(key);
        return (v && v->type == STR) ? v->str : std::string(def);
    }
};

// ------------------------------------------------------------------ ecriture
inline std::string quote(const std::string& in) {
    std::string o = "\"";
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\b': o += "\\b";  break;
        case '\f': o += "\\f";  break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; sprintf(b, "\\u%04x", (unsigned)c); o += b; }
            else o += (char)c;   // UTF-8 recopie tel quel
        }
    }
    o += "\"";
    return o;
}

// ------------------------------------------------------------------ lecture
inline void utf8_append(unsigned cp, std::string& o) {
    if (cp < 0x80) {
        o += (char)cp;
    } else if (cp < 0x800) {
        o += (char)(0xC0 | (cp >> 6));
        o += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += (char)(0xE0 | (cp >> 12));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    } else {
        o += (char)(0xF0 | (cp >> 18));
        o += (char)(0x80 | ((cp >> 12) & 0x3F));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    }
}

inline bool hex4(const std::string& t, size_t i, unsigned& out) {
    if (i + 3 >= t.size()) return false;
    unsigned v = 0;
    for (int k = 0; k < 4; ++k) {
        char c = t[i + k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

struct P {
    const std::string& t;
    size_t i;
    explicit P(const std::string& s) : t(s), i(0) {}

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i;
    }
    bool lit(const char* s) {
        size_t n = strlen(s);
        if (t.compare(i, n, s) == 0) { i += n; return true; }
        return false;
    }
    bool str(std::string& out);
    bool val(Value& v, int depth);
};

inline bool P::str(std::string& out) {
    ws();
    if (i >= t.size() || t[i] != '"') return false;
    ++i;
    out.clear();
    while (i < t.size()) {
        unsigned char c = (unsigned char)t[i];
        if (c == '"') { ++i; return true; }
        if (c == '\\') {
            ++i;
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(t, i, cp)) return false;
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < t.size()
                    && t[i] == '\\' && t[i + 1] == 'u') {
                    unsigned lo = 0;
                    if (hex4(t, i + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                utf8_append(cp, out);
                break;
            }
            default: return false;
            }
            continue;
        }
        out += (char)c;
        ++i;
    }
    return false;   // guillemet fermant manquant
}

inline bool P::val(Value& v, int depth) {
    if (depth > 64) return false;
    ws();
    if (i >= t.size()) return false;
    char c = t[i];

    if (c == '{') {
        ++i; v.type = OBJ; ws();
        if (i < t.size() && t[i] == '}') { ++i; return true; }
        for (;;) {
            std::string k;
            if (!str(k)) return false;
            ws();
            if (i >= t.size() || t[i] != ':') return false;
            ++i;
            Value cv;
            if (!val(cv, depth + 1)) return false;
            v.obj.push_back(std::make_pair(k, cv));
            ws();
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            return false;
        }
    }
    if (c == '[') {
        ++i; v.type = ARR; ws();
        if (i < t.size() && t[i] == ']') { ++i; return true; }
        for (;;) {
            Value cv;
            if (!val(cv, depth + 1)) return false;
            v.arr.push_back(cv);
            ws();
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            return false;
        }
    }
    if (c == '"') { v.type = STR; return str(v.str); }
    if (lit("true"))  { v.type = BOOL; v.b = true;  return true; }
    if (lit("false")) { v.type = BOOL; v.b = false; return true; }
    if (lit("null"))  { v.type = NUL;  return true; }

    {   // nombre
        size_t s = i;
        bool any = false;
        if (i < t.size() && (t[i] == '-' || t[i] == '+')) ++i;
        while (i < t.size() && (isdigit((unsigned char)t[i]) || t[i] == '.' ||
               t[i] == 'e' || t[i] == 'E' || t[i] == '-' || t[i] == '+')) { ++i; any = true; }
        if (!any) return false;
        v.type = NUM;
        v.num = atof(t.substr(s, i - s).c_str());
        return true;
    }
}

// Renvoie false si le texte n'est pas du JSON valide.
inline bool parse(const std::string& text, Value& out) {
    std::string body = text;
    if (body.size() >= 3 && (unsigned char)body[0] == 0xEF
        && (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
        body.erase(0, 3);                       // BOM UTF-8
    P p(body);
    if (!p.val(out, 0)) return false;
    p.ws();
    return p.i == body.size();
}

} // namespace mj
#endif // MINIJSON_H
