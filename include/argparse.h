//
// @author : Morris Franken
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#ifndef ARGPARSE_ARGPARSE_H
#define ARGPARSE_ARGPARSE_H

#include <vector>
#include <string>
#include <map>
#include <set>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

#if __cplusplus < 201703L
// Allow it to compile for c++11 by partially adding c++17 types
#include <memory>
#include <typeindex>

namespace std {
    typedef string string_view;

    struct nullopt_t {} nullopt;
    template<typename T> struct optional { // since we only use optional<string> this is OK
        shared_ptr<T> _value;
        typedef T value_type;

        optional() : _value(nullptr) {}
        optional(const nullopt_t &v) : _value(nullptr) {}
        optional(const T &value) : _value(std::make_shared<T>(value)) {}
        optional(const char *value) : _value(std::make_shared<T>(value)) {} //implicit conversion from char[] to string in pre-c++17
        const T &operator*() const {return *_value;}
        bool has_value() const {return _value != nullptr;}
        const T &value_or(const T &_or) const {return _value != nullptr? *_value : _or;}

    };
}
#else // c++17 support
#include <optional>
#endif

#define CONSTRUCTOR(T) T(int argc, char* argv[]) : argparse::Args(argc, argv) {validate();}

namespace argparse {
    using std::cout;
    using std::cerr;
    using std::endl;
    using std::setw;

    template<typename T> struct is_vector : public std::false_type {};
    template<typename T, typename A> struct is_vector<std::vector<T, A>> : public std::true_type {};

    template<typename T> struct is_optional : public std::false_type {};
    template<typename T> struct is_optional<std::optional<T>> : public std::true_type {};

    template<typename T, typename = decltype(std::declval<std::ostream&>() << std::declval<T const&>())> std::string toString(const T &v) {
        std::ostringstream out;
        out << v;
        return out.str();
    }
    template<typename T, typename... Ignored > std::string toString(const T &v, const Ignored &...) {
        return "unknown";
    }

    std::vector<std::string> inline split(const std::string &str) {
        std::vector<std::string> splits;
        for (size_t start=0, end=0; end != std::string::npos; start=end+1) {
            end = str.find(',', start);
            splits.emplace_back(str.substr(start, end - start));
        }
        return splits;
    }

    template<typename T> inline T get(const std::string &v);
    template<> inline std::string get(const std::string &v) { return v; }
    template<> inline char get(const std::string &v) { return v.empty()? throw std::invalid_argument("empty string") : v.size() > 1?  v.substr(0,2) == "0x"? (char)std::stoul(v, nullptr, 16) : (char)std::stoi(v) : v[0]; }
    template<> inline int get(const std::string &v) { return std::stoi(v); }
    template<> inline long get(const std::string &v) { return std::stol(v); }
    template<> inline bool get(const std::string &v) { return v == "true" || v == "TRUE" || v == "1"; }
    template<> inline float get(const std::string &v) { return std::stof(v); }
    template<> inline double get(const std::string &v) { return std::stod(v); }
    template<> inline unsigned char get(const std::string &v) { return get<char>(v); }
    template<> inline unsigned int get(const std::string &v) { return std::stoul(v); }
    template<> inline unsigned long get(const std::string &v) { return std::stoul(v); }
    template<typename T> T get_vector_optional_default(std::true_type is_vector, std::false_type is_optional, const std::string &v) {
        const std::vector<std::string> splitted = split(v);
        T res(splitted.size());
        if (!v.empty())
            std::transform (splitted.begin(), splitted.end(), res.begin(), get<typename T::value_type>);
        return res;
    }
    template<typename T> T get_vector_optional_default(std::false_type is_vector, std::true_type is_optional, const std::string &v) {
        return std::optional<typename T::value_type>(get<typename T::value_type>(v));
    }
    template<typename T> T get_vector_optional_default(std::false_type is_vector, std::false_type is_optional, const std::string &v) {
        return T(v);
    }
    template<typename T> inline T get(const std::string &v) { // "if constexpr" are only supported from c++17, so use this to distuingish vectors.
        return get_vector_optional_default<T>(is_vector<T>{}, is_optional<T>{}, v);
    }

