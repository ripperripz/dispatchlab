#pragma once

// Minimal JSON support: a small DOM + recursive-descent parser for reading
// configs, scenarios, event logs and replays, and a streaming writer that emits
// keys in the order they are written (canonical output for logs and hashes).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dispatchlab::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value make_bool(bool b);
    static Value make_int(std::int64_t v);
    static Value make_double(double v);
    static Value make_string(std::string s);
    static Value make_array();
    static Value make_object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool() const;
    std::int64_t as_int() const;  // throws unless the number is integral
    double as_double() const;
    const std::string& as_string() const;
    const std::vector<Value>& as_array() const;
    std::vector<Value>& as_array_mut();
    const std::vector<std::pair<std::string, Value>>& as_object() const;

    bool has(std::string_view key) const;
    const Value& at(std::string_view key) const;  // throws if missing
    const Value* find(std::string_view key) const;
    void set(std::string key, Value v);  // object only, appends or replaces
    void push(Value v);                  // array only

    std::int64_t get_int(std::string_view key, std::int64_t def) const;
    double get_double(std::string_view key, double def) const;
    std::string get_string(std::string_view key, const std::string& def) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    bool integral_ = false;
    std::int64_t int_ = 0;
    double double_ = 0.0;
    std::string str_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

Value parse(std::string_view text);
Value parse_file(const std::string& path);

// Streaming writer producing compact JSON.
class Writer {
public:
    Writer& begin_object();
    Writer& end_object();
    Writer& begin_array();
    Writer& end_array();
    Writer& key(std::string_view k);
    Writer& value(std::int64_t v);
    Writer& value(std::uint64_t v);
    Writer& value(int v) { return value(static_cast<std::int64_t>(v)); }
    Writer& value(unsigned v) { return value(static_cast<std::uint64_t>(v)); }
    Writer& value(double v, int precision = 6);
    Writer& value(bool v);
    Writer& value(std::string_view s);
    Writer& value(const char* s) { return value(std::string_view(s)); }
    Writer& null();
    Writer& raw(std::string_view s);  // pre-serialized JSON
    template <class T>
    Writer& kv(std::string_view k, const T& v) {
        key(k);
        return value(v);
    }
    Writer& kv(std::string_view k, double v, int precision) {
        key(k);
        return value(v, precision);
    }
    const std::string& str() const { return out_; }
    std::string take() { return std::move(out_); }

private:
    void sep();
    std::string out_;
    std::vector<bool> first_;  // per nesting level: no element written yet
    bool after_key_ = false;
};

std::string escape(std::string_view s);
void write_file(const std::string& path, const std::string& content);
std::string read_file(const std::string& path);

}  // namespace dispatchlab::json
