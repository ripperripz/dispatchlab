#include "dispatchlab/json.hpp"

#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace dispatchlab::json {

// ---------------------------------------------------------------- Value

Value Value::make_bool(bool b) {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = b;
    return v;
}
Value Value::make_int(std::int64_t i) {
    Value v;
    v.type_ = Type::Number;
    v.integral_ = true;
    v.int_ = i;
    v.double_ = static_cast<double>(i);
    return v;
}
Value Value::make_double(double d) {
    Value v;
    v.type_ = Type::Number;
    v.double_ = d;
    if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 9.0e15) {
        v.integral_ = true;
        v.int_ = static_cast<std::int64_t>(d);
    }
    return v;
}
Value Value::make_string(std::string s) {
    Value v;
    v.type_ = Type::String;
    v.str_ = std::move(s);
    return v;
}
Value Value::make_array() {
    Value v;
    v.type_ = Type::Array;
    return v;
}
Value Value::make_object() {
    Value v;
    v.type_ = Type::Object;
    return v;
}

bool Value::as_bool() const {
    if (type_ != Type::Bool) throw std::runtime_error("json: not a bool");
    return bool_;
}
std::int64_t Value::as_int() const {
    if (type_ != Type::Number || !integral_) throw std::runtime_error("json: not an integer");
    return int_;
}
double Value::as_double() const {
    if (type_ != Type::Number) throw std::runtime_error("json: not a number");
    return integral_ ? static_cast<double>(int_) : double_;
}
const std::string& Value::as_string() const {
    if (type_ != Type::String) throw std::runtime_error("json: not a string");
    return str_;
}
const std::vector<Value>& Value::as_array() const {
    if (type_ != Type::Array) throw std::runtime_error("json: not an array");
    return arr_;
}
std::vector<Value>& Value::as_array_mut() {
    if (type_ != Type::Array) throw std::runtime_error("json: not an array");
    return arr_;
}
const std::vector<std::pair<std::string, Value>>& Value::as_object() const {
    if (type_ != Type::Object) throw std::runtime_error("json: not an object");
    return obj_;
}
const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& [k, v] : obj_) {
        if (k == key) return &v;
    }
    return nullptr;
}
bool Value::has(std::string_view key) const { return find(key) != nullptr; }
const Value& Value::at(std::string_view key) const {
    const Value* v = find(key);
    if (!v) throw std::runtime_error("json: missing key '" + std::string(key) + "'");
    return *v;
}
void Value::set(std::string key, Value v) {
    if (type_ != Type::Object) throw std::runtime_error("json: set on non-object");
    for (auto& [k, old] : obj_) {
        if (k == key) {
            old = std::move(v);
            return;
        }
    }
    obj_.emplace_back(std::move(key), std::move(v));
}
void Value::push(Value v) {
    if (type_ != Type::Array) throw std::runtime_error("json: push on non-array");
    arr_.push_back(std::move(v));
}
std::int64_t Value::get_int(std::string_view key, std::int64_t def) const {
    const Value* v = find(key);
    return v ? v->as_int() : def;
}
double Value::get_double(std::string_view key, double def) const {
    const Value* v = find(key);
    return v ? v->as_double() : def;
}
std::string Value::get_string(std::string_view key, const std::string& def) const {
    const Value* v = find(key);
    return v ? v->as_string() : def;
}

// ---------------------------------------------------------------- parser