    struct Entry {
        enum ARG_TYPE {ARG, KWARG, FLAG} type;
        std::vector<std::string> keys;
        std::string help;
        std::string value;
        std::optional<std::string> implicit_value;
        std::optional<std::string> default_str;
//        std::string_view value_type; // remove value_type, since it does not add much to the help
        std::string error;

        Entry(ARG_TYPE type, const std::string& key, std::string help, std::optional<std::string> implicit_value=std::nullopt) :
                type(type),
                keys(split(key)),
                help(std::move(help)),
                implicit_value(std::move(implicit_value)) {
        }

        template <typename T> auto find(const T &collection) const -> decltype(collection.end()) {
            const auto end = collection.end();
            for (const std::string &key : keys) {
                const auto &itt = collection.find(key);
                if (itt != end)
                    return itt;
            }
            return end;
        }

        std::string get_keys() const {
            std::stringstream ss;
            for (size_t i = 0; i < keys.size(); i++)
                ss << (i? "," : "") << (type == ARG? "" : (keys[i].size() > 1? "--" : "-")) + keys[i];
            return ss.str();
        }

        template <typename T> T inline set_default(const T &default_value) {
            default_str = toString(default_value);
            return _convert<T>(&default_value);
        }

        // Magically convert the value string to the requested type
        template <typename T> inline operator T() {
            return _convert<T>();
        };

        private:
        template <typename T> T _convert(const T *default_value=nullptr) {
            if (!error.empty())
                return T{};

            if (value.empty()) {
                if (!default_value) {
                    error = "Argument missing: " + get_keys();
                    return T{};
                } else {
                    value = *default_str; // for printing
                    return *default_value;
                }
            }

            try {
                return get<T>(value);
            } catch (const std::invalid_argument &e) {
                this->error = "Invalid argument, could not convert \"" + value + "\" for " + get_keys() + " (" + help + ")";
                return T{};
            }
        }
    };

    class Args {
    private:
        int _arg_idx = -1;
        bool _help = false;
        std::string program_name;
        std::vector<Entry> _options;
        std::vector<Entry> _arg_options;

    protected:
        std::vector<std::string> _args;
        std::set<std::string> _flags;
        std::map<std::string_view, std::string> _kwargs;

    public:
        Args(int argc, char *argv[]) : program_name(argv[0]) {
            std::vector<std::string_view> params(argv + 1, argv + argc);

            auto is_param = [&](const size_t &i) -> bool {
                return params.size() > i && (params[i][0] != '-' || (params[i].size() > 1 && std::isdigit(params[i][1])));
            };
            auto add_param = [&](size_t &i, const size_t &start) {
                size_t eq_idx = params[i].find('=');  // check if value was passed using the '=' sign
                if (eq_idx != std::string::npos) {
                    _kwargs[params[i].substr(start, eq_idx - start)] = std::string(params[i].substr(eq_idx + 1));
                } else if (is_param(i + 1)) {
                    _kwargs[params[i].substr(start)] = std::string(params[i + 1]);
                    i++;
                } else {
                    _flags.insert(std::string(params[i].substr(start)));
                }
            };

            for (size_t i = 0; i < params.size(); i++) {
                if (!is_param(i)) {
                    if (params[i].size() > 1 && params[i][1] == '-') {  // long --
                        add_param(i, 2);
                    } else { // short -
                        const size_t j_end = std::min(params[i].size(), params[i].find('=')) - 1;
                        for (size_t j = 1; j < j_end; j++)  // add possible other flags
                            _flags.insert(std::string(1, params[i][j]));
                        add_param(i, j_end);
                    }
                } else {
                    _args.emplace_back(params[i]);
                }
            }

            _help = flag("help", "print help");
        }

