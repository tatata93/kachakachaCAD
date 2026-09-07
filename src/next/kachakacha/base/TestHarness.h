#pragma once

//! V2の試験土台。
//! 受入基準(acceptance-tests.md)は「1件失敗で残りを停止しない」ことを要求する。
//! V1の自己診断は最初の失敗で return する一本道だったため、そのまま移植してはならない。
//! ここでは1ケースを1つの関数として登録し、例外を捕まえて次のケースへ進む。

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::test {

//! 1件の検証失敗。RunAll がこれを捕まえて、そのケースだけを失敗にする。
class CheckFailure : public std::exception {
public:
    explicit CheckFailure(std::string message) : message_(std::move(message)) {}
    [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

struct Case {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

class Registry {
public:
    static Registry& Instance()
    {
        static Registry registry;
        return registry;
    }

    void Add(std::string suite, std::string name, std::function<void()> body)
    {
        cases_.push_back(Case{std::move(suite), std::move(name), std::move(body)});
    }

    //! 全ケースを走らせ、失敗した件数を返す。失敗しても止まらない。
    [[nodiscard]] int RunAll(std::string_view binaryName) const
    {
        int failed = 0;
        for (const Case& item : cases_) {
            std::string reason;
            bool ok = true;
            try {
                item.body();
            } catch (const CheckFailure& failure) {
                ok = false;
                reason = failure.what();
            } catch (const std::exception& error) {
                ok = false;
                reason = std::string("unexpected exception: ") + error.what();
            } catch (...) {
                ok = false;
                reason = "unexpected non-standard exception";
            }
            if (ok) {
                std::cout << "PASS " << item.suite << " / " << item.name << '\n';
            } else {
                ++failed;
                std::cout << "FAIL " << item.suite << " / " << item.name << "\n     "
                          << reason << '\n';
            }
        }
        std::cout << binaryName << ": " << (cases_.size() - static_cast<std::size_t>(failed))
                  << " passed, " << failed << " failed, " << cases_.size() << " total\n";
        return failed;
    }

    [[nodiscard]] std::size_t Size() const noexcept { return cases_.size(); }

private:
    std::vector<Case> cases_;
};

struct Registrar {
    Registrar(std::string suite, std::string name, std::function<void()> body)
    {
        Registry::Instance().Add(std::move(suite), std::move(name), std::move(body));
    }
};

inline void Require(bool condition, std::string_view message)
{
    if (!condition) {
        throw CheckFailure(std::string(message));
    }
}

inline void RequireNear(double actual, double expected, double tolerance,
    std::string_view message)
{
    const double difference = actual - expected;
    const double absolute = difference < 0.0 ? -difference : difference;
    if (!(absolute <= tolerance)) {
        std::ostringstream text;
        text << message << " (actual=" << actual << " expected=" << expected
             << " tolerance=" << tolerance << ')';
        throw CheckFailure(text.str());
    }
}

inline void RequireEqual(const std::string& actual, const std::string& expected,
    std::string_view message)
{
    if (actual != expected) {
        std::ostringstream text;
        text << message << " (actual=\"" << actual << "\" expected=\"" << expected << "\")";
        throw CheckFailure(text.str());
    }
}

} // namespace kachakacha::v2::test

//! 1ケースを登録する。同じ翻訳単位に何個書いてもよい。
#define KACHA_V2_TEST(suite_name, case_name)                                              \
    static void kacha_v2_case_##suite_name##_##case_name();                               \
    static const ::kachakacha::v2::test::Registrar                                        \
        kacha_v2_registrar_##suite_name##_##case_name(                                    \
            #suite_name, #case_name, &kacha_v2_case_##suite_name##_##case_name);          \
    static void kacha_v2_case_##suite_name##_##case_name()

//! 試験実行ファイルの main。
#define KACHA_V2_TEST_MAIN(binary_name)                                                   \
    int main()                                                                            \
    {                                                                                     \
        const int failed = ::kachakacha::v2::test::Registry::Instance().RunAll(            \
            binary_name);                                                                 \
        return failed == 0 ? 0 : 1;                                                       \
    }