namespace {

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    Value parse_document() {
        Value v = parse_value(0);
        skip_ws();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("json parse error: ") + what + " at offset " + std::to_string(pos_));
    }
    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
    }
    char peek() {
        skip_ws();
        if (pos_ >= s_.size()) fail("unexpected end");
        return s_[pos_];
    }
    void expect(char c) {
        if (peek() != c) fail("unexpected character");
        ++pos_;
    }
    bool consume_literal(std::string_view lit) {
        if (s_.substr(pos_, lit.size()) == lit) {
            pos_ += lit.size();
            return true;
        }
        return false;
    }

    Value parse_value(int depth) {
        if (depth > 200) fail("nesting too deep");
        const char c = peek();
        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '"') return Value::make_string(parse_string());
        if (consume_literal("true")) return Value::make_bool(true);
        if (consume_literal("false")) return Value::make_bool(false);
        if (consume_literal("null")) return Value();
        return parse_number();
    }

    Value parse_object(int depth) {
        expect('{');
        Value obj = Value::make_object();
        if (peek() == '}') {
            ++pos_;
            return obj;
        }
        while (true) {
            if (peek() != '"') fail("expected key");
            std::string k = parse_string();
            expect(':');
            obj.set(std::move(k), parse_value(depth + 1));
            const char c = peek();
            ++pos_;
            if (c == '}') break;
            if (c != ',') fail("expected , or }");
        }
        return obj;
    }

    Value parse_array(int depth) {
        expect('[');
        Value arr = Value::make_array();
        if (peek() == ']') {
            ++pos_;
            return arr;
        }
        while (true) {
            arr.push(parse_value(depth + 1));
            const char c = peek();
            ++pos_;
            if (c == ']') break;
            if (c != ',') fail("expected , or ]");
        }
        return arr;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') break;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) fail("bad escape");
            const char e = s_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) fail("bad \\u escape");
                    const unsigned cp = static_cast<unsigned>(std::stoul(std::string(s_.substr(pos_, 4)), nullptr, 16));
                    pos_ += 4;
                    // Only BMP code points are needed for our own files.
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: fail("bad escape");
            }
        }
        return out;
    }

    Value parse_number() {
        skip_ws();
        const std::size_t start = pos_;
        bool is_float = false;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                is_float = true;
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) fail("invalid value");
        const std::string tok(s_.substr(start, pos_ - start));
        char* end = nullptr;
        if (!is_float) {
            errno = 0;
            const long long v = std::strtoll(tok.c_str(), &end, 10);
            if (errno == 0 && end && *end == '\0') return Value::make_int(v);
        }
        const double d = std::strtod(tok.c_str(), &end);
        if (!end || *end != '\0') fail("invalid number");
        return Value::make_double(d);
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

}  // namespace

Value parse(std::string_view text) { return Parser(text).parse_document(); }

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Value parse_file(const std::string& path) { return parse(read_file(path)); }

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw std::runtime_error("short write: " + path);
}

// ---------------------------------------------------------------- writer

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

void Writer::sep() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (!first_.empty()) {
        if (!first_.back()) out_.push_back(',');
        first_.back() = false;
    }
}
Writer& Writer::begin_object() {
    sep();
    out_.push_back('{');
    first_.push_back(true);
    return *this;
}
Writer& Writer::end_object() {
    out_.push_back('}');
    first_.pop_back();
    return *this;
}
Writer& Writer::begin_array() {
    sep();
    out_.push_back('[');
    first_.push_back(true);
    return *this;
}
Writer& Writer::end_array() {
    out_.push_back(']');
    first_.pop_back();
    return *this;
}
Writer& Writer::key(std::string_view k) {
    sep();
    out_.push_back('"');
    out_ += escape(k);
    out_ += "\":";
    after_key_ = true;
    return *this;
}
Writer& Writer::value(std::int64_t v) {
    sep();
    out_ += std::to_string(v);
    return *this;
}
Writer& Writer::value(std::uint64_t v) {
    sep();
    out_ += std::to_string(v);
    return *this;
}
Writer& Writer::value(double v, int precision) {
    sep();
    if (!std::isfinite(v)) {
        out_ += "null";
        return *this;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", precision, v);
    out_ += buf;
    return *this;
}
Writer& Writer::value(bool v) {
    sep();
    out_ += v ? "true" : "false";
    return *this;
}
Writer& Writer::value(std::string_view s) {
    sep();
    out_.push_back('"');
    out_ += escape(s);
    out_.push_back('"');
    return *this;
}
Writer& Writer::null() {
    sep();
    out_ += "null";
    return *this;
}
Writer& Writer::raw(std::string_view s) {
    sep();
    out_ += s;
    return *this;
}

}  // namespace dispatchlab::json