        /* Add a positional argument, the order in which it is defined equals the order in which they are being read.
         * help : Description of the variable
         *
         * Returns a reference to the Entry, which will collapse into the requested type in `Entry::operator T()`
         */
        Entry &arg(const std::string &help) {
            _arg_options.emplace_back(Entry::ARG, "arg_" + std::to_string(++_arg_idx), help);
            Entry &entry = _arg_options.back();
            if (_arg_idx < _args.size()) {
                entry.value = _args[_arg_idx];
            }
            return entry;
        }

        /* Add a variable argument that takes a variable.
         * key : A comma-separated string, e.g. "k,key", which denotes the short (-k) and long(--key) keys
         * help : Description of the variable
         * default_value : A default can be set as std::string
         *
         * Returns a reference to the Entry, which will collapse into the requested type in `Entry::operator T()`
         */
        Entry &kwarg(const std::string &key, const std::string &help, const std::optional<std::string>& implicit_value=std::nullopt) {
            _options.emplace_back(Entry::KWARG, key, help, implicit_value);
            Entry &entry = _options.back();
            const auto itt = entry.find(_kwargs);
            if (itt != _kwargs.end()) {
                entry.value = itt->second;
            } else if (implicit_value.has_value()) {
                if (entry.find(_flags) != _flags.end())
                    entry.value = *implicit_value;
            }
            return entry;
        }

        /* Add a flag which will be false by default.
         * key : A comma-separated string, e.g. "k,key", which denotes the short (-k) and long(--key) keys
         * help : Description of the variable
         *
         * Returns a bool that represents whether or not the flag was set
         */
        bool flag(const std::string &key, const std::string &help) {
            _options.emplace_back(Entry::FLAG, key, help, "true");
            Entry &entry = _options.back();
            const auto itt = entry.find(_flags);
            entry.value = std::to_string(itt != _flags.end());
            return itt != _flags.end();
        }

        void help() const {
            cout << "Usage: " << program_name << " ";
            for (const auto &entry : _arg_options)
                cout << entry.keys[0] << ' ';
            cout << " [options...]" << endl;
            for (const auto &entry : _arg_options) {
                const std::string default_value = entry.default_str.has_value()? " [default: " + *entry.default_str + "]" : "";
                cout << setw(17) << entry.keys[0] << " : " << entry.help << default_value << endl;
            }

            cout << endl << "Options:" << endl;
            for (const auto &entry : _options) {
                const std::string default_value = entry.type == Entry::KWARG? entry.default_str.has_value()? "default: " + *entry.default_str : "required" : "";
                const std::string implicit_value = entry.type == Entry::KWARG and entry.implicit_value.has_value()? "implicit: " + *entry.implicit_value : "";
                const std::string info = entry.type == Entry::KWARG? " [" + implicit_value + (implicit_value.empty() or default_value.empty()? "":", ") + default_value + "]" : "";
                cout << setw(17) << entry.get_keys() << " : " << entry.help << info << endl;
            }
        }

        /* Validate all parameters and also check for the help_flag which was set in this constructor
         * Upon error, it will print the error and exit immediatelyy.
         */
        void validate() const {
            if (_help) {
                help();
                exit(0);
            }

            // print errors
            for (const auto &entries : {_arg_options, _options}) {
                for (const auto &entry : entries) {
                    if (!entry.error.empty()) {
                        cerr << entry.error << endl;
                        exit(-1);
                    }
                }
            }
        }

        void print() const {
            for (const auto &entries : {_arg_options, _options}) {
                for (const auto &entry : entries) {
                    std::string snip = entry.type == Entry::ARG ? "(" + (entry.help.size() > 10 ? entry.help.substr(0, 7) + "..." : entry.help) + ")" : "";
                    cout << setw(21) << entry.get_keys() + snip << " : " << entry.value << endl;
                }
            }
        }
    };
}
#endif //ARGPARSE_ARGPARSE_H
